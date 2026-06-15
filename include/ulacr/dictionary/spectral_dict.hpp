#pragma once
#include "ulacr/core/types.hpp"
#include "ulacr/core/codec_config.hpp"
#include <vector>
#include <span>

namespace ulacr {

// ---------------------------------------------------------------
// スペクトル辞書
// ---------------------------------------------------------------
struct SpectralDictionary {
    uint32_t               atom_size;   // 辞書原子のサイズ（周波数ビン数）
    uint32_t               dict_size;   // 辞書要素数
    std::vector<std::vector<float>> atoms; // [dict_size][atom_size]
};

// ---------------------------------------------------------------
// スパース符号（各ブロックの辞書表現）
// ---------------------------------------------------------------
struct SparseCode {
    std::vector<uint32_t> indices;     // 使用した辞書要素インデックス
    std::vector<float>    coefficients; // 対応する係数
};

// ---------------------------------------------------------------
// Spectral Dictionary Learning
//
// 役割:
//   楽曲全体のスペクトルデータから K-SVD を用いて辞書を学習し、
//   各ブロックをスパース表現へ変換する。
//   元信号との差分（残差）は完全可逆を保証するために保存される。
// ---------------------------------------------------------------
class SpectralDictLearner {
public:
    explicit SpectralDictLearner(const CodecConfig& cfg) noexcept;

    /// 全セグメントのスペクトルデータから辞書を学習
    SpectralDictionary learn(const std::vector<std::vector<float>>& spectra) const;

    /// あるブロックのスペクトルをスパース符号化（整数残差付き）
    SparseCode encode(std::span<const float>     spectrum,
                      const SpectralDictionary&   dict,
                      std::vector<float>&          residual_out) const;

    /// スパース符号 + 残差から元スペクトルを復元
    std::vector<float> decode(const SparseCode&          code,
                               const SpectralDictionary&  dict,
                               std::span<const float>     residual) const;

private:
    /// K-SVD の 1 イテレーション
    void ksvd_step(SpectralDictionary&                      dict,
                   const std::vector<std::vector<float>>&   signals,
                   std::vector<SparseCode>&                 codes) const;

    /// OMP (Orthogonal Matching Pursuit) によるスパース近似
    SparseCode omp(std::span<const float>    signal,
                   const SpectralDictionary& dict,
                   uint32_t                  sparsity) const;

    const CodecConfig& cfg_;
};

} // namespace ulacr
