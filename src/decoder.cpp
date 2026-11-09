#include "ulacr/decoder.hpp"
#include "ulacr/io/wav_writer.hpp"
#include "ulacr/io/ulacr_file.hpp"
#include "ulacr/io/bitstream.hpp"
#include "ulacr/analysis/harmonic_model.hpp"
#include "ulacr/graph/block_graph.hpp"
#include "ulacr/transform/wavelet.hpp"
#include "ulacr/coding/residual_island.hpp"
#include "ulacr/coding/bitplane.hpp"
#include "ulacr/coding/context_model.hpp"
#include "ulacr/coding/rans_coder.hpp"
#include "ulacr/coding/range_coder.hpp"
#include "pipeline_common.hpp"

#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <limits>

namespace ulacr {

// =================================================================
// Decoder::Impl
// =================================================================
struct Decoder::Impl {
    ProgressCallback progress;
};

Decoder::Decoder() = default;
Decoder::~Decoder() = default;

void Decoder::set_progress_callback(ProgressCallback cb) noexcept {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->progress = std::move(cb);
}

namespace {

void report(const ProgressCallback& cb, float p, std::string_view phase) {
    if (cb) cb(p, phase);
}

std::vector<uint8_t> read_whole_file(const std::filesystem::path& path, bool& ok) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { ok = false; return {}; }
    std::streamoff size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    ok = f.good() || f.eof();
    return data;
}

} // namespace

// ---------------------------------------------------------------
// ヘッダのみ読み込んで AudioSpec を取得する
// ---------------------------------------------------------------
Error Decoder::probe(const std::filesystem::path& input_path, AudioSpec& spec_out) {
    bool ok = false;
    auto data = read_whole_file(input_path, ok);
    if (!ok) return Error::IOError;

    try {
        io::BitReader r(data);
        io::read_and_check_file_magic(r);
        spec_out = io::read_audio_spec(r);
    } catch (const io::BitstreamError&) {
        return Error::CorruptedData;
    }
    return Error::OK;
}

// ---------------------------------------------------------------
// メインデコードパイプライン
// ---------------------------------------------------------------
Error Decoder::decode(const std::filesystem::path& input_path, AudioBuffer& out) {
    bool ok = false;
    auto data = read_whole_file(input_path, ok);
    if (!ok) return Error::IOError;

    try {
        io::BitReader r(data);

        report(impl_ ? impl_->progress : ProgressCallback{}, 0.0f, "header");

        uint8_t file_minor = 0;
        io::read_and_check_file_magic(r, &file_minor);
        AudioSpec spec = io::read_audio_spec(r);
        io::DecodeFlags flags = io::read_decode_flags(r, file_minor);

        const uint32_t num_channels = spec.num_channels;
        if (num_channels == 0) return Error::CorruptedData;

        // --- セグメント長一覧 ---
        size_t num_segments = static_cast<size_t>(r.read_uvarint());
        std::vector<Segment> segments(num_segments);
        {
            uint64_t offset = 0;
            for (size_t i = 0; i < num_segments; ++i) {
                uint32_t len = static_cast<uint32_t>(r.read_uvarint());
                segments[i].offset = offset;
                segments[i].length = len;
                segments[i].local_entropy = 0.0f;
                offset += len;
            }
        }

        // --- Block Graph エッジ ---
        BlockGraph graph;
        std::unordered_map<uint32_t, uint32_t> ref_map;
        if (flags.enable_block_graph) {
            size_t num_edges = static_cast<size_t>(r.read_uvarint());
            graph.edges.resize(num_edges);
            for (auto& e : graph.edges) {
                e.tgt_id     = static_cast<uint32_t>(r.read_uvarint());
                e.ref_id     = static_cast<uint32_t>(r.read_uvarint());
                e.similarity = 0.0f;
            }
            for (const auto& e : graph.edges) ref_map[e.tgt_id] = e.ref_id;
        }

        // --- チャンネル毎・セグメント毎のメタデータ ---
        struct SubbandMeta {
            uint32_t num_values = 0;
            uint32_t num_planes = 0;
            std::vector<Sample> sparse_map;
        };
        struct SegMeta {
            uint32_t levels_done = 0;
            std::vector<SubbandMeta> subbands;
            analysis::HarmonicModel model;
            bool has_model = false;
            uint8_t prediction_order = 0;
            bool prediction_reverse = false;
            uint8_t scale_shift = 0;
            std::vector<Sample> prediction_seeds;
        };
        std::vector<std::vector<SegMeta>> meta(
            num_channels, std::vector<SegMeta>(num_segments));

        for (uint32_t c = 0; c < num_channels; ++c) {
            for (size_t s = 0; s < num_segments; ++s) {
                auto& m = meta[c][s];
                m.levels_done = static_cast<uint32_t>(r.read_uvarint());
                size_t nsb = static_cast<size_t>(r.read_uvarint());
                m.subbands.resize(nsb);
                for (auto& sb : m.subbands) {
                    sb.num_values = static_cast<uint32_t>(r.read_uvarint());
                    sb.num_planes = flags.enable_bitplane
                                        ? static_cast<uint32_t>(r.read_uvarint())
                                        : 0;
                    if (file_minor >= 3) {
                        const uint64_t map_count = r.read_uvarint();
                        if (map_count > 32) throw io::BitstreamError("invalid sparse map size");
                        sb.sparse_map.reserve(static_cast<size_t>(map_count));
                        for (uint64_t j = 0; j < map_count; ++j)
                            sb.sparse_map.push_back(detail::unzigzag_sample(r.read_uvarint()));
                        if (!std::is_sorted(sb.sparse_map.begin(), sb.sparse_map.end()) ||
                            std::adjacent_find(sb.sparse_map.begin(), sb.sparse_map.end()) != sb.sparse_map.end())
                            throw io::BitstreamError("invalid sparse map ordering");
                    }
                }

                if (flags.enable_harmonic) {
                    bool has_ref = ref_map.count(static_cast<uint32_t>(s)) > 0;
                    if (!has_ref) {
                        analysis::HarmonicModel model;
                        model.sample_rate      = spec.sample_rate;
                        model.length           = segments[s].length;
                        model.fundamental_freq = static_cast<double>(r.read_f32());
                        if (model.fundamental_freq > 0.0) {
                            model.amplitudes.resize(flags.max_harmonics);
                            model.phases.resize(flags.max_harmonics);
                            for (auto& a : model.amplitudes) a = static_cast<double>(r.read_f32());
                            for (auto& p : model.phases)     p = static_cast<double>(r.read_f32());
                        }
                        m.model     = std::move(model);
                        m.has_model = true;
                    }
                }

                if (file_minor >= 2) {
                    const uint8_t encoded_order = r.read_byte();
                    if (file_minor >= 3) m.prediction_reverse = (encoded_order & 0x80u) != 0;
                    m.prediction_order = file_minor >= 3
                        ? static_cast<uint8_t>(encoded_order & 0x7fu) : encoded_order;
                    const uint8_t max_predictor_order = file_minor >= 3 ? 18 : 4;
                    if (m.prediction_order > max_predictor_order) {
                        throw io::BitstreamError("invalid predictor order");
                    }
                    if (m.prediction_order == 18) {
                        const Sample lag = detail::unzigzag_sample(r.read_uvarint());
                        if (lag <= 0 || lag > 64) throw io::BitstreamError("invalid delay-match lag");
                        m.prediction_seeds.push_back(lag);
                        const uint8_t seed_count = static_cast<uint8_t>(lag);
                        for (uint8_t i = 0; i < seed_count; ++i)
                            m.prediction_seeds.push_back(
                                detail::unzigzag_sample(r.read_uvarint()));
                    } else {
                        const uint8_t seed_count = m.prediction_order == 17 ? 4 : m.prediction_order;
                        m.prediction_seeds.reserve(seed_count);
                        for (uint8_t i = 0; i < seed_count; ++i) {
                            m.prediction_seeds.push_back(
                                detail::unzigzag_sample(r.read_uvarint()));
                        }
                    }
                    if (file_minor >= 3) {
                        m.scale_shift = r.read_byte();
                        if (m.scale_shift > 31) throw io::BitstreamError("invalid coefficient scale");
                    }
                }
            }
        }

        // --- コンテキストグループ（確率テーブル + rANS復号）---
        report(impl_ ? impl_->progress : ProgressCallback{}, 0.3f, "entropy-decoding");

        struct GroupData {
            std::vector<uint32_t> symbols;
            size_t cursor = 0;
        };
        std::unordered_map<coding::ContextKey, GroupData, coding::ContextKeyHash> group_data;

        size_t num_groups = static_cast<size_t>(r.read_uvarint());
        coding::RansDecoder rans_dec;
        coding::AdaptiveRangeDecoder range_dec;
        coding::AdaptiveMixtureRangeDecoder mixture_range_dec;
        for (size_t g = 0; g < num_groups; ++g) {
            uint8_t stream_type = r.read_byte();
            uint8_t plane_idx   = r.read_byte();
            uint8_t wlevel      = r.read_byte();
            uint8_t channel     = r.read_byte();

            uint64_t symbol_count = r.read_uvarint();
            coding::ContextKey key = detail::make_ctx(channel, wlevel, stream_type, plane_idx);

            GroupData gd;
            if (symbol_count > 0) {
                const uint32_t alphabet_field = static_cast<uint32_t>(r.read_uvarint());
                const bool use_mixture = file_minor >= 3 && (alphabet_field & 0x80000000u) != 0;
                const uint32_t used_alphabet = file_minor >= 3
                    ? (alphabet_field & 0x7fffffffu) : alphabet_field;
                if (used_alphabet == 0 || used_alphabet > 65536) {
                    throw io::BitstreamError("invalid symbol alphabet");
                }
                if (file_minor >= 3) {
                    uint64_t encoded_len = r.read_uvarint();
                    auto encoded_bytes = r.read_bytes(static_cast<size_t>(encoded_len));
                    try {
                        gd.symbols = use_mixture
                            ? mixture_range_dec.decode(encoded_bytes, used_alphabet, symbol_count)
                            : range_dec.decode(encoded_bytes, used_alphabet, symbol_count);
                    } catch (const std::exception&) {
                        throw io::BitstreamError("invalid adaptive range stream");
                    }
                } else {
                    std::vector<uint32_t> counts(used_alphabet);
                    for (auto& cnt : counts) cnt = static_cast<uint32_t>(r.read_uvarint());
                    uint64_t encoded_len = r.read_uvarint();
                    auto encoded_bytes = r.read_bytes(static_cast<size_t>(encoded_len));
                    auto table = coding::build_prob_table(counts, used_alphabet);
                    gd.symbols = rans_dec.decode(encoded_bytes, table, symbol_count);
                }
            }
            group_data.emplace(key, std::move(gd));
        }

        // --- 生ビット列（仮数ビット）---
        uint64_t raw_len = r.read_uvarint();
        auto raw_bytes = r.read_bytes(static_cast<size_t>(raw_len));
        io::BitReader raw_reader(raw_bytes);

        // --- 再構成 ---
        report(impl_ ? impl_->progress : ProgressCallback{}, 0.4f, "reconstruction");

        out.spec = spec;
        out.channels.assign(num_channels, SampleVec(spec.num_samples, 0));

        CodecConfig cfg_local;
        cfg_local.wavelet_levels = flags.wavelet_levels;
        WaveletTransform wavelet(cfg_local);
        analysis::HarmonicAnalyzer harmonic(cfg_local);

        std::vector<std::vector<SampleVec>> recon_segments(
            num_channels, std::vector<SampleVec>(num_segments));

        for (uint32_t c = 0; c < num_channels; ++c) {
            for (size_t s = 0; s < num_segments; ++s) {
                auto& m = meta[c][s];

                auto sizes = detail::compute_subband_sizes(segments[s].length, m.levels_done);

                WaveletCoeffs coeffs;
                coeffs.levels = m.levels_done;
                coeffs.subbands.resize(sizes.size());

                for (size_t sb = 0; sb < sizes.size(); ++sb) {
                    uint8_t  subband_level = static_cast<uint8_t>(sb);
                    uint32_t subband_len   = sizes[sb];
                    uint32_t num_values    = m.subbands[sb].num_values;
                    uint32_t num_zero_runs = num_values + 1;

                    coding::ResidualIslandData island;
                    island.total_length = subband_len;
                    island.zero_runs.resize(num_zero_runs);
                    island.values.resize(num_values);

                    // --- ゼロ連続長 ---
                    {
                        auto key = detail::make_ctx(static_cast<uint8_t>(c), subband_level, detail::STREAM_RUNS);
                        auto& gd = group_data.at(key);
                        for (uint32_t i = 0; i < num_zero_runs; ++i) {
                            uint32_t bucket = gd.symbols[gd.cursor++];
                            uint32_t v;
                            if (bucket == 0) {
                                v = 0;
                            } else {
                                uint32_t mbits = bucket - 1;
                                uint64_t mantissa = (mbits > 0) ? raw_reader.read_bits(mbits) : 0;
                                v = static_cast<uint32_t>((1ull << mbits) | mantissa);
                            }
                            island.zero_runs[i] = v;
                        }
                    }

                    // --- 非ゼロ値 ---
                    if (flags.enable_bitplane) {
                        uint32_t num_planes = m.subbands[sb].num_planes;
                        coding::BitplaneData bp;
                        bp.num_planes = num_planes;
                        bp.num_values = num_values;
                        bp.planes.assign(num_planes, std::vector<uint8_t>(num_values, 0));

                        for (uint32_t p = 0; p < num_planes; ++p) {
                            uint8_t plane_ctx = detail::clamp_plane(p);
                            auto key = detail::make_ctx(static_cast<uint8_t>(c), subband_level,
                                                         detail::STREAM_PLANE, plane_ctx);
                            auto& gd = group_data.at(key);
                            for (uint32_t i = 0; i < num_values; ++i) {
                                bp.planes[p][i] = static_cast<uint8_t>(gd.symbols[gd.cursor++]);
                            }
                        }
                        island.values = coding::BitplaneDecomposer::reconstruct(bp);
                    } else {
                        auto key = detail::make_ctx(static_cast<uint8_t>(c), subband_level, detail::STREAM_VALUE);
                        auto& gd = group_data.at(key);
                        for (uint32_t i = 0; i < num_values; ++i) {
                            uint32_t bucket = gd.symbols[gd.cursor++];
                            uint64_t zz;
                            if (bucket == 0) {
                                zz = 0;
                            } else {
                                uint32_t mbits = bucket - 1;
                                uint64_t mantissa = (mbits > 0) ? raw_reader.read_bits(mbits) : 0;
                                zz = (1ull << mbits) | mantissa;
                            }
                            island.values[i] = coding::BitplaneDecomposer::zigzag_decode(zz);
                        }
                    }

                    const auto& sparse_map = m.subbands[sb].sparse_map;
                    if (!sparse_map.empty()) {
                        for (Sample& index : island.values) {
                            if (index < 0 || static_cast<uint64_t>(index) >= sparse_map.size())
                                throw io::BitstreamError("sparse map index out of range");
                            index = sparse_map[static_cast<size_t>(index)];
                        }
                    }
                    coeffs.subbands[sb] = coding::ResidualIslandCoder::decode(island);
                }

                if (m.scale_shift > 0) {
                    const Sample multiplier = static_cast<Sample>(1ull << m.scale_shift);
                    for (auto& band : coeffs.subbands) {
                        for (Sample& value : band) {
                            if (value > std::numeric_limits<Sample>::max() / multiplier ||
                                value < std::numeric_limits<Sample>::min() / multiplier) {
                                throw io::BitstreamError("coefficient scale overflow");
                            }
                            value *= multiplier;
                        }
                    }
                }

                SampleVec wavelet_input;
                if (flags.enable_wavelet) {
                    wavelet_input = wavelet.inverse(coeffs);
                } else {
                    wavelet_input = coeffs.subbands.empty() ? SampleVec{} : coeffs.subbands[0];
                }

                if (file_minor >= 2 && m.prediction_order > 0) {
                    // The stored residual is already in the predictor's scan
                    // direction. Invert the predictor first, then restore the
                    // original sample order.
                    wavelet_input = detail::inverse_predictor(
                        wavelet_input, m.prediction_order, m.prediction_seeds,
                        detail::nlms_normalization_shift(spec.format));
                    if (wavelet_input.size() != segments[s].length) {
                        return Error::CorruptedData;
                    }
                    if (m.prediction_reverse)
                        std::reverse(wavelet_input.begin(), wavelet_input.end());
                }

                SampleVec samples;
                auto ref_it = ref_map.find(static_cast<uint32_t>(s));
                if (ref_it != ref_map.end()) {
                    uint32_t ref_id = ref_it->second;
                    const SampleVec& ref_samples = recon_segments[c][ref_id];
                    samples.resize(segments[s].length);
                    for (uint32_t i = 0; i < segments[s].length; ++i) {
                        samples[i] = wavelet_input[i] + ref_samples[i];
                    }
                } else if (m.has_model) {
                    samples = harmonic.reconstruct(wavelet_input, m.model);
                } else {
                    samples = std::move(wavelet_input);
                }

                recon_segments[c][s] = samples;

                for (uint32_t i = 0; i < segments[s].length; ++i) {
                    out.channels[c][segments[s].offset + i] = samples[i];
                }
            }
            report(impl_ ? impl_->progress : ProgressCallback{},
                   0.4f + 0.55f * static_cast<float>(c + 1) / static_cast<float>(num_channels),
                   "reconstruction");
        }

        // Mid/Sideから元のLeft/Rightへ完全可逆変換
        if (flags.stereo_mid_side && num_channels == 2) {
            for (uint64_t i = 0; i < spec.num_samples; ++i) {
                const Sample mid = out.channels[0][i];
                const Sample side = out.channels[1][i];
                out.channels[0][i] = mid + detail::ceil_div2(side);
                out.channels[1][i] = mid - detail::floor_div2(side);
            }
        }

    } catch (const io::BitstreamError&) {
        return Error::CorruptedData;
    } catch (const std::out_of_range&) {
        return Error::CorruptedData;
    }

    report(impl_ ? impl_->progress : ProgressCallback{}, 1.0f, "done");
    return Error::OK;
}

// ---------------------------------------------------------------
// .ulacr -> PCM ファイルへ直接書き出し
// ---------------------------------------------------------------
Error Decoder::decode_file(const std::filesystem::path& input_path,
                           const std::filesystem::path& output_path) {
    AudioBuffer buf;
    Error err = decode(input_path, buf);
    if (err != Error::OK) return err;
    return io::WavWriter::write(output_path, buf);
}

} // namespace ulacr
