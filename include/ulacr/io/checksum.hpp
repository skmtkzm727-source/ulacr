#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace ulacr::io {

// IEEE CRC-32 (polynomial 0xEDB88320), serialized little-endian.
// Used as a trailer in ULAC-R 0.3 streams.
inline uint32_t crc32_ieee(std::span<const uint8_t> bytes) noexcept {
    uint32_t crc = 0xffffffffu;
    for (uint8_t byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

inline void append_crc32(std::vector<uint8_t>& bytes) {
    const uint32_t crc = crc32_ieee(bytes);
    for (unsigned i = 0; i < 4; ++i)
        bytes.push_back(static_cast<uint8_t>((crc >> (i * 8)) & 0xffu));
}

inline bool verify_and_strip_crc32(std::vector<uint8_t>& bytes) {
    if (bytes.size() < 4) return false;
    const size_t payload_size = bytes.size() - 4;
    const uint32_t stored =
        static_cast<uint32_t>(bytes[payload_size]) |
        (static_cast<uint32_t>(bytes[payload_size + 1]) << 8) |
        (static_cast<uint32_t>(bytes[payload_size + 2]) << 16) |
        (static_cast<uint32_t>(bytes[payload_size + 3]) << 24);
    if (crc32_ieee(std::span<const uint8_t>(bytes.data(), payload_size)) != stored)
        return false;
    bytes.resize(payload_size);
    return true;
}

} // namespace ulacr::io
