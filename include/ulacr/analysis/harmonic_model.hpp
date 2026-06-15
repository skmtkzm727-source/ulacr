#pragma once
#include "ulacr/core/types.hpp"
#include "ulacr/core/codec_config.hpp"
#include <vector>
#include <span>

namespace ulacr::analysis {

// ---------------------------------------------------------------
// HarmonicModel
//
// 基本周波数と倍音列（振幅・位相）で表現された音声モデル。
// synthesize() により時間領域信号を再構成できる。
// ---------------------------------------------------------------
struct HarmonicModel {
    double               fundamental_freq = 0.0; // Hz (0 = 無音/非調性)
    std::vector<double>  amplitudes;             // 倍音ごとの振幅
    std::vector<double>  phases;                 // 倍音ごとの位相 (rad)
    uint32_t             length = 0;             // モデル対象のサンプル数
    uint32_t             sample_rate = 0;        // 再合成に必要なサンプリング周波数
};

// ---------------------------------------------------------------
// HarmonicAnalyzer
//
// 役割:
//   各ブロックに対して自己相関法による基本周波数推定を行い、
//   Goertzel アルゴリズムで各倍音成分の振幅・位相を抽出する。
//   推定モデルを整数値へ丸めて時間領域信号として再合成し、
//   元信号との差分のみを残差として保持することで、
//   倍音構造を持つ楽器音・ボーカルに対する完全可逆圧縮を実現する。
// ---------------------------------------------------------------
class HarmonicAnalyzer {
public:
    explicit HarmonicAnalyzer(const CodecConfig& cfg) noexcept;

    /// サンプル列から倍音モデルを推定する
    HarmonicModel analyze(std::span<const Sample> samples, uint32_t sample_rate) const;

    /// 倍音モデルから整数サンプル列を再合成する（量子化丸め込み）
    SampleVec synthesize(const HarmonicModel& model) const;

    /// 元信号からモデルを減算した残差を計算する
    SampleVec residual(std::span<const Sample> samples, const HarmonicModel& model) const;

    /// 残差 + モデルから元信号を復元する（synthesize の加算）
    SampleVec reconstruct(std::span<const Sample> residual, const HarmonicModel& model) const;

private:
    /// 自己相関法による基本周波数推定 (Hz)
    double estimate_fundamental(std::span<const Sample> samples, uint32_t sample_rate) const;

    /// Goertzel アルゴリズムによる単一周波数の振幅・位相推定
    void goertzel(std::span<const Sample> samples, uint32_t sample_rate,
                  double freq, double& amplitude_out, double& phase_out) const;

    const CodecConfig& cfg_;
};

} // namespace ulacr::analysis
