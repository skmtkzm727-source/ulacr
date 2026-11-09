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

BlockGraph BlockGraphBuilder::build(
        const AudioBuffer& buf,
        const std::vector<Segment>& segs,
        const std::function<void(float)>& progress) const {
    BlockGraph graph;
    graph.nodes.reserve(segs.size());

    for (size_t i = 0; i < segs.size(); ++i) {
        graph.nodes.push_back(extract_feature(buf, segs[i], static_cast<uint32_t>(i)));
    }

    if (!cfg_.enable_block_graph || graph.nodes.empty()) {
        if (progress) progress(1.0f);
        return graph;
    }

    // Reuse one LSH index per segment length. Rebuilding the index from all
    // previous nodes for every target made this loop O(N^2) in index inserts.
    // Query first, then insert the current node to preserve causal references.
    std::unordered_map<uint32_t, graph::LSHIndex> indexes_by_length;
    indexes_by_length.reserve(8);

    const size_t total = graph.nodes.size();
    for (size_t i = 0; i < total; ++i) {
        const uint32_t length = segs[i].length;
        auto index_it = indexes_by_length.find(length);
        if (index_it == indexes_by_length.end()) {
            index_it = indexes_by_length.try_emplace(
                length, FEATURE_DIM, cfg_.lsh_num_tables, cfg_.lsh_num_bits).first;
        }

        // Only earlier blocks with the exact same segment length are in this
        // index, so every returned reference is valid and causal.
        if (i > 0) {
            const auto candidates = index_it->second.query(graph.nodes[i].feature_vec);

            uint32_t best_ref = 0;
            double best_sim = -1.0;
            bool found = false;
            for (uint32_t candidate_id : candidates) {
                if (candidate_id >= i) continue;
                const double similarity = cosine_similarity(
                    graph.nodes[i].feature_vec,
                    graph.nodes[candidate_id].feature_vec);
                if (similarity > best_sim) {
                    best_sim = similarity;
                    best_ref = candidate_id;
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

        // Insert only after searching so the node can never reference itself.
        index_it->second.insert(static_cast<uint32_t>(i), graph.nodes[i].feature_vec);

        if (progress && ((i + 1) % 16 == 0 || i + 1 == total)) {
            progress(static_cast<float>(i + 1) / static_cast<float>(total));
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
