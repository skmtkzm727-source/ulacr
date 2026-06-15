#pragma once
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace ulacr::graph {

// ---------------------------------------------------------------
// LSHIndex
//
// SimHash（ランダム超平面射影）に基づく局所感度ハッシュ。
// コサイン類似度の近い特徴量ベクトルを高速に発見するために用いる。
//
//   hash(v) = ビット列。各ビット b は sign(v · h_b) で決まる
//             （h_b はランダムな単位超平面）
//
// num_tables 個の独立なハッシュテーブルを用意し、
// いずれかのテーブルで同じバケットに入った要素を候補とする。
// ---------------------------------------------------------------
class LSHIndex {
public:
    LSHIndex(uint32_t dim, uint32_t num_tables, uint32_t num_bits, uint64_t seed = 0x9E3779B97F4A7C15ull);

    /// 特徴量ベクトルを ID 付きで登録する
    void insert(uint32_t id, const std::vector<float>& vec);

    /// 類似候補の ID 一覧を返す（重複なし、登録順は保証しない）
    std::vector<uint32_t> query(const std::vector<float>& vec) const;

private:
    uint32_t compute_hash(const std::vector<float>& vec, uint32_t table) const noexcept;

    uint32_t dim_;
    uint32_t num_tables_;
    uint32_t num_bits_;

    // hyperplanes_[table][bit] = dim_ 次元の射影ベクトル
    std::vector<std::vector<std::vector<float>>> hyperplanes_;

    // tables_[table][hash] = id のリスト
    std::vector<std::unordered_map<uint32_t, std::vector<uint32_t>>> tables_;
};

} // namespace ulacr::graph
