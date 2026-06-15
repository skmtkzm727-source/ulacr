#include "ulacr/coding/context_model.hpp"
#include <functional>

namespace ulacr::coding {

std::size_t ContextKeyHash::operator()(const ContextKey& k) const noexcept {
    std::size_t h = 0xcbf29ce484222325ull;
    auto mix = [&h](std::size_t v) {
        h ^= v;
        h *= 0x100000001b3ull;
    };
    mix(static_cast<std::size_t>(static_cast<uint32_t>(k.prev_residual_0)));
    mix(static_cast<std::size_t>(static_cast<uint32_t>(k.prev_residual_1)));
    mix(static_cast<std::size_t>(k.bitplane_idx));
    mix(static_cast<std::size_t>(k.wavelet_level));
    mix(static_cast<std::size_t>(k.dict_atom_idx));
    mix(static_cast<std::size_t>(k.channel));
    mix(static_cast<std::size_t>(k.has_graph_ref ? 1 : 0));
    return h;
}

const ProbTable& ContextModel::get_table(const ContextKey& ctx) const {
    auto it = ctx_map_.find(ctx);
    if (it == ctx_map_.end()) {
        if (uniform_table_.cumul.empty()) {
            std::vector<uint32_t> zero(ALPHABET, 0);
            uniform_table_ = build_prob_table(zero, ALPHABET);
        }
        return uniform_table_;
    }

    CtxEntry& entry = it->second;
    if (entry.dirty) {
        entry.table = build_prob_table(entry.counts, ALPHABET);
        entry.dirty = false;
    }
    return entry.table;
}

void ContextModel::update(const ContextKey& ctx, uint32_t symbol) {
    if (symbol >= ALPHABET) return; // 範囲外シンボルは無視
    CtxEntry& entry = ctx_map_[ctx];
    entry.counts[symbol] += 1;
    entry.dirty = true;
}

void ContextModel::rebuild_tables() {
    for (auto& kv : ctx_map_) {
        CtxEntry& entry = kv.second;
        if (entry.dirty) {
            entry.table = build_prob_table(entry.counts, ALPHABET);
            entry.dirty = false;
        }
    }
}

void ContextModel::reset() {
    ctx_map_.clear();
    uniform_table_ = ProbTable{};
}

} // namespace ulacr::coding
