#include "ulacr/graph/block_graph.hpp"
#include "ulacr/graph/lsh.hpp"
#include <cmath>
#include <algorithm>

namespace ulacr {

namespace {
constexpr uint32_t FEATURE_DIM = 32;

double cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    size_t n = std::min(a.size(), b.size());
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (size_t i = 0; i < n; ++i) {
        dot += static_cast<double>(a[i]) * static_cast<double>(b[i]);
        na  += static_cast<double>(a[i]) * static_cast<double>(a[i]);
        nb  += static_cast<double>(b[i]) * static_cast<double>(b[i]);
    }
    if (na <= 1e-12 || nb <= 1e-12) return 0.0;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}
} // namespace

BlockGraphBuilder::BlockGraphBuilder(const CodecConfig& cfg) noexcept : cfg_(cfg) {}

BlockFeature BlockGraphBuilder::extract_feature(const AudioBuffer& buf,
                                                const Segment&     seg,
                                                uint32_t           id) const {
    BlockFeature f;
    f.block_id = id;
    f.feature_vec.assign(FEATURE_DIM, 0.0f);

    const size_t num_ch = buf.channels.size();
    if (num_ch == 0 || seg.length == 0) return f;

    uint32_t bin_size = std::max<uint32_t>(1, seg.length / FEATURE_DIM);

    for (uint32_t b = 0; b < FEATURE_DIM; ++b) {
        uint64_t bin_start = seg.offset + static_cast<uint64_t>(b) * bin_size;
        uint64_t bin_end   = std::min<uint64_t>(seg.offset + seg.length, bin_start + bin_size);
        if (bin_start >= bin_end) continue;

        double sum_sq = 0.0;
        uint64_t count = 0;
        for (uint64_t i = bin_start; i < bin_end; ++i) {
            for (const auto& ch : buf.channels) {
                double v = static_cast<double>(ch[i]);
                sum_sq += v * v;
                ++count;
            }
        }
        f.feature_vec[b] = (count > 0) ? static_cast<float>(std::sqrt(sum_sq / static_cast<double>(count))) : 0.0f;
    }

    // L2 正規化
    double norm = 0.0;
    for (float v : f.feature_vec) norm += static_cast<double>(v) * v;
    norm = std::sqrt(norm);
    if (norm > 1e-9) {
        for (float& v : f.feature_vec) v = static_cast<float>(v / norm);
    }

    return f;
}

std::vector<uint32_t> BlockGraphBuilder::lsh_candidates(
        const std::vector<BlockFeature>& nodes,
        const BlockFeature&              query) const {

    if (nodes.empty()) return {};

    graph::LSHIndex index(FEATURE_DIM, cfg_.lsh_num_tables, cfg_.lsh_num_bits);
    for (const auto& n : nodes) {
        index.insert(n.block_id, n.feature_vec);
    }

    auto candidates = index.query(query.feature_vec);

    // 自分自身は候補から除外する
    candidates.erase(
        std::remove(candidates.begin(), candidates.end(), query.block_id),
        candidates.end());

    return candidates;
}

BlockGraph BlockGraphBuilder::build(const AudioBuffer&         buf,
                                    const std::vector<Segment>& segs) const {
    BlockGraph graph;
    graph.nodes.reserve(segs.size());

    for (size_t i = 0; i < segs.size(); ++i) {
        graph.nodes.push_back(extract_feature(buf, segs[i], static_cast<uint32_t>(i)));
    }

    if (!cfg_.enable_block_graph) return graph;

    // 因果性を保つため、各ノードについて「自分より前のノード」のみを候補とする。
    // (デコード時に参照ブロックが既に復元済みであることを保証するため)
    for (size_t i = 1; i < graph.nodes.size(); ++i) {
        // 同じ長さのセグメントのみが差分対象になり得る
        std::vector<BlockFeature> earlier;
        earlier.reserve(i);
        for (size_t j = 0; j < i; ++j) {
            if (segs[j].length == segs[i].length) {
                earlier.push_back(graph.nodes[j]);
            }
        }
        if (earlier.empty()) continue;

        auto candidates = lsh_candidates(earlier, graph.nodes[i]);

        uint32_t best_ref = 0;
        double   best_sim = -1.0;
        bool     found    = false;
        for (uint32_t cand_id : candidates) {
            if (cand_id >= graph.nodes.size()) continue;
            double sim = cosine_similarity(graph.nodes[i].feature_vec, graph.nodes[cand_id].feature_vec);
            if (sim > best_sim) {
                best_sim = sim;
                best_ref = cand_id;
                found = true;
            }
        }

        if (found && best_sim >= static_cast<double>(cfg_.similarity_threshold)) {
            BlockEdge edge;
            edge.ref_id = best_ref;
            edge.tgt_id = static_cast<uint32_t>(i);
            edge.similarity = static_cast<float>(best_sim);
            graph.edges.push_back(edge);
        }
    }

    return graph;
}

std::optional<uint32_t> BlockGraphBuilder::find_reference(const BlockGraph& g,
                                                          uint32_t          block_id) const noexcept {
    for (const auto& e : g.edges) {
        if (e.tgt_id == block_id) return e.ref_id;
    }
    return std::nullopt;
}

} // namespace ulacr
