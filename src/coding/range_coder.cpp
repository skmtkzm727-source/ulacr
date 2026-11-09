#include "ulacr/coding/range_coder.hpp"
#include <algorithm>
#include <stdexcept>
#include <numeric>

namespace ulacr::coding {
namespace {
constexpr uint64_t TOP = 0xFFFFFFFFull;
constexpr uint64_t HALF = 0x80000000ull;
constexpr uint64_t FIRST_QTR = 0x40000000ull;
constexpr uint64_t THIRD_QTR = 0xC0000000ull;
constexpr uint32_t RESCALE_TOTAL = 16384;

class BitOutput {
public:
    void put(uint32_t bit) {
        current_ = static_cast<uint8_t>((current_ << 1) | (bit & 1u));
        if (++used_ == 8) { bytes_.push_back(current_); current_ = 0; used_ = 0; }
    }
    void finish_padding() {
        if (used_) { current_ <<= (8 - used_); bytes_.push_back(current_); }
        // Arithmetic decoders prime a 32-bit code register and may read ahead.
        while (bytes_.size() < 4) bytes_.push_back(0);
        for (int i = 0; i < 4; ++i) bytes_.push_back(0);
    }
    std::vector<uint8_t> take() { return std::move(bytes_); }
private:
    std::vector<uint8_t> bytes_;
    uint8_t current_ = 0;
    uint32_t used_ = 0;
};

class BitInput {
public:
    explicit BitInput(std::span<const uint8_t> bytes) : bytes_(bytes) {}
    uint32_t get() {
        if (position_ >= bytes_.size() * 8) return 0;
        const uint32_t b = (bytes_[position_ / 8] >> (7 - (position_ % 8))) & 1u;
        ++position_;
        return b;
    }
private:
    std::span<const uint8_t> bytes_;
    size_t position_ = 0;
};

void update_counts(std::vector<uint32_t>& counts, uint64_t& total, uint32_t symbol) {
    ++counts[symbol];
    ++total;
    if (total >= RESCALE_TOTAL) {
        total = 0;
        for (uint32_t& count : counts) {
            count = (count + 1u) >> 1;
            total += count;
        }
    }
}

void emit_with_follow(BitOutput& out, uint32_t bit, uint64_t& pending) {
    out.put(bit);
    while (pending) {
        out.put(bit ^ 1u);
        --pending;
    }
}
} // namespace

std::vector<uint8_t> AdaptiveRangeEncoder::encode(std::span<const uint32_t> symbols,
                                                   uint32_t alphabet_size) const {
    if (alphabet_size == 0 || alphabet_size > 65536)
        throw std::invalid_argument("invalid adaptive range alphabet");
    std::vector<uint32_t> counts(alphabet_size, 1u);
    uint64_t total = alphabet_size;
    uint64_t low = 0, high = TOP, pending = 0;
    BitOutput output;

    for (uint32_t symbol : symbols) {
        if (symbol >= alphabet_size) throw std::invalid_argument("symbol outside alphabet");
        uint64_t cum_low = 0;
        for (uint32_t i = 0; i < symbol; ++i) cum_low += counts[i];
        const uint64_t frequency = counts[symbol];
        const uint64_t range = high - low + 1;
        const uint64_t old_low = low;
        high = old_low + (range * (cum_low + frequency)) / total - 1;
        low = old_low + (range * cum_low) / total;

        for (;;) {
            if (high < HALF) {
                emit_with_follow(output, 0, pending);
            } else if (low >= HALF) {
                emit_with_follow(output, 1, pending);
                low -= HALF; high -= HALF;
            } else if (low >= FIRST_QTR && high < THIRD_QTR) {
                ++pending;
                low -= FIRST_QTR; high -= FIRST_QTR;
            } else {
                break;
            }
            low = (low << 1) & TOP;
            high = ((high << 1) | 1u) & TOP;
        }
        update_counts(counts, total, symbol);
    }

    ++pending;
    emit_with_follow(output, low < FIRST_QTR ? 0u : 1u, pending);
    output.finish_padding();
    return output.take();
}

std::vector<uint32_t> AdaptiveRangeDecoder::decode(std::span<const uint8_t> bytes,
                                                    uint32_t alphabet_size,
                                                    uint64_t symbol_count) const {
    if (alphabet_size == 0 || alphabet_size > 65536 || bytes.size() < 4)
        throw std::invalid_argument("invalid adaptive range stream");
    if (symbol_count > static_cast<uint64_t>(SIZE_MAX))
        throw std::invalid_argument("adaptive range symbol count too large");
    std::vector<uint32_t> counts(alphabet_size, 1u);
    uint64_t total = alphabet_size;
    uint64_t low = 0, high = TOP, code = 0;
    BitInput input(bytes);
    for (int i = 0; i < 32; ++i) code = (code << 1) | input.get();

    std::vector<uint32_t> output;
    output.reserve(static_cast<size_t>(symbol_count));
    for (uint64_t n = 0; n < symbol_count; ++n) {
        const uint64_t range = high - low + 1;
        const uint64_t scaled = ((code - low + 1) * total - 1) / range;
        uint64_t cumulative = 0;
        uint32_t symbol = 0;
        while (symbol < alphabet_size && cumulative + counts[symbol] <= scaled) {
            cumulative += counts[symbol];
            ++symbol;
        }
        if (symbol >= alphabet_size) throw std::runtime_error("invalid adaptive range code");
        const uint64_t frequency = counts[symbol];
        const uint64_t old_low = low;
        high = old_low + (range * (cumulative + frequency)) / total - 1;
        low = old_low + (range * cumulative) / total;

        for (;;) {
            if (high < HALF) {
                // no offset
            } else if (low >= HALF) {
                code -= HALF; low -= HALF; high -= HALF;
            } else if (low >= FIRST_QTR && high < THIRD_QTR) {
                code -= FIRST_QTR; low -= FIRST_QTR; high -= FIRST_QTR;
            } else {
                break;
            }
            low = (low << 1) & TOP;
            high = ((high << 1) | 1u) & TOP;
            code = ((code << 1) | input.get()) & TOP;
        }
        output.push_back(symbol);
        update_counts(counts, total, symbol);
    }
    return output;
}
} // namespace ulacr::coding
