#include "ulacr/transform/wavelet.hpp"
#include "test_common.hpp"
#include <random>
#include <iostream>

using namespace ulacr;

static void test_roundtrip_even() {
    CodecConfig cfg = CodecConfig::best_compression();
    cfg.wavelet_levels = 4;
    WaveletTransform wt(cfg);

    std::mt19937 rng(7);
    std::uniform_int_distribution<int64_t> dist(-30000, 30000);

    SampleVec samples(1024);
    for (auto& s : samples) s = dist(rng);

    auto coeffs = wt.forward(samples);
    auto recon = wt.inverse(coeffs);

    ULACR_CHECK_EQ(recon.size(), samples.size());
    for (size_t i = 0; i < samples.size(); ++i) ULACR_CHECK_EQ(recon[i], samples[i]);
}

static void test_roundtrip_odd() {
    CodecConfig cfg = CodecConfig::best_compression();
    cfg.wavelet_levels = 3;
    WaveletTransform wt(cfg);

    std::mt19937 rng(13);
    std::uniform_int_distribution<int64_t> dist(-1000000, 1000000);

    // 奇数長 & 様々なサイズでテスト
    for (size_t n : {1u, 2u, 3u, 5u, 7u, 17u, 99u, 257u, 1000u, 1001u}) {
        SampleVec samples(n);
        for (auto& s : samples) s = dist(rng);

        auto coeffs = wt.forward(samples);
        auto recon = wt.inverse(coeffs);

        ULACR_CHECK_EQ(recon.size(), samples.size());
        for (size_t i = 0; i < samples.size(); ++i) {
            ULACR_CHECK_EQ(recon[i], samples[i]);
        }
    }
}

static void test_constant_signal() {
    CodecConfig cfg = CodecConfig::best_compression();
    cfg.wavelet_levels = 5;
    WaveletTransform wt(cfg);

    SampleVec samples(512, 12345);
    auto coeffs = wt.forward(samples);

    // 定数信号: 全ての高域成分が 0 になるはず
    for (size_t i = 1; i < coeffs.subbands.size(); ++i) {
        for (Sample v : coeffs.subbands[i]) {
            ULACR_CHECK_EQ(v, 0);
        }
    }

    auto recon = wt.inverse(coeffs);
    for (size_t i = 0; i < samples.size(); ++i) ULACR_CHECK_EQ(recon[i], samples[i]);
}

int main() {
    try {
        test_roundtrip_even();
        test_roundtrip_odd();
        test_constant_signal();
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    std::cout << "test_wavelet OK\n";
    return 0;
}
