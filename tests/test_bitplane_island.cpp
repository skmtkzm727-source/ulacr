#include "ulacr/coding/bitplane.hpp"
#include "ulacr/coding/residual_island.hpp"
#include "test_common.hpp"
#include <random>
#include <iostream>

using namespace ulacr;
using namespace ulacr::coding;

static void test_zigzag() {
    std::mt19937_64 rng(1);
    std::uniform_int_distribution<int64_t> dist(-1000000, 1000000);
    for (int i = 0; i < 10000; ++i) {
        Sample v = dist(rng);
        uint64_t zz = BitplaneDecomposer::zigzag_encode(v);
        Sample back = BitplaneDecomposer::zigzag_decode(zz);
        ULACR_CHECK_EQ(back, v);
    }
    ULACR_CHECK_EQ(BitplaneDecomposer::zigzag_encode(0), 0u);
    ULACR_CHECK_EQ(BitplaneDecomposer::zigzag_encode(-1), 1u);
    ULACR_CHECK_EQ(BitplaneDecomposer::zigzag_encode(1), 2u);
}

static void test_bitplane_roundtrip() {
    std::mt19937_64 rng(2);
    std::uniform_int_distribution<int64_t> dist(-50000, 50000);

    SampleVec values(2000);
    for (auto& v : values) v = dist(rng);

    auto bp = BitplaneDecomposer::decompose(values);
    ULACR_CHECK(bp.num_planes > 0);
    ULACR_CHECK_EQ(bp.num_values, values.size());

    auto recon = BitplaneDecomposer::reconstruct(bp);
    ULACR_CHECK_EQ(recon.size(), values.size());
    for (size_t i = 0; i < values.size(); ++i) ULACR_CHECK_EQ(recon[i], values[i]);
}

static void test_bitplane_empty() {
    SampleVec values;
    auto bp = BitplaneDecomposer::decompose(values);
    ULACR_CHECK_EQ(bp.num_values, 0u);
    auto recon = BitplaneDecomposer::reconstruct(bp);
    ULACR_CHECK_EQ(recon.size(), 0u);
}

static void test_residual_island_roundtrip() {
    SampleVec residuals = {0,0,0,5,0,0,-3,0,0,0,0,7,0};
    auto island = ResidualIslandCoder::encode(residuals);

    ULACR_CHECK_EQ(island.values.size(), 3u);
    ULACR_CHECK_EQ(island.zero_runs.size(), 4u);
    ULACR_CHECK_EQ(island.total_length, residuals.size());

    auto recon = ResidualIslandCoder::decode(island);
    ULACR_CHECK_EQ(recon.size(), residuals.size());
    for (size_t i = 0; i < residuals.size(); ++i) ULACR_CHECK_EQ(recon[i], residuals[i]);
}

static void test_residual_island_all_zero() {
    SampleVec residuals(100, 0);
    auto island = ResidualIslandCoder::encode(residuals);
    ULACR_CHECK_EQ(island.values.size(), 0u);
    ULACR_CHECK_EQ(island.zero_runs.size(), 1u);
    ULACR_CHECK_EQ(island.zero_runs[0], 100u);

    auto recon = ResidualIslandCoder::decode(island);
    for (size_t i = 0; i < residuals.size(); ++i) ULACR_CHECK_EQ(recon[i], 0);
}

static void test_residual_island_no_zero() {
    SampleVec residuals = {1,2,3,4,5};
    auto island = ResidualIslandCoder::encode(residuals);
    ULACR_CHECK_EQ(island.values.size(), 5u);
    ULACR_CHECK_EQ(island.zero_runs.size(), 6u);
    for (auto r : island.zero_runs) ULACR_CHECK_EQ(r, 0u);

    auto recon = ResidualIslandCoder::decode(island);
    for (size_t i = 0; i < residuals.size(); ++i) ULACR_CHECK_EQ(recon[i], residuals[i]);
}

int main() {
    try {
        test_zigzag();
        test_bitplane_roundtrip();
        test_bitplane_empty();
        test_residual_island_roundtrip();
        test_residual_island_all_zero();
        test_residual_island_no_zero();
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    std::cout << "test_bitplane_island OK\n";
    return 0;
}
