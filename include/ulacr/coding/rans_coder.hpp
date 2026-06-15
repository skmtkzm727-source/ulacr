#pragma once
#include <cstdint>
#include <vector>
#include <span>

namespace ulacr::coding {

// ---------------------------------------------------------------
// 確率テーブル（シンボル → 符号化確率）
// ---------------------------------------------------------------
struct ProbTable {
    static constexpr uint32_t SCALE_BITS = 14;         // 確率精度
    static constexpr uint32_t SCALE      = 1 << SCALE_BITS;

    std::vector<uint16_t> cumul;   // 累積確率 [0, SCALE] (size = alphabet+1)
    uint32_t              alphabet; // アルファベットサイズ

    /// シンボルの確率を返す（freq 単位）
    uint16_t freq(uint32_t sym)  const noexcept { return cumul[sym+1] - cumul[sym]; }
    uint16_t start(uint32_t sym) const noexcept { return cumul[sym]; }
};

// ---------------------------------------------------------------
// rANS エンコーダ
//
// Range Asymmetric Numeral Systems による逆順エンコード。
// 出力はリトルエンディアン 32bit ワード列。
// ---------------------------------------------------------------
class RansEncoder {
public:
    RansEncoder() = default;

    /// シンボル列を符号化してビット列を返す
    std::vector<uint8_t> encode(std::span<const uint32_t> symbols,
                                const ProbTable&          table) const;

private:
    static constexpr uint64_t RANS_L     = 1u << 23; // 下限
    static constexpr uint32_t RANS_UPPER = 1u << 31;

    void rans_encode_sym(uint64_t& state, std::vector<uint32_t>& out,
                         uint32_t start, uint32_t freq) const noexcept;
    void rans_flush(uint64_t state, std::vector<uint32_t>& out) const noexcept;
};

// ---------------------------------------------------------------
// rANS デコーダ
// ---------------------------------------------------------------
class RansDecoder {
public:
    RansDecoder() = default;

    /// 符号化ビット列からシンボル列を復元
    std::vector<uint32_t> decode(std::span<const uint8_t> data,
                                 const ProbTable&         table,
                                 uint64_t                 num_symbols) const;

private:
    static constexpr uint64_t RANS_L = 1u << 23;

    uint32_t rans_decode_sym(uint64_t& state,
                             const std::vector<uint32_t>& buf,
                             uint32_t& buf_pos,
                             const ProbTable& table) const noexcept;
};

// ---------------------------------------------------------------
// 確率テーブルビルダ（シンボル頻度から構築）
// ---------------------------------------------------------------
ProbTable build_prob_table(std::span<const uint32_t> symbol_counts,
                           uint32_t                  alphabet_size);

} // namespace ulacr::coding
