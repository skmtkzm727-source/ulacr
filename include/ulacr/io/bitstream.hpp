#pragma once
#include <cstdint>
#include <vector>
#include <span>
#include <string>
#include <stdexcept>

namespace ulacr::io {

// ---------------------------------------------------------------
// BitWriter
//
// バイト境界に従わないビット単位の書き込みをサポートする。
// マルチバイト整数は明示的にリトルエンディアンで書き込まれる。
// ---------------------------------------------------------------
class BitWriter {
public:
    BitWriter() = default;

    /// 任意ビット数（1〜64）の符号なし整数を書き込む（MSB-first within value）
    void write_bits(uint64_t value, uint32_t num_bits);

    /// バイト単位の書き込み（境界に揃える）
    void write_byte(uint8_t b);
    void write_u16(uint16_t v);
    void write_u32(uint32_t v);
    void write_u64(uint64_t v);
    void write_i32(int32_t v);
    void write_i64(int64_t v);
    void write_f32(float v);

    /// 可変長整数（unsigned LEB128）
    void write_uvarint(uint64_t v);
    /// 可変長整数（zigzag + LEB128, 符号付き）
    void write_svarint(int64_t v);

    /// 生バイト列をそのまま書き込む
    void write_bytes(std::span<const uint8_t> data);
    /// 文字列を [uvarint length][bytes] 形式で書き込む
    void write_string(const std::string& s);

    /// 現在のビット位置を 0 にパディングしてバイト境界に揃える
    void align_to_byte();

    /// 書き込み済みバッファを返す（自動的にバイト境界に揃える）
    std::vector<uint8_t> finish();

    size_t bit_size() const noexcept { return buffer_.size() * 8 + bit_pos_; }
    size_t byte_size() const noexcept { return buffer_.size() + (bit_pos_ > 0 ? 1 : 0); }

private:
    std::vector<uint8_t> buffer_;
    uint8_t  cur_byte_ = 0;
    uint32_t bit_pos_  = 0; // 0..7, 現在の cur_byte_ 内の使用ビット数

    void push_bit(uint32_t bit) noexcept;
};

// ---------------------------------------------------------------
// BitReader
// ---------------------------------------------------------------
class BitReader {
public:
    explicit BitReader(std::span<const uint8_t> data) noexcept
        : data_(data) {}

    uint64_t read_bits(uint32_t num_bits);

    uint8_t  read_byte();
    uint16_t read_u16();
    uint32_t read_u32();
    uint64_t read_u64();
    int32_t  read_i32();
    int64_t  read_i64();
    float    read_f32();

    uint64_t read_uvarint();
    int64_t  read_svarint();

    std::vector<uint8_t> read_bytes(size_t n);
    std::string read_string();

    void align_to_byte() noexcept;

    bool eof() const noexcept { return byte_pos_ >= data_.size() && bit_pos_ == 0; }
    size_t bytes_consumed() const noexcept { return byte_pos_ + (bit_pos_ > 0 ? 1 : 0); }

private:
    std::span<const uint8_t> data_;
    size_t   byte_pos_ = 0;
    uint32_t bit_pos_  = 0; // 0..7

    uint32_t pop_bit();
};

class BitstreamError : public std::runtime_error {
public:
    explicit BitstreamError(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace ulacr::io
