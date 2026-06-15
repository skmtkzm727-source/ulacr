#pragma once
#include "ulacr/core/types.hpp"
#include <vector>
#include <span>

namespace ulacr::coding {

// ---------------------------------------------------------------
// ResidualIslandData
//
// 残差列を「ゼロ連続領域（island）」と「非ゼロ値」に分離した表現。
//
//   residuals = [0,0,0, 5, 0,0, -3, 0,0,0,0]
//   →  zero_runs   = [3, 2, 4]   (各非ゼロ値の直前のゼロ数 + 末尾ゼロ数)
//      values      = [5, -3]
//      num_values  = 2
//      total_length = 11
//
// zero_runs のサイズは values.size() + 1
// （各非ゼロ値の前のゼロ数 × num_values 個 + 末尾のゼロ数 1 個）。
// ---------------------------------------------------------------
struct ResidualIslandData {
    std::vector<uint32_t> zero_runs; // size = values.size() + 1
    SampleVec              values;
    uint64_t               total_length = 0;
};

// ---------------------------------------------------------------
// ResidualIslandCoder
//
// 役割:
//   予測/変換後の残差はゼロ付近に集中する傾向を持つ。
//   ゼロ値の連続領域を「Residual Island」として位置・長さのみ保存し、
//   非ゼロ値は別ストリームとして管理することで、
//   単純なランレングス圧縮より高い効率を目指す。
// ---------------------------------------------------------------
class ResidualIslandCoder {
public:
    ResidualIslandCoder() = default;

    /// 残差列を Island 表現へ変換する
    static ResidualIslandData encode(std::span<const Sample> residuals);

    /// Island 表現から残差列を復元する
    static SampleVec decode(const ResidualIslandData& data);
};

} // namespace ulacr::coding
