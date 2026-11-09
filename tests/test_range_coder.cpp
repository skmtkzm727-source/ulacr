#include "ulacr/coding/range_coder.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

int main() {
    try {
        ulacr::coding::AdaptiveRangeEncoder encoder;
        ulacr::coding::AdaptiveRangeDecoder decoder;
        ulacr::coding::AdaptiveMixtureRangeEncoder mixture_encoder;
        ulacr::coding::AdaptiveMixtureRangeDecoder mixture_decoder;
        std::mt19937 rng(0x554C4352u);
        for (uint32_t alphabet : {1u, 2u, 3u, 8u, 16u, 64u}) {
            for (uint32_t n : {0u, 1u, 2u, 17u, 4096u, 20000u}) {
                std::vector<uint32_t> source;
                source.reserve(n);
                for (uint32_t i = 0; i < n; ++i) {
                    const uint32_t value = (rng() % 5u == 0u)
                        ? (rng() % alphabet) : (rng() % std::min<uint32_t>(alphabet, 2u));
                    source.push_back(value);
                }
                const auto bytes = encoder.encode(source, alphabet);
                const auto restored = decoder.decode(bytes, alphabet, source.size());
                if (source != restored) {
                    std::cerr << "Range coder mismatch, alphabet=" << alphabet
                              << " count=" << n << "\n";
                    return 1;
                }
                const auto mixture_bytes = mixture_encoder.encode(source, alphabet);
                const auto mixture_restored = mixture_decoder.decode(
                    mixture_bytes, alphabet, source.size());
                if (source != mixture_restored) {
                    std::cerr << "Mixture coder mismatch, alphabet=" << alphabet
                              << " count=" << n << "\n";
                    return 1;
                }
            }
        }

        // Larger alphabets and long skewed runs exercise integer CDF
        // normalization, posterior weight updates and rescaling.
        for (uint32_t alphabet : {127u, 255u, 1024u}) {
            std::vector<uint32_t> source;
            source.reserve(2048);
            for (uint32_t i = 0; i < 2048; ++i) {
                const uint32_t value = (i % 97u == 0u)
                    ? (rng() % alphabet) : ((i / 31u) % std::min<uint32_t>(alphabet, 7u));
                source.push_back(value);
            }
            const auto bytes = mixture_encoder.encode(source, alphabet);
            const auto restored = mixture_decoder.decode(bytes, alphabet, source.size());
            if (source != restored) {
                std::cerr << "Mixture stress mismatch, alphabet=" << alphabet << "\\n";
                return 1;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    std::cout << "test_range_coder OK\n";
    return 0;
}
