#pragma once
#include "ulacr/core/types.hpp"
#include "ulacr/coding/context_model.hpp"
#include <vector>
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
    return -static_cast<Sample>((value >> 1) + 1u);
}

inline uint32_t uvarint_size(uint64_t value) noexcept {
    uint32_t bytes = 1;
    while (value >= 0x80u) { value >>= 7; ++bytes; }
    return bytes;
}

struct PredictorResult {
    SampleVec residual;
    uint8_t order = 0;
    std::vector<Sample> seeds;
    uint64_t estimated_bits = 0;
};

inline Sample predict_from_history(std::span<const Sample> samples,
                                   size_t index, uint8_t order) noexcept {
    switch (order) {
        case 1: return samples[index - 1];
        case 2: return 2 * samples[index - 1] - samples[index - 2];
        case 3: return 3 * samples[index - 1] - 3 * samples[index - 2] + samples[index - 3];
        case 4: return 4 * samples[index - 1] - 6 * samples[index - 2]
                      + 4 * samples[index - 3] - samples[index - 4];
        default: return 0;
    }
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
                                        bool enabled) {
    PredictorResult best;
    best.residual.assign(input.begin(), input.end());
    best.estimated_bits = estimate_residual_bits(best.residual) + 8u;

    if (!enabled || input.size() < 2) return best;

    for (uint8_t order = 1; order <= 4 && order < input.size(); ++order) {
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

inline SampleVec inverse_predictor(std::span<const Sample> residual,
                                   uint8_t order,
                                   std::span<const Sample> seeds) {
    if (order == 0) return SampleVec(residual.begin(), residual.end());
    if (order > 4 || seeds.size() != order || residual.size() < order) return {};

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
