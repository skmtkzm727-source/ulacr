#include "ulacr/io/checksum.hpp"
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    const std::vector<uint8_t> known{'1','2','3','4','5','6','7','8','9'};
    if (ulacr::io::crc32_ieee(known) != 0xcbf43926u) {
        std::cerr << "CRC32 known-vector mismatch\n";
        return 1;
    }

    std::vector<uint8_t> bytes{0x55, 0xaa, 0x01, 0x02, 0x03};
    const auto original = bytes;
    ulacr::io::append_crc32(bytes);
    if (!ulacr::io::verify_and_strip_crc32(bytes) || bytes != original) {
        std::cerr << "CRC32 append/verify mismatch\n";
        return 1;
    }

    bytes = original;
    ulacr::io::append_crc32(bytes);
    bytes[2] ^= 0x80u;
    if (ulacr::io::verify_and_strip_crc32(bytes)) {
        std::cerr << "CRC32 accepted corrupted payload\n";
        return 1;
    }

    bytes = {1, 2, 3};
    if (ulacr::io::verify_and_strip_crc32(bytes)) {
        std::cerr << "CRC32 accepted truncated trailer\n";
        return 1;
    }
    std::cout << "test_checksum OK\n";
    return 0;
}
