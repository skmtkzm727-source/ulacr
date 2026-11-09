#include "ulacr/encoder.hpp"
#include "ulacr/io/wav_reader.hpp"
#include "ulacr/io/ulacr_file.hpp"
#include "ulacr/io/bitstream.hpp"
#include "ulacr/analysis/segmenter.hpp"
#include "ulacr/analysis/harmonic_model.hpp"
#include "ulacr/graph/block_graph.hpp"
#include "ulacr/transform/wavelet.hpp"
#include "ulacr/coding/residual_island.hpp"
#include "ulacr/coding/bitplane.hpp"
#include "ulacr/coding/context_model.hpp"
#include "ulacr/coding/rans_coder.hpp"
#include "pipeline_common.hpp"

#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <system_error>

namespace ulacr {

// =================================================================
// Encoder::Impl
// =================================================================
struct Encoder::Impl {
    CodecConfig      cfg;
    ProgressCallback progress;
};

Encoder::Encoder(const CodecConfig& cfg) noexcept : impl_(std::make_unique<Impl>()) {
    impl_->cfg = cfg;
}

Encoder::~Encoder() = default;

void Encoder::set_progress_callback(ProgressCallback cb) noexcept {
    impl_->progress = std::move(cb);
}

namespace {

void report(const ProgressCallback& cb, float p, std::string_view phase) {
    if (cb) cb(p, phase);
}

uint64_t estimate_binary_plane_bits(const std::vector<uint8_t>& plane) {
    if (plane.empty()) return 0;
    uint64_t ones = 0;
    for (uint8_t bit : plane) ones += bit != 0;
    const uint64_t zeros = plane.size() - ones;
    auto entropy = [](uint64_t count, uint64_t total) -> double {
        if (count == 0) return 0.0;
        const double p = static_cast<double>(count) / static_cast<double>(total);
        return -static_cast<double>(count) * std::log2(p);
    };
    return static_cast<uint64_t>(std::ceil(entropy(ones, plane.size()) +
                                           entropy(zeros, plane.size()))) + 16u;
}

uint64_t estimate_wavelet_payload_bits(std::span<const Sample> input,
                                       const WaveletTransform& wavelet,
                                       const CodecConfig& cfg) {
    WaveletCoeffs coeffs;
    if (cfg.enable_wavelet) {
        coeffs = wavelet.forward(input);
    } else {
        coeffs.levels = 0;
        coeffs.subbands.emplace_back(input.begin(), input.end());
    }

    uint64_t bits = 0;
    for (const auto& subband : coeffs.subbands) {
        const auto island = coding::ResidualIslandCoder::encode(subband);
        // Zero-run symbols use a bucket in rANS and lower bits in the raw stream.
        bits += 8u * 4u; // conservative per-subband metadata/table allowance
        for (uint32_t run : island.zero_runs) {
            const uint32_t bucket = detail::bit_length_u64(run);
            bits += 5u + (bucket > 0 ? bucket - 1u : 0u);
        }

        if (cfg.enable_bitplane) {
            const auto bp = coding::BitplaneDecomposer::decompose(island.values);
            for (const auto& plane : bp.planes) {
                bits += estimate_binary_plane_bits(plane);
            }
        } else {
            for (Sample value : island.values) {
                const uint64_t zz = detail::zigzag_u64(value);
                const uint32_t bucket = detail::bit_length_u64(zz);
                bits += 5u + (bucket > 0 ? bucket - 1u : 0u);
            }
        }
    }
    return bits;
}

detail::PredictorResult choose_predictor_by_payload_cost(
        std::span<const Sample> input,
        const WaveletTransform& wavelet,
        const CodecConfig& cfg,
        bool enabled,
        uint8_t max_order) {
    detail::PredictorResult best;
    best.residual.assign(input.begin(), input.end());
    best.estimated_bits = estimate_wavelet_payload_bits(best.residual, wavelet, cfg) + 8u;
    if (!enabled || input.size() < 2) return best;

    max_order = std::min<uint8_t>(max_order, 16);
    for (uint8_t order = 1; order <= max_order && order < input.size(); ++order) {
        SampleVec residual(input.size(), 0);
        std::vector<Sample> seeds(input.begin(), input.begin() + order);
        for (size_t i = order; i < input.size(); ++i) {
            residual[i] = input[i] - detail::predict_from_history(input, i, order);
        }
        uint64_t cost = estimate_wavelet_payload_bits(residual, wavelet, cfg) + 8u;
        for (Sample seed : seeds) {
            cost += 8u * detail::uvarint_size(detail::zigzag_u64(seed));
        }
        if (cost < best.estimated_bits) {
            best.residual = std::move(residual);
            best.order = order;
            best.seeds = std::move(seeds);
            best.estimated_bits = cost;
        }
    }
    return best;
}

uint64_t estimate_audio_cost(const AudioBuffer& audio,
                             const std::vector<Segment>& segments,
                             const CodecConfig& cfg) {
    const bool integer_pcm = audio.spec.format == SampleFormat::Int16 ||
                             audio.spec.format == SampleFormat::Int24 ||
                             audio.spec.format == SampleFormat::Int32;
    WaveletTransform wavelet(cfg);
    uint64_t total_bits = 0;
    for (const auto& channel : audio.channels) {
        for (const Segment& segment : segments) {
            const std::span<const Sample> samples(
                channel.data() + segment.offset, segment.length);
            const uint8_t max_order = cfg.profile == EncoderProfile::Ultra
                ? std::max<uint8_t>(cfg.max_predictor_order, 16)
                : std::min<uint8_t>(cfg.max_predictor_order, 4);
            const auto predictor = choose_predictor_by_payload_cost(
                samples, wavelet, cfg, cfg.enable_predictive_coding && integer_pcm, max_order);
            total_bits += predictor.estimated_bits;
        }
    }
    return total_bits;
}

} // namespace

// ---------------------------------------------------------------
// ファイルからエンコード（WAV/AIFF/RAW 入力）
// ---------------------------------------------------------------
Error Encoder::encode_file(const std::filesystem::path& input_path,
                           const std::filesystem::path& output_path) {
    AudioBuffer buf;
    Error err = io::WavReader::read(input_path, buf);
    if (err != Error::OK) return err;
    return encode(buf, output_path);
}

// ---------------------------------------------------------------
// メインエンコードパイプライン
// ---------------------------------------------------------------
Error Encoder::encode(const AudioBuffer& buf, const std::filesystem::path& output_path) {
    if (!buf.valid() || buf.spec.num_samples == 0) return Error::InvalidArg;

    // Auto compares complete serialized outputs; selection is based on actual
    // file length, not an entropy estimate.
    if (impl_->cfg.profile == EncoderProfile::Auto) {
        CodecConfig classic_cfg = impl_->cfg;
        classic_cfg.profile = EncoderProfile::Classic;
        classic_cfg.max_predictor_order = 4;
        CodecConfig ultra_cfg = impl_->cfg;
        ultra_cfg.profile = EncoderProfile::Ultra;
        ultra_cfg.max_predictor_order = 16;
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto stem = output_path.string() + ".auto-" + std::to_string(nonce);
        const std::filesystem::path classic_tmp(stem + "-classic");
        const std::filesystem::path ultra_tmp(stem + "-ultra");
        Encoder classic_encoder(classic_cfg);
        Encoder ultra_encoder(ultra_cfg);
        const Error classic_err = classic_encoder.encode(buf, classic_tmp);
        const Error ultra_err = ultra_encoder.encode(buf, ultra_tmp);
        if (classic_err != Error::OK || ultra_err != Error::OK) {
            std::error_code ignored;
            std::filesystem::remove(classic_tmp, ignored);
            std::filesystem::remove(ultra_tmp, ignored);
            return classic_err != Error::OK ? classic_err : ultra_err;
        }
        std::error_code fs_error;
        const auto classic_size = std::filesystem::file_size(classic_tmp, fs_error);
        if (fs_error) return Error::IOError;
        const auto ultra_size = std::filesystem::file_size(ultra_tmp, fs_error);
        if (fs_error) return Error::IOError;
        const auto& winner = ultra_size < classic_size ? ultra_tmp : classic_tmp;
        std::filesystem::copy_file(winner, output_path,
            std::filesystem::copy_options::overwrite_existing, fs_error);
        std::error_code ignored;
        std::filesystem::remove(classic_tmp, ignored);
        std::filesystem::remove(ultra_tmp, ignored);
        return fs_error ? Error::IOError : Error::OK;
    }

    CodecConfig effective_cfg = impl_->cfg;
    const bool integer_pcm = buf.spec.format == SampleFormat::Int16 ||
                             buf.spec.format == SampleFormat::Int24 ||
                             buf.spec.format == SampleFormat::Int32;
    // In v0.3 float samples are opaque bit patterns, not numeric integers.
    // Disable arithmetic transforms so every IEEE-754 bit is preserved.
    if (effective_cfg.profile == EncoderProfile::Ultra && !integer_pcm) {
        effective_cfg.enable_block_graph = false;
        effective_cfg.enable_harmonic = false;
        effective_cfg.enable_predictive_coding = false;
        effective_cfg.enable_stereo_mid_side = false;
        effective_cfg.enable_wavelet = false;
    }
    const CodecConfig& cfg = effective_cfg;
    if (cfg.profile != EncoderProfile::Classic && cfg.profile != EncoderProfile::Ultra)
        return Error::UnsupportedFormat;
    const uint32_t num_channels = buf.spec.num_channels;
    AudioBuffer coding_buf = buf;
    bool use_mid_side = false;

    // --- 1. 適応的セグメンテーション ---
    report(impl_->progress, 0.0f, "segmentation");
    Segmenter segmenter(cfg);
    std::vector<Segment> segments = segmenter.segment(buf);
    if (segments.empty()) return Error::InvalidArg;

    // M/S変換は常時適用しない。変換前後の実際のウェーブレット＋
    // Residual-Island/entropy近似コストを比較し、小さくなる場合だけ採用する。
    if (cfg.enable_stereo_mid_side && integer_pcm && num_channels == 2) {
        AudioBuffer candidate = buf;
        for (uint64_t i = 0; i < buf.spec.num_samples; ++i) {
            const Sample left = buf.channels[0][i];
            const Sample right = buf.channels[1][i];
            const Sample side = left - right;
            const Sample mid = right + detail::floor_div2(side);
            candidate.channels[0][i] = mid;
            candidate.channels[1][i] = side;
        }
        const uint64_t lr_cost = estimate_audio_cost(buf, segments, cfg);
        const uint64_t ms_cost = estimate_audio_cost(candidate, segments, cfg);
        if (ms_cost < lr_cost) {
            coding_buf = std::move(candidate);
            use_mid_side = true;
        }
    }

    // --- 2. Block Graph Compression ---
    report(impl_->progress, 0.05f, "block-graph");
    BlockGraphBuilder graph_builder(cfg);
    BlockGraph graph;
    if (cfg.enable_block_graph) {
        graph = graph_builder.build(coding_buf, segments, [&](float fraction) {
            report(impl_->progress, 0.05f + 0.05f * fraction, "block-graph");
        });
    }

    std::unordered_map<uint32_t, uint32_t> ref_map; // tgt_id -> ref_id
    for (const auto& e : graph.edges) ref_map[e.tgt_id] = e.ref_id;

    // --- 3. 各種解析器 ---
    WaveletTransform            wavelet(cfg);
    analysis::HarmonicAnalyzer  harmonic(cfg);

    // 再構成済み（= 元）セグメントサンプル。Block Graph 差分の参照に使用する。
    std::vector<std::vector<SampleVec>> recon_segments(
        num_channels, std::vector<SampleVec>(segments.size()));

    // ハーモニックモデル (有効時のみ意味を持つ)
    std::vector<std::vector<analysis::HarmonicModel>> harmonic_models(
        num_channels, std::vector<analysis::HarmonicModel>(segments.size()));

    struct SubbandMeta {
        uint32_t num_values = 0;
        uint32_t num_planes = 0;
    };
    struct SegMeta {
        uint32_t levels_done = 0;
        uint8_t prediction_order = 0;
        std::vector<Sample> prediction_seeds;
        std::vector<SubbandMeta> subbands;
    };
    std::vector<std::vector<SegMeta>> meta(
        num_channels, std::vector<SegMeta>(segments.size()));

    // コンテキストグループ: ContextKey -> シンボル列
    std::unordered_map<coding::ContextKey, std::vector<uint32_t>, coding::ContextKeyHash> groups;

    io::BitWriter raw_bits; // bucket方式の仮数ビット（生ビット列）

    // --- 4. セグメント単位の変換・符号化 ---
    for (uint32_t c = 0; c < num_channels; ++c) {
        for (size_t s = 0; s < segments.size(); ++s) {
            const Segment& seg = segments[s];
            std::span<const Sample> orig(coding_buf.channels[c].data() + seg.offset, seg.length);
            recon_segments[c][s].assign(orig.begin(), orig.end());

            SampleVec wavelet_input;
            auto ref_it = ref_map.find(static_cast<uint32_t>(s));
            bool has_ref = (ref_it != ref_map.end());

            if (has_ref) {
                // Block Graph 差分: 同長の参照セグメントとの差分を変換対象とする
                uint32_t ref_id = ref_it->second;
                const SampleVec& ref_samples = recon_segments[c][ref_id];
                wavelet_input.resize(seg.length);
                for (uint32_t i = 0; i < seg.length; ++i) {
                    wavelet_input[i] = orig[i] - ref_samples[i];
                }
            } else if (cfg.enable_harmonic) {
                // Harmonic Modeling: 倍音モデルとの差分を変換対象とする
                auto model = harmonic.analyze(orig, buf.spec.sample_rate);

                // ファイルには f32 として保存されるため、residual計算前に
                // f32 精度へ量子化しておく（decode側の再合成と完全一致させるため）
                model.fundamental_freq = static_cast<double>(static_cast<float>(model.fundamental_freq));
                for (double& a : model.amplitudes) a = static_cast<double>(static_cast<float>(a));
                for (double& p : model.phases)     p = static_cast<double>(static_cast<float>(p));

                harmonic_models[c][s] = model;
                wavelet_input = harmonic.residual(orig, model);
            } else {
                wavelet_input.assign(orig.begin(), orig.end());
            }

            // --- 適応整数予測 ---
            // 既存のモデル差分/グラフ差分に重ねて、次数0〜4の固定予測器を
            // 実測コストで比較する。予測が不利なら次数0（恒等変換）を選ぶ。
            const uint8_t max_order = cfg.profile == EncoderProfile::Ultra
                ? std::max<uint8_t>(cfg.max_predictor_order, 16)
                : std::min<uint8_t>(cfg.max_predictor_order, 4);
            auto prediction = choose_predictor_by_payload_cost(
                wavelet_input, wavelet, cfg, cfg.enable_predictive_coding && integer_pcm, max_order);
            meta[c][s].prediction_order = prediction.order;
            meta[c][s].prediction_seeds = std::move(prediction.seeds);
            wavelet_input = std::move(prediction.residual);

            // --- ウェーブレット変換 ---
            WaveletCoeffs coeffs;
            if (cfg.enable_wavelet) {
                coeffs = wavelet.forward(wavelet_input);
            } else {
                coeffs.levels = 0;
                coeffs.subbands.push_back(std::move(wavelet_input));
            }

            meta[c][s].levels_done = coeffs.levels;
            meta[c][s].subbands.resize(coeffs.subbands.size());

            for (size_t sb = 0; sb < coeffs.subbands.size(); ++sb) {
                uint8_t subband_level = static_cast<uint8_t>(sb);

                // --- Residual Island Coding ---
                auto island = coding::ResidualIslandCoder::encode(coeffs.subbands[sb]);
                meta[c][s].subbands[sb].num_values = static_cast<uint32_t>(island.values.size());

                // --- ゼロ連続長 (zero_runs) ---
                {
                    auto key = detail::make_ctx(static_cast<uint8_t>(c), subband_level, detail::STREAM_RUNS);
                    auto& vec = groups[key];
                    for (uint32_t run : island.zero_runs) {
                        uint32_t bucket = detail::bit_length_u64(run);
                        vec.push_back(bucket);
                        if (bucket > 0) {
                            uint32_t mbits = bucket - 1;
                            if (mbits > 0) {
                                uint64_t mantissa = static_cast<uint64_t>(run) & ((1ull << mbits) - 1);
                                raw_bits.write_bits(mantissa, mbits);
                            }
                        }
                    }
                }

                // --- 非ゼロ値 (Bitplane または Bucket) ---
                if (cfg.enable_bitplane) {
                    auto bp = coding::BitplaneDecomposer::decompose(island.values);
                    meta[c][s].subbands[sb].num_planes = bp.num_planes;

                    for (uint32_t p = 0; p < bp.num_planes; ++p) {
                        uint8_t plane_ctx = detail::clamp_plane(p);
                        auto key = detail::make_ctx(static_cast<uint8_t>(c), subband_level,
                                                     detail::STREAM_PLANE, plane_ctx);
                        auto& vec = groups[key];
                        for (uint8_t bit : bp.planes[p]) vec.push_back(static_cast<uint32_t>(bit));
                    }
                } else {
                    meta[c][s].subbands[sb].num_planes = 0;
                    auto key = detail::make_ctx(static_cast<uint8_t>(c), subband_level, detail::STREAM_VALUE);
                    auto& vec = groups[key];
                    for (Sample v : island.values) {
                        uint64_t zz = coding::BitplaneDecomposer::zigzag_encode(v);
                        uint32_t bucket = detail::bit_length_u64(zz);
                        vec.push_back(bucket);
                        if (bucket > 0) {
                            uint32_t mbits = bucket - 1;
                            if (mbits > 0) {
                                uint64_t mantissa = zz & ((1ull << mbits) - 1);
                                raw_bits.write_bits(mantissa, mbits);
                            }
                        }
                    }
                }
            }
        }
        report(impl_->progress, 0.1f + 0.6f * static_cast<float>(c + 1) / static_cast<float>(num_channels),
               "transform");
    }

    // --- 5. ファイルヘッダの書き込み ---
    report(impl_->progress, 0.75f, "entropy-coding");
    io::BitWriter w;
    io::write_file_magic(w, cfg.profile == EncoderProfile::Ultra ? 3 : 2);
    io::write_audio_spec(w, buf.spec);

    io::DecodeFlags flags;
    flags.wavelet_levels     = static_cast<uint8_t>(cfg.wavelet_levels);
    flags.enable_wavelet     = cfg.enable_wavelet;
    flags.enable_block_graph = cfg.enable_block_graph;
    flags.enable_harmonic    = cfg.enable_harmonic;
    flags.enable_bitplane    = cfg.enable_bitplane;
    flags.max_harmonics      = static_cast<uint8_t>(cfg.max_harmonics);
    flags.stereo_mid_side    = use_mid_side;
    io::write_decode_flags(w, flags);

    // セグメント長一覧
    w.write_uvarint(segments.size());
    for (const auto& seg : segments) w.write_uvarint(seg.length);

    // Block Graph エッジ
    if (cfg.enable_block_graph) {
        w.write_uvarint(graph.edges.size());
        for (const auto& e : graph.edges) {
            w.write_uvarint(e.tgt_id);
            w.write_uvarint(e.ref_id);
        }
    }

    // チャンネル毎・セグメント毎のメタデータ
    for (uint32_t c = 0; c < num_channels; ++c) {
        for (size_t s = 0; s < segments.size(); ++s) {
            const auto& sm = meta[c][s];
            w.write_uvarint(sm.levels_done);
            w.write_uvarint(sm.subbands.size());
            for (const auto& sb : sm.subbands) {
                w.write_uvarint(sb.num_values);
                if (cfg.enable_bitplane) w.write_uvarint(sb.num_planes);
            }

            if (cfg.enable_harmonic) {
                bool has_ref = ref_map.count(static_cast<uint32_t>(s)) > 0;
                if (!has_ref) {
                    const auto& m = harmonic_models[c][s];
                    w.write_f32(static_cast<float>(m.fundamental_freq));
                    if (m.fundamental_freq > 0.0) {
                        for (double a : m.amplitudes) w.write_f32(static_cast<float>(a));
                        for (double p : m.phases)     w.write_f32(static_cast<float>(p));
                    }
                }
            }

            // Version 0.2: ブロックごとの適応予測次数と初期履歴サンプル
            w.write_byte(sm.prediction_order);
            for (Sample seed : sm.prediction_seeds) {
                w.write_uvarint(detail::zigzag_u64(seed));
            }
        }
    }

    // --- 6. コンテキストグループ（確率テーブル + rANS符号化データ）---
    w.write_uvarint(groups.size());
    coding::RansEncoder rans_enc;
    for (const auto& kv : groups) {
        const coding::ContextKey&    key     = kv.first;
        const std::vector<uint32_t>& symbols = kv.second;

        w.write_byte(static_cast<uint8_t>(key.prev_residual_0)); // stream_type
        w.write_byte(key.bitplane_idx);                           // plane_idx (該当時)
        w.write_byte(key.wavelet_level);                          // subband level
        w.write_byte(key.channel);                                // channel

        w.write_uvarint(symbols.size());
        if (symbols.empty()) continue;

        uint32_t used_alphabet = 0;
        for (uint32_t sym : symbols) used_alphabet = std::max(used_alphabet, sym + 1);

        std::vector<uint32_t> counts(used_alphabet, 0);
        for (uint32_t sym : symbols) counts[sym]++;

        w.write_uvarint(used_alphabet);
        for (uint32_t cnt : counts) w.write_uvarint(cnt);

        auto table   = coding::build_prob_table(counts, used_alphabet);
        auto encoded = rans_enc.encode(symbols, table);

        w.write_uvarint(encoded.size());
        w.write_bytes(encoded);
    }

    // --- 7. 生ビット列（仮数ビット）---
    auto raw_bytes = raw_bits.finish();
    w.write_uvarint(raw_bytes.size());
    w.write_bytes(raw_bytes);

    // --- 8. ファイル出力 ---
    report(impl_->progress, 0.95f, "writing-file");
    auto final_bytes = w.finish();

    std::ofstream f(output_path, std::ios::binary);
    if (!f) return Error::IOError;
    f.write(reinterpret_cast<const char*>(final_bytes.data()),
            static_cast<std::streamsize>(final_bytes.size()));

    report(impl_->progress, 1.0f, "done");
    return f.good() ? Error::OK : Error::IOError;
}

} // namespace ulacr
