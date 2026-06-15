#pragma once
#include "ulacr/core/types.hpp"
#include <vector>
#include <span>

namespace ulacr::analysis {

// ---------------------------------------------------------------
// FeatureExtractor
//
// 適応的セグメンテーションが利用する信号特徴量を計算する。
//   - エネルギー（窓内の二乗平均）
//   - スペクトルフラックス（前後フレームのスペクトル変化量）
//   - ゼロ交差率
//   - 局所エントロピー（ヒストグラムベースのシャノンエントロピー）
// ---------------------------------------------------------------
class FeatureExtractor {
public:
    /// 窓内のエネルギー（RMS の二乗）を計算する
    static double energy(std::span<const Sample> window) noexcept;

    /// 2つの窓間のスペクトルフラックス（振幅スペクトル差のL2ノルム）を計算する
    static double spectral_flux(std::span<const Sample> prev_window,
                                std::span<const Sample> curr_window);

    /// ゼロ交差率（0.0〜1.0）を計算する
    static double zero_crossing_rate(std::span<const Sample> window) noexcept;

    /// 局所エントロピー（ヒストグラムベースのシャノンエントロピー、bit単位）を計算する
    static double local_entropy(std::span<const Sample> window, uint32_t num_bins = 64);
};

} // namespace ulacr::analysis
