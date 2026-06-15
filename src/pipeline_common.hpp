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
