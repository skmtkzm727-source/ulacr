#pragma once
#include <vector>
#include <complex>
#include <span>
#include <cstddef>

namespace ulacr::transform {

using Complex = std::complex<double>;

// ---------------------------------------------------------------
// FFT
//
// 反復 Radix-2 Cooley-Tukey FFT。
// 入力長が 2 のべき乗でない場合はゼロパディングする。
// スペクトル解析（適応的セグメンテーションの spectral flux、
// 倍音モデリングの周波数推定など）に使用する。
// ---------------------------------------------------------------
class FFT {
public:
    /// 実数列 → 複素スペクトル（長さは 2 のべき乗にゼロパディングされる）
    static std::vector<Complex> forward_real(std::span<const double> input);

    /// 複素数列の FFT（in-place ではなくコピーを返す）
    static std::vector<Complex> forward(std::span<const Complex> input);

    /// 逆 FFT（正規化込み）
    static std::vector<Complex> inverse(std::span<const Complex> input);

    /// 振幅スペクトル |X[k]| を計算（長さ N/2+1）
    static std::vector<double> magnitude_spectrum(std::span<const double> input);

    /// 次に大きい（または等しい） 2 のべき乗を返す
    static size_t next_pow2(size_t n) noexcept;

private:
    /// in-place 反復 FFT (Cooley-Tukey, ビット反転 + バタフライ演算)
    static void fft_inplace(std::vector<Complex>& a, bool inverse);
};

} // namespace ulacr::transform
