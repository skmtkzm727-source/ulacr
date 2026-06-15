#pragma once
#include "ulacr/core/types.hpp"
#include <vector>
#include <span>
#include <cstdint>

namespace ulacr::coding {

// ---------------------------------------------------------------
// BitplaneData
//
// 非負整数列（zigzag変換済み）を上位ビットから下位ビットへ向けて
// num_planes 枚のビットプレーンへ分解した表現。
// 各プレーンは values.size() 個の 0/1 を持つ。
// plane[0] が最上位ビット、plane[num_planes-1] が最下位ビット。
// ---------------------------------------------------------------
struct BitplaneData {
    uint32_t                          num_planes = 0;
    uint32_t                          num_values = 0;
    std::vector<std::vector<uint8_t>> planes; // [num_planes][num_values], 各要素は 0 or 1
};

// ---------------------------------------------------------------
// BitplaneDecomposer
//
// 役割:
//   残差データ（ResidualIslandCoder の非ゼロ値ストリームなど）を
//   ビットプレーン単位に分解する。
//   上位ビットは高い規則性（多くがゼロ）を持つため、
//   独立した二値コンテキストモデルで圧縮することで効率を向上させる。
//
// 値は zigzag 符号化により非負整数として扱う:
//   zigzag(v) = (v >= 0) ? 2v : -2v - 1
// ---------------------------------------------------------------
class BitplaneDecomposer {
public:
    /// signed 値列を zigzag → ビットプレーン分解する。
    /// num_planes は内部で最大値から自動決定され、結果に格納される。
    static BitplaneData decompose(std::span<const Sample> values);

    /// ビットプレーンから signed 値列を復元する（decompose の逆）
    static SampleVec reconstruct(const BitplaneData& planes);

    /// zigzag エンコード / デコード（符号付き整数 <-> 非負整数）
    static uint64_t zigzag_encode(Sample v) noexcept;
    static Sample   zigzag_decode(uint64_t z) noexcept;

    /// 値が必要とする最小ビット数（0 の場合は 0）
    static uint32_t bit_length(uint64_t v) noexcept;
};

} // namespace ulacr::coding
