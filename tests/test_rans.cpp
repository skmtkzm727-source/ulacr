#include "ulacr/coding/rans_coder.hpp"
#include "test_common.hpp"
#include <vector>
#include <random>
#include <iostream>

using namespace ulacr::coding;

static void test_uniform() {
    std::vector<uint32_t> symbols;
    std::mt19937 rng(1);
    std::uniform_int_distribution<uint32_t> dist(0, 9);
    for (int i = 0; i < 10000; ++i) symbols.push_back(dist(rng));

    std::vector<uint32_t> counts(10, 0);
    for (auto s : symbols) counts[s]++;

    auto table = build_prob_table(counts, 10);

    RansEncoder enc;
    auto encoded = enc.encode(symbols, table);

    RansDecoder dec;
    auto decoded = dec.decode(encoded, table, symbols.size());

    ULACR_CHECK_EQ(decoded.size(), symbols.size());
    for (size_t i = 0; i < symbols.size(); ++i) {
        ULACR_CHECK_EQ(decoded[i], symbols[i]);
    }
}

static void test_skewed() {
    // 非常に偏った分布（ほぼ0、時々大きな値）
    std::vector<uint32_t> symbols;
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (int i = 0; i < 5000; ++i) {
        if (u(rng) < 0.9) symbols.push_back(0);
        else symbols.push_back(static_cast<uint32_t>(1 + u(rng) * 30));
    }

    uint32_t alphabet = 32;
    std::vector<uint32_t> counts(alphabet, 0);
    for (auto s : symbols) counts[s]++;

    auto table = build_prob_table(counts, alphabet);

    RansEncoder enc;
    auto encoded = enc.encode(symbols, table);

    RansDecoder dec;
    auto decoded = dec.decode(encoded, table, symbols.size());

    ULACR_CHECK_EQ(decoded.size(), symbols.size());
    for (size_t i = 0; i < symbols.size(); ++i) {
        ULACR_CHECK_EQ(decoded[i], symbols[i]);
    }

    // 圧縮されていることを確認（理論上のエントロピーよりかなり小さいはず）
    ULACR_CHECK(encoded.size() < symbols.size());
}

static void test_single_symbol() {
    std::vector<uint32_t> symbols(100, 3);
    std::vector<uint32_t> counts(4, 0);
    counts[3] = 100;
    auto table = build_prob_table(counts, 4);

    RansEncoder enc;
    auto encoded = enc.encode(symbols, table);

    RansDecoder dec;
    auto decoded = dec.decode(encoded, table, symbols.size());

    for (size_t i = 0; i < symbols.size(); ++i) ULACR_CHECK_EQ(decoded[i], symbols[i]);
}

int main() {
    try {
        test_uniform();
        test_skewed();
        test_single_symbol();
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    std::cout << "test_rans OK\n";
    return 0;
}
