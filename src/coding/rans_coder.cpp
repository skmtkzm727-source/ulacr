#include "ulacr/coding/rans_coder.hpp"
#include <algorithm>
#include <cassert>

namespace ulacr::coding {

// =================================================================
// RansEncoder
// =================================================================

std::vector<uint8_t> RansEncoder::encode(std::span<const uint32_t> symbols,
                                         const ProbTable&          table) const {
    // 出力バッファを十分大きく確保し、末尾から逆方向に書き込む
    // （rANS の標準的な実装方法: ryg_rans 方式）
    const size_t cap = symbols.size() * 8 + 64;
    std::vector<uint8_t> buf(cap);
    uint8_t* const end = buf.data() + buf.size();
    uint8_t* ptr = end;

    uint64_t x = RANS_L;

    for (size_t i = symbols.size(); i-- > 0; ) {
        uint32_t sym   = symbols[i];
        assert(sym < table.alphabet);
        uint32_t start = table.start(sym);
        uint32_t freq  = table.freq(sym);
        assert(freq > 0 && "zero-frequency symbol cannot be encoded");

        // 正規化: x が許容範囲を超える間、下位バイトを出力へ retire する
        const uint64_t x_max =
            ((RANS_L >> ProbTable::SCALE_BITS) << 8) * static_cast<uint64_t>(freq);
        while (x >= x_max) {
            *--ptr = static_cast<uint8_t>(x & 0xFF);
            x >>= 8;
        }

        // C(x, s) = (x / freq) << scale_bits + (x % freq) + start
        x = ((x / freq) << ProbTable::SCALE_BITS) + (x % freq) + start;
    }

    // 最終状態を 4 バイト（ビッグエンディアン）でフラッシュ
    for (int i = 0; i < 4; ++i) {
        *--ptr = static_cast<uint8_t>(x & 0xFF);
        x >>= 8;
    }

    return std::vector<uint8_t>(ptr, end);
}

void RansEncoder::rans_encode_sym(uint64_t&, std::vector<uint32_t>&,
                                  uint32_t, uint32_t) const noexcept {
    // 未使用（encode() 内にインライン実装済み）
}

void RansEncoder::rans_flush(uint64_t, std::vector<uint32_t>&) const noexcept {
    // 未使用（encode() 内にインライン実装済み）
}

// =================================================================
// RansDecoder
// =================================================================

namespace {

/// xs (0..SCALE-1) に対応するシンボルを cumul テーブルから二分探索で求める
uint32_t find_symbol(const ProbTable& table, uint32_t xs) noexcept {
    // cumul[0]=0 <= xs < cumul[alphabet]=SCALE
    // cumul[sym] <= xs < cumul[sym+1] となる sym を探す
    uint32_t lo = 0, hi = table.alphabet; // [lo, hi)
    while (lo + 1 < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (table.cumul[mid] <= xs) lo = mid;
        else hi = mid;
    }
    return lo;
}

} // namespace

std::vector<uint32_t> RansDecoder::decode(std::span<const uint8_t> data,
                                          const ProbTable&         table,
                                          uint64_t                 num_symbols) const {
    std::vector<uint32_t> out;
    out.reserve(num_symbols);

    const uint8_t* ptr = data.data();
    const uint8_t* const data_end = data.data() + data.size();

    uint64_t x = 0;
    for (int i = 0; i < 4; ++i) {
        x = (x << 8) | (ptr < data_end ? *ptr++ : 0);
    }

    for (uint64_t i = 0; i < num_symbols; ++i) {
        uint32_t xs  = static_cast<uint32_t>(x & (ProbTable::SCALE - 1));
        uint32_t sym = find_symbol(table, xs);

        uint32_t start = table.start(sym);
        uint32_t freq  = table.freq(sym);

        x = static_cast<uint64_t>(freq) * (x >> ProbTable::SCALE_BITS) + xs - start;

        while (x < RANS_L && ptr < data_end) {
            x = (x << 8) | *ptr++;
        }

        out.push_back(sym);
    }

    return out;
}

uint32_t RansDecoder::rans_decode_sym(uint64_t&, const std::vector<uint32_t>&,
                                      uint32_t&, const ProbTable&) const noexcept {
    return 0; // 未使用（decode() 内にインライン実装済み）
}

// =================================================================
// ProbTable builder
// =================================================================

ProbTable build_prob_table(std::span<const uint32_t> symbol_counts,
                           uint32_t                  alphabet_size) {
    ProbTable table;
    table.alphabet = alphabet_size;
    table.cumul.assign(alphabet_size + 1, 0);

    std::vector<uint32_t> counts_copy;
    if (symbol_counts.size() != alphabet_size) {
        // 不正なサイズ: 一様分布にフォールバック
        counts_copy.assign(alphabet_size, 0);
        symbol_counts = counts_copy;
    }

    uint64_t total = 0;
    for (uint32_t c : symbol_counts) total += c;

    std::vector<uint32_t> freqs(alphabet_size, 0);

    if (total == 0) {
        // 一様分布
        uint32_t base = ProbTable::SCALE / alphabet_size;
        uint32_t rem  = ProbTable::SCALE - base * alphabet_size;
        for (uint32_t i = 0; i < alphabet_size; ++i) {
            freqs[i] = base + (i < rem ? 1u : 0u);
        }
    } else {
        uint64_t assigned = 0;
        for (uint32_t i = 0; i < alphabet_size; ++i) {
            uint64_t c = symbol_counts[i];
            uint32_t f;
            if (c == 0) {
                f = 0;
            } else {
                uint64_t scaled = (c * static_cast<uint64_t>(ProbTable::SCALE)) / total;
                f = static_cast<uint32_t>(std::max<uint64_t>(1, scaled));
            }
            freqs[i] = f;
            assigned += f;
        }

        // 合計が SCALE になるよう調整する
        int64_t diff = static_cast<int64_t>(ProbTable::SCALE) - static_cast<int64_t>(assigned);

        while (diff != 0) {
            if (diff > 0) {
                // 最大頻度のシンボルへ加算
                uint32_t best = 0;
                for (uint32_t i = 1; i < alphabet_size; ++i) {
                    if (freqs[i] > freqs[best]) best = i;
                }
                freqs[best] += 1;
                diff -= 1;
            } else {
                // freq > 1 のシンボルから最大のものを減算
                int32_t best = -1;
                for (uint32_t i = 0; i < alphabet_size; ++i) {
                    if (freqs[i] > 1 && (best < 0 || freqs[i] > freqs[static_cast<uint32_t>(best)])) {
                        best = static_cast<int32_t>(i);
                    }
                }
                if (best < 0) break; // これ以上減らせない（縮退ケース）
                freqs[static_cast<uint32_t>(best)] -= 1;
                diff += 1;
            }
        }
    }

    for (uint32_t i = 0; i < alphabet_size; ++i) {
        table.cumul[i + 1] = static_cast<uint16_t>(table.cumul[i] + static_cast<uint16_t>(freqs[i]));
    }

    return table;
}

} // namespace ulacr::coding
