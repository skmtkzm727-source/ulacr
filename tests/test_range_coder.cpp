#include "ulacr/coding/range_coder.hpp"
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

int main() {
    try {
        ulacr::coding::AdaptiveRangeEncoder encoder;
        ulacr::coding::AdaptiveRangeDecoder decoder;
        std::mt19937 rng(0x554C4352u);
        for (uint32_t alphabet : {1u, 2u, 3u, 8u, 16u, 64u}) {
            for (uint32_t n : {0u, 1u, 2u, 17u, 4096u, 20000u}) {
                std::vector<uint32_t> source;
                source.reserve(n);
                for (uint32_t i = 0; i < n; ++i) {
                    // A skewed distribution stresses adaptation and rescaling.
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
            }
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    std::cout << "test_range_coder OK\n";
    return 0;
}
