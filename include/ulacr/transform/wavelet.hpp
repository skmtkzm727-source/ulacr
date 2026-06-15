#pragma once
#include "ulacr/core/types.hpp"
#include "ulacr/core/codec_config.hpp"
#include <vector>
#include <span>

namespace ulacr {

// ---------------------------------------------------------------
// ウェーブレット係数（階層構造）
// ---------------------------------------------------------------
struct WaveletCoeffs {
    uint32_t              levels;        // 分解段数
    std::vector<SampleVec> subbands;     // [0]=最低周波, [levels-1]=最高周波
                                         // 長さ levels+1 (低域 + 各高域)
};

// ---------------------------------------------------------------
// Hierarchical Integer Wavelet Transform
//
// 使用ウェーブレット: CDF 5/3（整数リフティング実装）
//   - 完全可逆（整数演算）
//   - JPEG 2000 で実績あり
//
// 役割:
//   各チャンネルのサンプル列を多段ウェーブレット変換し、
//   高周波成分の残差エネルギーを低減して符号化効率を上げる。
// ---------------------------------------------------------------
class WaveletTransform {
public:
    explicit WaveletTransform(const CodecConfig& cfg) noexcept;

    /// 前進変換: サンプル列 → ウェーブレット係数
    WaveletCoeffs forward(std::span<const Sample> samples) const;

    /// 逆変換: ウェーブレット係数 → サンプル列
    SampleVec inverse(const WaveletCoeffs& coeffs) const;

private:
    /// CDF 5/3 リフティング 1 段前進
    void cdf53_forward_step(SampleVec& low, SampleVec& high,
                            std::span<const Sample> in) const noexcept;
    /// CDF 5/3 リフティング 1 段逆
    void cdf53_inverse_step(SampleVec& out,
                            std::span<const Sample> low,
                            std::span<const Sample> high) const noexcept;

    const CodecConfig& cfg_;
};

} // namespace ulacr
