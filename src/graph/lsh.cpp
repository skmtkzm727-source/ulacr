#include "ulacr/graph/lsh.hpp"
#include <random>
#include <algorithm>
#include <unordered_set>

namespace ulacr::graph {

LSHIndex::LSHIndex(uint32_t dim, uint32_t num_tables, uint32_t num_bits, uint64_t seed)
    : dim_(dim), num_tables_(num_tables), num_bits_(num_bits) {

    std::mt19937_64 rng(seed);
    std::normal_distribution<float> dist(0.0f, 1.0f);

    hyperplanes_.resize(num_tables_);
    tables_.resize(num_tables_);

    for (uint32_t t = 0; t < num_tables_; ++t) {
        hyperplanes_[t].resize(num_bits_);
        for (uint32_t b = 0; b < num_bits_; ++b) {
            hyperplanes_[t][b].resize(dim_);
            for (uint32_t d = 0; d < dim_; ++d) {
                hyperplanes_[t][b][d] = dist(rng);
            }
        }
    }
}

uint32_t LSHIndex::compute_hash(const std::vector<float>& vec, uint32_t table) const noexcept {
    uint32_t hash = 0;
    const size_t n = std::min<size_t>(vec.size(), dim_);

    for (uint32_t b = 0; b < num_bits_; ++b) {
        double dot = 0.0;
        const auto& plane = hyperplanes_[table][b];
        for (size_t d = 0; d < n; ++d) {
            dot += static_cast<double>(vec[d]) * static_cast<double>(plane[d]);
        }
        if (dot >= 0.0) hash |= (1u << b);
    }
    return hash;
}

void LSHIndex::insert(uint32_t id, const std::vector<float>& vec) {
    for (uint32_t t = 0; t < num_tables_; ++t) {
        uint32_t h = compute_hash(vec, t);
        tables_[t][h].push_back(id);
    }
}

std::vector<uint32_t> LSHIndex::query(const std::vector<float>& vec) const {
    std::vector<uint32_t> result;
    // Avoid quadratic duplicate checks and pathological work when many
    // identical blocks land in one very large bucket (e.g. repeated tones).
    constexpr size_t MAX_CANDIDATES_PER_TABLE = 64;
    result.reserve(static_cast<size_t>(num_tables_) * MAX_CANDIDATES_PER_TABLE);
    std::unordered_set<uint32_t> seen;
    seen.reserve(static_cast<size_t>(num_tables_) * MAX_CANDIDATES_PER_TABLE);

    for (uint32_t table = 0; table < num_tables_; ++table) {
        const uint32_t hash = compute_hash(vec, table);
        const auto it = tables_[table].find(hash);
        if (it == tables_[table].end() || it->second.empty()) continue;

        const auto& bucket = it->second;
        const size_t count = std::min(bucket.size(), MAX_CANDIDATES_PER_TABLE);
        for (size_t i = 0; i < count; ++i) {
            // For oversized buckets, sample the full insertion range instead
            // of repeatedly examining only the oldest entries.
            const size_t bucket_index = (bucket.size() <= MAX_CANDIDATES_PER_TABLE)
                ? i
                : (i * (bucket.size() - 1)) / (count - 1);
            const uint32_t id = bucket[bucket_index];
            if (seen.insert(id).second) result.push_back(id);
        }
    }

    return result;
}

} // namespace ulacr::graph
