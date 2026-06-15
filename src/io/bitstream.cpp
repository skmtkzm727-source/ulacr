#include "ulacr/io/bitstream.hpp"
#include <cstring>

namespace ulacr::io {

// =================================================================
// BitWriter
// =================================================================

void BitWriter::push_bit(uint32_t bit) noexcept {
    cur_byte_ = static_cast<uint8_t>((cur_byte_ << 1) | (bit & 1u));
    bit_pos_++;
    if (bit_pos_ == 8) {
        buffer_.push_back(cur_byte_);
        cur_byte_ = 0;
        bit_pos_  = 0;
    }
}

void BitWriter::write_bits(uint64_t value, uint32_t num_bits) {
    // MSB-first
    for (uint32_t i = 0; i < num_bits; ++i) {
        uint32_t shift = num_bits - 1 - i;
        push_bit(static_cast<uint32_t>((value >> shift) & 1u));
    }
}

void BitWriter::align_to_byte() {
    if (bit_pos_ != 0) {
        uint32_t pad = 8 - bit_pos_;
        write_bits(0, pad);
    }
}

void BitWriter::write_byte(uint8_t b) {
    align_to_byte();
    buffer_.push_back(b);
}

void BitWriter::write_u16(uint16_t v) {
    write_byte(static_cast<uint8_t>(v & 0xFF));
    write_byte(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void BitWriter::write_u32(uint32_t v) {
    for (int i = 0; i < 4; ++i)
        write_byte(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

void BitWriter::write_u64(uint64_t v) {
    for (int i = 0; i < 8; ++i)
        write_byte(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

void BitWriter::write_i32(int32_t v) { write_u32(static_cast<uint32_t>(v)); }
void BitWriter::write_i64(int64_t v) { write_u64(static_cast<uint64_t>(v)); }

void BitWriter::write_f32(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    write_u32(bits);
}

void BitWriter::write_uvarint(uint64_t v) {
    align_to_byte();
    while (true) {
        uint8_t byte = static_cast<uint8_t>(v & 0x7F);
        v >>= 7;
        if (v != 0) {
            buffer_.push_back(static_cast<uint8_t>(byte | 0x80));
        } else {
            buffer_.push_back(byte);
            break;
        }
    }
}

void BitWriter::write_svarint(int64_t v) {
    // zigzag encoding
    uint64_t zz = (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
    write_uvarint(zz);
}

void BitWriter::write_bytes(std::span<const uint8_t> data) {
    align_to_byte();
    buffer_.insert(buffer_.end(), data.begin(), data.end());
}

void BitWriter::write_string(const std::string& s) {
    write_uvarint(s.size());
    write_bytes(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(s.data()), s.size()));
}

std::vector<uint8_t> BitWriter::finish() {
    align_to_byte();
    return buffer_;
}

// =================================================================
// BitReader
// =================================================================

uint32_t BitReader::pop_bit() {
    if (byte_pos_ >= data_.size()) {
        throw BitstreamError("BitReader: unexpected end of data");
    }
    uint8_t byte = data_[byte_pos_];
    uint32_t bit = (byte >> (7 - bit_pos_)) & 1u;
    bit_pos_++;
    if (bit_pos_ == 8) {
        bit_pos_ = 0;
        byte_pos_++;
    }
    return bit;
}

uint64_t BitReader::read_bits(uint32_t num_bits) {
    uint64_t result = 0;
    for (uint32_t i = 0; i < num_bits; ++i) {
        result = (result << 1) | pop_bit();
    }
    return result;
}

void BitReader::align_to_byte() noexcept {
    if (bit_pos_ != 0) {
        bit_pos_ = 0;
        byte_pos_++;
    }
}

uint8_t BitReader::read_byte() {
    align_to_byte();
    if (byte_pos_ >= data_.size()) {
        throw BitstreamError("BitReader: read_byte past end");
    }
    return data_[byte_pos_++];
}

uint16_t BitReader::read_u16() {
    uint16_t v = 0;
    v |= static_cast<uint16_t>(read_byte());
    v |= static_cast<uint16_t>(read_byte()) << 8;
    return v;
}

uint32_t BitReader::read_u32() {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= static_cast<uint32_t>(read_byte()) << (8 * i);
    return v;
}

uint64_t BitReader::read_u64() {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(read_byte()) << (8 * i);
    return v;
}

int32_t BitReader::read_i32() { return static_cast<int32_t>(read_u32()); }
int64_t BitReader::read_i64() { return static_cast<int64_t>(read_u64()); }

float BitReader::read_f32() {
    uint32_t bits = read_u32();
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

uint64_t BitReader::read_uvarint() {
    align_to_byte();
    uint64_t result = 0;
    uint32_t shift  = 0;
    while (true) {
        uint8_t byte = read_byte();
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) break;
        shift += 7;
        if (shift >= 64) throw BitstreamError("uvarint too long");
    }
    return result;
}

int64_t BitReader::read_svarint() {
    uint64_t zz = read_uvarint();
    return static_cast<int64_t>((zz >> 1) ^ (~(zz & 1) + 1));
}

std::vector<uint8_t> BitReader::read_bytes(size_t n) {
    align_to_byte();
    if (byte_pos_ + n > data_.size()) {
        throw BitstreamError("read_bytes past end of data");
    }
    std::vector<uint8_t> out(data_.begin() + byte_pos_, data_.begin() + byte_pos_ + n);
    byte_pos_ += n;
    return out;
}

std::string BitReader::read_string() {
    uint64_t len = read_uvarint();
    auto bytes = read_bytes(static_cast<size_t>(len));
    return std::string(bytes.begin(), bytes.end());
}

} // namespace ulacr::io
