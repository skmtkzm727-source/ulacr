#include "ulacr/encoder.hpp"
#include "ulacr/decoder.hpp"
#include "test_common.hpp"
#include "../src/pipeline_common.hpp"
#include <cmath>
#include <random>
#include <iostream>
#include <filesystem>
#include <bit>

using namespace ulacr;

namespace {

AudioBuffer make_test_buffer(uint32_t num_channels, uint64_t num_samples,
                             uint32_t sample_rate, SampleFormat fmt) {
    AudioBuffer buf;
    buf.spec.sample_rate   = sample_rate;
    buf.spec.num_channels  = num_channels;
    buf.spec.num_samples   = num_samples;
    buf.spec.format        = fmt;
    buf.spec.source_format = SourceFormat::WAV;

    buf.channels.assign(num_channels, SampleVec(num_samples));

    std::mt19937 rng(123);
    std::uniform_int_distribution<int> noise(-50, 50);

    int64_t max_val = (1ll << (buf.spec.bit_depth() - 1)) - 1;
    if (buf.spec.bit_depth() >= 32) max_val = 1000000; // 浮動小数点/32bitは小さめの値域でテスト

    for (uint32_t c = 0; c < num_channels; ++c) {
        for (uint64_t i = 0; i < num_samples; ++i) {
            double t = static_cast<double>(i) / sample_rate;
            double freq = 220.0 * (c + 1);
            double sine = std::sin(2.0 * M_PI * freq * t);
            int64_t v = static_cast<int64_t>(sine * (max_val * 0.5)) + noise(rng);
            // 一部を完全な無音にしてResidual Islandを活用させる
            if ((i / 500) % 3 == 0) v = 0;
            buf.channels[c][i] = static_cast<Sample>(v);
        }
    }

    return buf;
}

bool buffers_equal(const AudioBuffer& a, const AudioBuffer& b) {
    if (a.spec.num_channels != b.spec.num_channels) return false;
    if (a.spec.num_samples  != b.spec.num_samples)  return false;
    for (uint32_t c = 0; c < a.spec.num_channels; ++c) {
        if (a.channels[c].size() != b.channels[c].size()) return false;
        for (size_t i = 0; i < a.channels[c].size(); ++i) {
            if (a.channels[c][i] != b.channels[c][i]) {
                std::cerr << "Mismatch at channel " << c << " sample " << i
                          << ": " << a.channels[c][i] << " != " << b.channels[c][i] << "\n";
                return false;
            }
        }
    }
    return true;
}

uint64_t run_roundtrip(const CodecConfig& cfg, const AudioBuffer& buf, const std::string& name) {
    std::filesystem::path tmp = std::filesystem::temp_directory_path() / (name + ".ulacr");

    Encoder enc(cfg);
    Error err = enc.encode(buf, tmp);
    ULACR_CHECK(err == Error::OK);

    Decoder dec;
    AudioBuffer decoded;
    err = dec.decode(tmp, decoded);
    ULACR_CHECK(err == Error::OK);

    ULACR_CHECK(buffers_equal(buf, decoded));

    auto orig_size = static_cast<uint64_t>(buf.spec.num_samples) * buf.spec.num_channels * buf.spec.bytes_per_sample();
    auto comp_size = std::filesystem::file_size(tmp);
    std::cout << "  [" << name << "] " << orig_size << " -> " << comp_size
              << " bytes (" << (100.0 * static_cast<double>(comp_size) / static_cast<double>(orig_size)) << "%)\n";

    std::filesystem::remove(tmp);
    return comp_size;
}

} // namespace

int main() {
    try {
        auto buf_mono   = make_test_buffer(1, 8000, 44100, SampleFormat::Int16);
        auto buf_stereo = make_test_buffer(2, 8000, 44100, SampleFormat::Int16);
        auto buf_24bit  = make_test_buffer(2, 4000, 48000, SampleFormat::Int24);

        // Mid/Side + adaptive predictor: strongly correlated stereo and smooth integer ramp.
        AudioBuffer buf_correlated;
        buf_correlated.spec.sample_rate = 48000;
        buf_correlated.spec.num_channels = 2;
        buf_correlated.spec.num_samples = 24000;
        buf_correlated.spec.format = SampleFormat::Int16;
        buf_correlated.spec.source_format = SourceFormat::WAV;
        buf_correlated.channels.assign(2, SampleVec(buf_correlated.spec.num_samples));
        for (uint64_t i = 0; i < buf_correlated.spec.num_samples; ++i) {
            Sample base = static_cast<Sample>((i * 7u) % 20000u) - 10000;
            buf_correlated.channels[0][i] = base;
            buf_correlated.channels[1][i] = base - static_cast<Sample>(i % 7u);
        }

        // 新方式の圧縮率を同一音源で旧方式と比較。完全一致も双方で検証する。
        {
            CodecConfig baseline = CodecConfig::fast();
            baseline.enable_block_graph = false;
            baseline.enable_harmonic = false;
            baseline.enable_predictive_coding = false;
            baseline.enable_stereo_mid_side = false;

            CodecConfig advanced = baseline;
            advanced.enable_predictive_coding = true;
            advanced.enable_stereo_mid_side = true;

            const uint64_t baseline_size =
                run_roundtrip(baseline, buf_correlated, "stereo_correlated_baseline");
            const uint64_t advanced_size =
                run_roundtrip(advanced, buf_correlated, "stereo_correlated_advanced");
            ULACR_CHECK(advanced_size <= baseline_size);
            std::cout << "  [adaptive prediction + Mid/Side] "
                      << baseline_size << " -> " << advanced_size << " bytes\n";
        }

        // NLMS encoder/decoder state synchronization is bit-exact.
        {
            SampleVec source(4096);
            Sample value = 12000;
            for (size_t i = 0; i < source.size(); ++i) {
                value += static_cast<Sample>((i * 17u) % 31u) - 15;
                source[i] = value;
            }
            const auto prediction = detail::make_adaptive_nlms_prediction(source, 8);
            const auto restored = detail::inverse_predictor(
                prediction.residual, prediction.order, prediction.seeds, 8);
            ULACR_CHECK(prediction.order == 17);
            ULACR_CHECK(restored == source);
        }

        // ULAC-R 2 v0.3: integer predictor up to order 16 and IEEE bit-pattern tests.
        {
            CodecConfig ultra = CodecConfig::fast();
            ultra.profile = EncoderProfile::Ultra;
            ultra.enable_block_graph = false;
            ultra.enable_harmonic = false;
            ultra.max_predictor_order = 16;
            run_roundtrip(ultra, buf_correlated, "ultra_v03_integer");

            AudioBuffer scaled = buf_correlated;
            scaled.spec.format = SampleFormat::Int24;
            for (auto& channel : scaled.channels)
                for (Sample& value : channel) value *= 64;
            run_roundtrip(ultra, scaled, "ultra_a1_common_factor");

            AudioBuffer sparse;
            sparse.spec.sample_rate = 48000;
            sparse.spec.num_channels = 1;
            sparse.spec.num_samples = 12000;
            sparse.spec.format = SampleFormat::Int24;
            sparse.channels.assign(1, SampleVec(sparse.spec.num_samples));
            for (uint64_t i = 0; i < sparse.spec.num_samples; ++i) {
                const Sample level = static_cast<Sample>((i / 37u) % 5u) - 2;
                sparse.channels[0][i] = level * 257;
            }
            CodecConfig sparse_cfg = ultra;
            sparse_cfg.enable_wavelet = false;
            sparse_cfg.enable_bitplane = true;
            run_roundtrip(sparse_cfg, sparse, "ultra_a2_sparse_alphabet");

            AudioBuffer f32;
            f32.spec.sample_rate = 48000;
            f32.spec.num_channels = 1;
            f32.spec.num_samples = 8;
            f32.spec.format = SampleFormat::Float32;
            f32.channels.assign(1, SampleVec(8));
            const uint32_t f32_bits[] = {0x00000000u, 0x80000000u, 0x3f800000u,
                0xbf800000u, 0x7fc12345u, 0x00000001u, 0x7f800000u, 0xff800000u};
            for (size_t i = 0; i < 8; ++i)
                f32.channels[0][i] = static_cast<Sample>(std::bit_cast<int32_t>(f32_bits[i]));
            run_roundtrip(ultra, f32, "ultra_float32_bitpatterns");

            AudioBuffer f64;
            f64.spec.sample_rate = 48000;
            f64.spec.num_channels = 1;
            f64.spec.num_samples = 8;
            f64.spec.format = SampleFormat::Float64;
            f64.channels.assign(1, SampleVec(8));
            const uint64_t f64_bits[] = {0x0000000000000000ull, 0x8000000000000000ull,
                0x3ff0000000000000ull, 0xbff0000000000000ull, 0x7ff8123456789abcull,
                0x0000000000000001ull, 0x7ff0000000000000ull, 0xfff0000000000000ull};
            for (size_t i = 0; i < 8; ++i)
                f64.channels[0][i] = std::bit_cast<Sample>(f64_bits[i]);
            run_roundtrip(ultra, f64, "ultra_float64_bitpatterns");
        }

        // Auto is selected by complete serialized byte count, never estimated cost.
        {
            CodecConfig classic = CodecConfig::fast();
            classic.enable_block_graph = false;
            classic.enable_harmonic = false;
            const uint64_t classic_size = run_roundtrip(classic, buf_correlated, "auto_reference_classic");
            CodecConfig automatic = classic;
            automatic.profile = EncoderProfile::Auto;
            const uint64_t auto_size = run_roundtrip(automatic, buf_correlated, "auto_actual_size");
            ULACR_CHECK(auto_size <= classic_size);
        }

        // --- 基本構成 (Wavelet + Residual Island + Bucket) ---
        {
            CodecConfig cfg = CodecConfig::fast();
            cfg.enable_block_graph = false;
            cfg.enable_harmonic    = false;
            cfg.enable_bitplane    = false;
            run_roundtrip(cfg, buf_mono,   "mono_basic");
            run_roundtrip(cfg, buf_stereo, "stereo_basic");
            run_roundtrip(cfg, buf_24bit,  "24bit_basic");
            run_roundtrip(cfg, buf_correlated, "stereo_correlated_predictive");
        }

        // --- Bitplane 有効 ---
        {
            CodecConfig cfg = CodecConfig::fast();
            cfg.enable_block_graph = false;
            cfg.enable_harmonic    = false;
            cfg.enable_bitplane    = true;
            run_roundtrip(cfg, buf_stereo, "stereo_bitplane");
        }

        // --- Block Graph 有効 ---
        {
            CodecConfig cfg = CodecConfig::fast();
            cfg.min_block_size     = 256;
            cfg.max_block_size     = 1024;
            cfg.enable_block_graph = true;
            cfg.enable_harmonic    = false;
            cfg.enable_bitplane    = false;
            run_roundtrip(cfg, buf_stereo, "stereo_blockgraph");
        }

        // --- Harmonic Modeling 有効 ---
        {
            CodecConfig cfg = CodecConfig::fast();
            cfg.enable_block_graph = false;
            cfg.enable_harmonic    = true;
            cfg.max_harmonics      = 4;
            cfg.enable_bitplane    = false;
            run_roundtrip(cfg, buf_mono, "mono_harmonic");
        }

        // --- フルオプション (best_compression) ---
        {
            CodecConfig cfg = CodecConfig::best_compression();
            cfg.min_block_size = 256;
            cfg.max_block_size = 2048;
            cfg.max_harmonics  = 4;
            run_roundtrip(cfg, buf_stereo, "stereo_full");
        }

        // --- ウェーブレット無効 ---
        {
            CodecConfig cfg = CodecConfig::fast();
            cfg.enable_wavelet     = false;
            cfg.enable_block_graph = false;
            cfg.enable_harmonic    = false;
            cfg.enable_bitplane    = false;
            run_roundtrip(cfg, buf_mono, "mono_no_wavelet");
        }

    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    std::cout << "test_roundtrip OK\n";
    return 0;
}
