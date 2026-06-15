#include "ulacr/coding/bitplane.hpp"
#include <algorithm>
#include <bit>

namespace ulacr::coding {

uint64_t BitplaneDecomposer::zigzag_encode(Sample v) noexcept {
    return (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
}

Sample BitplaneDecomposer::zigzag_decode(uint64_t z) noexcept {
    return static_cast<Sample>((z >> 1) ^ (~(z & 1) + 1));
}

uint32_t BitplaneDecomposer::bit_length(uint64_t v) noexcept {
    if (v == 0) return 0;
    return 64 - static_cast<uint32_t>(std::countl_zero(v));
}

BitplaneData BitplaneDecomposer::decompose(std::span<const Sample> values) {
    BitplaneData out;
    out.num_values = static_cast<uint32_t>(values.size());

    std::vector<uint64_t> zz(values.size());
    uint64_t max_val = 0;
    for (size_t i = 0; i < values.size(); ++i) {
        zz[i] = zigzag_encode(values[i]);
        max_val = std::max(max_val, zz[i]);
    }

    uint32_t num_planes = std::max<uint32_t>(1, bit_length(max_val));
    out.num_planes = num_planes;
    out.planes.assign(num_planes, std::vector<uint8_t>(values.size(), 0));

    for (size_t i = 0; i < zz.size(); ++i) {
        for (uint32_t p = 0; p < num_planes; ++p) {
            // plane 0 = MSB, plane (num_planes-1) = LSB
            uint32_t shift = num_planes - 1 - p;
            out.planes[p][i] = static_cast<uint8_t>((zz[i] >> shift) & 1u);
        }
    }

    return out;
}

SampleVec BitplaneDecomposer::reconstruct(const BitplaneData& bp) {
    SampleVec out(bp.num_values);

    for (uint32_t i = 0; i < bp.num_values; ++i) {
        uint64_t zz = 0;
        for (uint32_t p = 0; p < bp.num_planes; ++p) {
            uint32_t shift = bp.num_planes - 1 - p;
            zz |= static_cast<uint64_t>(bp.planes[p][i] & 1u) << shift;
        }
        out[i] = zigzag_decode(zz);
    }

    return out;
}

} // namespace ulacr::coding
