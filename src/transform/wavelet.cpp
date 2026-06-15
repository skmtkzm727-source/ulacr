#include "ulacr/transform/wavelet.hpp"
#include <algorithm>

namespace ulacr {

namespace {

/// 正方向への floor 除算（divisor は正であることを仮定）
inline Sample floor_div(Sample a, Sample b) noexcept {
    Sample q = a / b;
    Sample r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) {
        --q;
    }
    return q;
}

} // namespace

WaveletTransform::WaveletTransform(const CodecConfig& cfg) noexcept : cfg_(cfg) {}

// ---------------------------------------------------------------
// CDF 5/3 整数リフティング（前進: 1段）
//
//   高域 (predict):  d[i] = x[2i+1] - floor((x[2i] + x[2i+2]) / 2)
//   低域 (update):   s[i] = x[2i]   + floor((d[i-1] + d[i] + 2) / 4)
//
// 境界では対称拡張を行う。
// ---------------------------------------------------------------
void WaveletTransform::cdf53_forward_step(SampleVec& low, SampleVec& high,
                                          std::span<const Sample> in) const noexcept {
    const size_t n         = in.size();
    const size_t num_high  = n / 2;
    const size_t num_low   = n - num_high; // ceil(n/2)

    high.resize(num_high);
    low.resize(num_low);

    // --- 高域（予測ステップ） ---
    for (size_t i = 0; i < num_high; ++i) {
        Sample left  = in[2 * i];
        Sample right = (2 * i + 2 < n) ? in[2 * i + 2] : in[2 * i];
        high[i] = in[2 * i + 1] - floor_div(left + right, 2);
    }

    // --- 低域（更新ステップ） ---
    for (size_t i = 0; i < num_low; ++i) {
        Sample d_left  = (i >= 1) ? high[i - 1]
                                  : (num_high > 0 ? high[0] : 0);
        Sample d_right = (i < num_high) ? high[i]
                                        : (num_high > 0 ? high[num_high - 1] : 0);
        low[i] = in[2 * i] + floor_div(d_left + d_right + 2, 4);
    }
}

// ---------------------------------------------------------------
// CDF 5/3 整数リフティング（逆方向: 1段）
// ---------------------------------------------------------------
void WaveletTransform::cdf53_inverse_step(SampleVec& out,
                                          std::span<const Sample> low,
                                          std::span<const Sample> high) const noexcept {
    const size_t num_low  = low.size();
    const size_t num_high = high.size();
    const size_t n        = num_low + num_high;

    out.resize(n);

    // --- 偶数位置（低域の逆update） ---
    for (size_t i = 0; i < num_low; ++i) {
        Sample d_left  = (i >= 1) ? high[i - 1]
                                  : (num_high > 0 ? high[0] : 0);
        Sample d_right = (i < num_high) ? high[i]
                                        : (num_high > 0 ? high[num_high - 1] : 0);
        out[2 * i] = low[i] - floor_div(d_left + d_right + 2, 4);
    }

    // --- 奇数位置（高域の逆predict） ---
    for (size_t i = 0; i < num_high; ++i) {
        Sample left  = out[2 * i];
        Sample right = (2 * i + 2 < n) ? out[2 * i + 2] : out[2 * i];
        out[2 * i + 1] = high[i] + floor_div(left + right, 2);
    }
}

// ---------------------------------------------------------------
// 前進変換: サンプル列 → 階層ウェーブレット係数
// ---------------------------------------------------------------
WaveletCoeffs WaveletTransform::forward(std::span<const Sample> samples) const {
    WaveletCoeffs out;

    SampleVec current(samples.begin(), samples.end());
    std::vector<SampleVec> details; // 計算順（最も細かい階層が先頭）

    uint32_t levels_done = 0;
    for (uint32_t lvl = 0; lvl < cfg_.wavelet_levels; ++lvl) {
        if (current.size() < 2) break;
        SampleVec low, high;
        cdf53_forward_step(low, high, current);
        details.push_back(std::move(high));
        current = std::move(low);
        ++levels_done;
    }

    out.levels = levels_done;
    out.subbands.reserve(levels_done + 1);
    out.subbands.push_back(std::move(current)); // [0] = 最低周波（近似）成分

    // details は細かい階層から積まれているので逆順（粗い→細かい）にして追加
    for (auto it = details.rbegin(); it != details.rend(); ++it) {
        out.subbands.push_back(std::move(*it));
    }

    return out;
}

// ---------------------------------------------------------------
// 逆変換: 階層ウェーブレット係数 → サンプル列
// ---------------------------------------------------------------
SampleVec WaveletTransform::inverse(const WaveletCoeffs& coeffs) const {
    if (coeffs.subbands.empty()) return {};

    SampleVec current = coeffs.subbands[0];

    // subbands[1..levels] は粗い→細かいの順
    for (uint32_t i = 0; i < coeffs.levels; ++i) {
        const SampleVec& high = coeffs.subbands[i + 1];
        SampleVec next;
        cdf53_inverse_step(next, current, high);
        current = std::move(next);
    }

    return current;
}

} // namespace ulacr
