#pragma once
#include "ulacr/core/types.hpp"
#include "ulacr/core/codec_config.hpp"
#include "ulacr/analysis/segmenter.hpp"
#include <vector>
#include <unordered_map>
#include <optional>

namespace ulacr {

// ---------------------------------------------------------------
// ブロック特徴量ベクトル
// ---------------------------------------------------------------
struct BlockFeature {
    uint32_t            block_id;
    std::vector<float>  feature_vec; // 正規化済み特徴量
};

// ---------------------------------------------------------------
// グラフエッジ（類似ブロックペア）
// ---------------------------------------------------------------
struct BlockEdge {
    uint32_t ref_id;    // 参照ブロック（基準）
    uint32_t tgt_id;    // 対象ブロック
    float    similarity;
};

// ---------------------------------------------------------------
// グラフ全体
// ---------------------------------------------------------------
struct BlockGraph {
    std::vector<BlockFeature> nodes;
    std::vector<BlockEdge>    edges;
};

// ---------------------------------------------------------------
// Block Graph Compression
//
// 役割:
//   1. 各 Segment から特徴量ベクトルを抽出
//   2. LSH を用いた近似最近傍探索で類似ブロックを発見
//   3. 類似ブロック間は差分のみ保存するグラフ構造を構築
//      （例: 繰り返しサビ → 基準ブロック + 差分）
// ---------------------------------------------------------------
class BlockGraphBuilder {
public:
    explicit BlockGraphBuilder(const CodecConfig& cfg) noexcept;

    /// セグメント列からグラフを構築
    BlockGraph build(const AudioBuffer&           buf,
                     const std::vector<Segment>&   segs) const;

    /// block_id に対応する参照ブロックを返す（存在しなければ nullopt）
    std::optional<uint32_t> find_reference(const BlockGraph& g,
                                           uint32_t block_id) const noexcept;

private:
    BlockFeature extract_feature(const AudioBuffer&  buf,
                                 const Segment&      seg,
                                 uint32_t            id) const;

    /// Locality Sensitive Hashing による候補探索
    std::vector<uint32_t> lsh_candidates(
            const std::vector<BlockFeature>& nodes,
            const BlockFeature&              query) const;

    const CodecConfig& cfg_;
};

} // namespace ulacr
