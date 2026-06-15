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

    const CodecConfig& cfg = impl_->cfg;
    const uint32_t num_channels = buf.spec.num_channels;

    // --- 1. 適応的セグメンテーション ---
    report(impl_->progress, 0.0f, "segmentation");
    Segmenter segmenter(cfg);
    std::vector<Segment> segments = segmenter.segment(buf);
    if (segments.empty()) return Error::InvalidArg;

    // --- 2. Block Graph Compression ---
    report(impl_->progress, 0.05f, "block-graph");
    BlockGraphBuilder graph_builder(cfg);
    BlockGraph graph;
    if (cfg.enable_block_graph) {
        graph = graph_builder.build(buf, segments);
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
            std::span<const Sample> orig(buf.channels[c].data() + seg.offset, seg.length);
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
    io::write_file_magic(w);
    io::write_audio_spec(w, buf.spec);

    io::DecodeFlags flags;
    flags.wavelet_levels     = static_cast<uint8_t>(cfg.wavelet_levels);
    flags.enable_wavelet     = cfg.enable_wavelet;
    flags.enable_block_graph = cfg.enable_block_graph;
    flags.enable_harmonic    = cfg.enable_harmonic;
    flags.enable_bitplane    = cfg.enable_bitplane;
    flags.max_harmonics      = static_cast<uint8_t>(cfg.max_harmonics);
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
