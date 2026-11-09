#pragma once
#include "ulacr/core/types.hpp"
#include "ulacr/coding/context_model.hpp"
#include <vector>
#include <array>
#include <bit>
#include <algorithm>

// ---------------------------------------------------------------
// encoder.cpp / decoder.cpp で共有される内部実装の詳細。
// 公開APIではなく、両者が同じビットストリーム規約に従うために
// 同一のロジックを使用することを保証するためのヘルパー群。
// ---------------------------------------------------------------
namespace ulacr::detail {

// シンボルストリームの種別
constexpr uint8_t STREAM_RUNS  = 0; // ゼロ連続長 (Residual Island)
constexpr uint8_t STREAM_VALUE = 1; // 非ゼロ残差値 (bucket方式)
constexpr uint8_t STREAM_PLANE = 2; // ビットプレーン (bit方式)

// ビットプレーンのコンテキストに使う最大プレーン番号
// (これを超えるプレーンは同一コンテキストへ畳み込まれる)
constexpr uint32_t PLANE_CONTEXT_MAX = 24;

/// 値が必要とする最小ビット数 (0 の場合は 0)
inline uint32_t bit_length_u64(uint64_t v) noexcept {
    return v == 0 ? 0u : (64u - static_cast<uint32_t>(std::countl_zero(v)));
}

inline uint64_t zigzag_u64(Sample value) noexcept {
    return value >= 0
        ? static_cast<uint64_t>(value) * 2u
        : static_cast<uint64_t>(-(value + 1)) * 2u + 1u;
}

inline Sample unzigzag_sample(uint64_t value) noexcept {
    if ((value & 1u) == 0) return static_cast<Sample>(value >> 1);
    // Complement is the exact two's-complement inverse, including INT64_MIN.
    return std::bit_cast<Sample>(~(value >> 1));
}

inline uint32_t uvarint_size(uint64_t value) noexcept {
    uint32_t bytes = 1;
    while (value >= 0x80u) { value >>= 7; ++bytes; }
    return bytes;
}

struct PredictorResult {
    SampleVec residual;
    uint8_t order = 0;
    bool reverse = false;
    std::vector<Sample> seeds;
    uint64_t estimated_bits = 0;
};

inline Sample predict_from_history(std::span<const Sample> samples,
                                   size_t index, uint8_t order) noexcept {
    if (order == 0 || order > 16 || index < order) return 0;
    // Exact integer backward-difference predictor. For <=32-bit PCM and
    // order<=16, the coefficient L1 norm is at most 2^16.
    int64_t coefficient = 1;
    int64_t prediction = 0;
    for (uint8_t k = 1; k <= order; ++k) {
        coefficient = coefficient * static_cast<int64_t>(order - k + 1) / k;
        const int64_t term = coefficient * samples[index - k];
        prediction += (k & 1u) ? term : -term;
    }
    return prediction;
}

inline uint64_t estimate_residual_bits(std::span<const Sample> residual) noexcept {
    uint64_t bits = 0;
    for (Sample value : residual) {
        const uint64_t zz = zigzag_u64(value);
        if (zz != 0) bits += bit_length_u64(zz) + 1u;
    }
    return bits;
}

// FLAC型の固定予測器（次数0〜4）をブロックごとに比較。
// 予測次数と先頭サンプルもコストに含め、悪化する場合は次数0を選ぶ。
inline PredictorResult choose_predictor(std::span<const Sample> input,
                                        bool enabled,
                                        uint8_t max_order = 4) {
    PredictorResult best;
    best.residual.assign(input.begin(), input.end());
    best.estimated_bits = estimate_residual_bits(best.residual) + 8u;

    if (!enabled || input.size() < 2) return best;

    max_order = std::min<uint8_t>(max_order, 16);
    for (uint8_t order = 1; order <= max_order && order < input.size(); ++order) {
        SampleVec residual(input.size(), 0);
        std::vector<Sample> seeds(input.begin(), input.begin() + order);
        for (size_t i = order; i < input.size(); ++i) {
            residual[i] = input[i] - predict_from_history(input, i, order);
        }

        uint64_t cost = estimate_residual_bits(residual) + 8u;
        for (Sample seed : seeds) cost += 8u * uvarint_size(zigzag_u64(seed));

        if (cost < best.estimated_bits) {
            best.residual = std::move(residual);
            best.order = order;
            best.seeds = std::move(seeds);
            best.estimated_bits = cost;
        }
    }
    return best;
}

// Four-tap fixed-point NLMS. Encoder and decoder update weights from
// exactly reconstructed samples, so no floating-point state is transmitted.
inline uint8_t nlms_normalization_shift(SampleFormat format) noexcept {
    switch (format) {
        case SampleFormat::Int24: return 8;
        case SampleFormat::Int32: return 16;
        default: return 0;
    }
}

using NlmsWeights = std::array<int64_t, 4>;

inline Sample adaptive_nlms_predict(const NlmsWeights& weights,
                                    std::span<const Sample> history,
                                    size_t index,
                                    uint8_t normalization_shift) noexcept {
    const Sample scale = Sample{1} << normalization_shift;
    int64_t dot = 0;
    for (size_t tap = 0; tap < weights.size(); ++tap) {
        const Sample x = history[index - 1 - tap] / scale;
        dot += weights[tap] * x;
    }
    return (dot / 65536) * scale;
}

inline void adaptive_nlms_update(NlmsWeights& weights,
                                 std::span<const Sample> history,
                                 size_t index,
                                 Sample error,
                                 uint8_t normalization_shift) noexcept {
    const Sample scale = Sample{1} << normalization_shift;
    const Sample error_norm = error / scale;
    int64_t energy = 1 << 16;
    std::array<Sample, 4> normalized{};
    for (size_t tap = 0; tap < weights.size(); ++tap) {
        normalized[tap] = history[index - 1 - tap] / scale;
        energy += normalized[tap] * normalized[tap];
    }
    constexpr int64_t step_q16 = 16384; // mu = 0.25
    constexpr int64_t weight_limit = 8 * 65536;
    for (size_t tap = 0; tap < weights.size(); ++tap) {
        const int64_t delta = (step_q16 * error_norm * normalized[tap]) / energy;
        weights[tap] = std::clamp(weights[tap] + delta, -weight_limit, weight_limit);
    }
}

inline PredictorResult make_adaptive_nlms_prediction(
        std::span<const Sample> input, uint8_t normalization_shift) {
    PredictorResult result;
    result.residual.assign(input.size(), 0);
    if (input.size() < 4) {
        result.residual.assign(input.begin(), input.end());
        return result;
    }
    result.order = 17; // v0.3 mode tag; four initial history samples follow.
    result.seeds.assign(input.begin(), input.begin() + 4);
    NlmsWeights weights{};
    for (size_t i = 4; i < input.size(); ++i) {
        const Sample prediction = adaptive_nlms_predict(weights, input, i, normalization_shift);
        const Sample error = input[i] - prediction;
        result.residual[i] = error;
        adaptive_nlms_update(weights, input, i, error, normalization_shift);
    }
    return result;
}

inline SampleVec inverse_adaptive_nlms(std::span<const Sample> residual,
                                       std::span<const Sample> seeds,
                                       uint8_t normalization_shift) {
    if (seeds.size() != 4 || residual.size() < 4) return {};
    SampleVec output(residual.begin(), residual.end());
    std::copy(seeds.begin(), seeds.end(), output.begin());
    NlmsWeights weights{};
    for (size_t i = 4; i < output.size(); ++i) {
        const Sample prediction = adaptive_nlms_predict(weights, output, i, normalization_shift);
        output[i] = residual[i] + prediction;
        adaptive_nlms_update(weights, output, i, residual[i], normalization_shift);
    }
    return output;
}

inline SampleVec inverse_delay_match(std::span<const Sample> residual,
                                              std::span<const Sample> seeds) {
    if (seeds.empty()) return {};
    const Sample lag_value = seeds[0];
    if (lag_value <= 0 || lag_value > 64) return {};
    const size_t lag = static_cast<size_t>(lag_value);
    if (seeds.size() != lag + 1 || residual.size() < lag) return {};
    SampleVec output(residual.size(), 0);
    std::copy(seeds.begin() + 1, seeds.end(), output.begin());
    for (size_t i = lag; i < output.size(); ++i)
        output[i] = residual[i] + output[i - lag];
    return output;
}

inline SampleVec inverse_predictor(std::span<const Sample> residual,
                                   uint8_t order,
                                   std::span<const Sample> seeds,
                                   uint8_t normalization_shift = 0) {
    if (order == 0) return SampleVec(residual.begin(), residual.end());
    if (order == 17) return inverse_adaptive_nlms(residual, seeds, normalization_shift);
    if (order == 18) return inverse_delay_match(residual, seeds);
    if (order > 16 || seeds.size() != order || residual.size() < order) return {};

    SampleVec output(residual.begin(), residual.end());
    for (uint8_t i = 0; i < order; ++i) output[i] = seeds[i];
    for (size_t i = order; i < output.size(); ++i) {
        output[i] = residual[i] + predict_from_history(output, i, order);
    }
    return output;
}

inline Sample floor_div2(Sample value) noexcept {
    Sample q = value / 2;
    if (value < 0 && (value % 2) != 0) --q;
    return q;
}

inline Sample ceil_div2(Sample value) noexcept {
    return value > 0 ? (value + 1) / 2 : value / 2;
}

/// コンテキストキーを構築する。
/// prev_residual_1 / dict_atom_idx / has_graph_ref は本パイプラインでは未使用 (常に0/false)。
inline coding::ContextKey make_ctx(uint8_t channel, uint8_t subband_level,
                                   uint8_t stream_type, uint8_t plane_idx = 0) noexcept {
    coding::ContextKey k{};
    k.prev_residual_0 = static_cast<int32_t>(stream_type);
    k.prev_residual_1 = 0;
    k.bitplane_idx    = plane_idx;
    k.wavelet_level   = subband_level;
    k.dict_atom_idx   = 0;
    k.channel         = channel;
    k.has_graph_ref   = false;
    return k;
}

/// セグメント長と完了済みウェーブレット段数から各サブバンドの長さを計算する。
/// WaveletTransform::forward と同一の分割規則 (num_high = n/2, num_low = n - num_high)
/// に従うため、デコーダはウェーブレット変換を実行せずにサブバンド長を再計算できる。
///
/// 戻り値: [0]=最終低域(近似)成分, [1..levels_done]=粗→細の高域成分
inline std::vector<uint32_t> compute_subband_sizes(uint32_t seg_len, uint32_t levels_done) {
    std::vector<uint32_t> details;
    details.reserve(levels_done);

    uint32_t cur = seg_len;
    for (uint32_t l = 0; l < levels_done; ++l) {
        uint32_t num_high = cur / 2;
        uint32_t num_low  = cur - num_high;
        details.push_back(num_high);
        cur = num_low;
    }

    std::vector<uint32_t> out;
    out.reserve(levels_done + 1);
    out.push_back(cur);
    for (auto it = details.rbegin(); it != details.rend(); ++it) out.push_back(*it);
    return out;
}

/// plane index をコンテキスト用に丸め込む
inline uint8_t clamp_plane(uint32_t plane_idx) noexcept {
    return static_cast<uint8_t>(std::min<uint32_t>(plane_idx, PLANE_CONTEXT_MAX));
}

} // namespace ulacr::detail
