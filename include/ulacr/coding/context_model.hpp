#pragma once
#include "ulacr/core/types.hpp"
#include "ulacr/coding/rans_coder.hpp"
#include <array>
#include <cstdint>
#include <unordered_map>

namespace ulacr::coding {

// ---------------------------------------------------------------
// コンテキストキー
// 複数の情報をパックしたハッシュとして使用する
// ---------------------------------------------------------------
struct ContextKey {
    int32_t  prev_residual_0;  // 直前残差
    int32_t  prev_residual_1;  // 2つ前残差
    uint8_t  bitplane_idx;     // ビットプレーン位置
    uint8_t  wavelet_level;    // ウェーブレット階層
    uint16_t dict_atom_idx;    // 辞書要素番号
    uint8_t  channel;          // チャンネル番号
    bool     has_graph_ref;    // グラフ参照あり

    bool operator==(const ContextKey&) const noexcept = default;
};

struct ContextKeyHash {
    std::size_t operator()(const ContextKey& k) const noexcept;
};

// ---------------------------------------------------------------
// コンテキストモデル
//
// 役割:
//   各残差シンボルをコンテキストに応じた確率分布で符号化する。
//   コンテキストが異なれば独立した ProbTable を持つ。
//   出現頻度の蓄積によって圧縮中に適応的に更新される。
// ---------------------------------------------------------------
class ContextModel {
public:
    static constexpr uint32_t ALPHABET = 512; // 残差値の量子化範囲

    ContextModel() = default;

    /// コンテキストに対応する確率テーブルを取得（存在しなければ一様分布）
    const ProbTable& get_table(const ContextKey& ctx) const;

    /// シンボルを観測してカウントを更新
    void update(const ContextKey& ctx, uint32_t symbol);

    /// 全コンテキストの確率テーブルを再構築（エンコード前後に呼ぶ）
    void rebuild_tables();

    void reset();

private:
    struct CtxEntry {
        std::array<uint32_t, ALPHABET> counts{};
        ProbTable                      table;
        bool                           dirty = true;
    };

    mutable std::unordered_map<ContextKey, CtxEntry, ContextKeyHash> ctx_map_;
    mutable ProbTable uniform_table_; // フォールバック用
};

} // namespace ulacr::coding
