#include "ulacr/transform/fft.hpp"
#include <cmath>
#include <numbers>

namespace ulacr::transform {

size_t FFT::next_pow2(size_t n) noexcept {
    if (n <= 1) return 1;
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

void FFT::fft_inplace(std::vector<Complex>& a, bool inverse) {
    const size_t n = a.size();
    if (n <= 1) return;

    // ビット反転並べ替え
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    // バタフライ演算
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = 2.0 * std::numbers::pi / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        Complex wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            Complex w(1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j) {
                Complex u = a[i + j];
                Complex v = a[i + j + len / 2] * w;
                a[i + j]           = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        for (auto& x : a) x /= static_cast<double>(n);
    }
}

std::vector<Complex> FFT::forward(std::span<const Complex> input) {
    size_t n = next_pow2(input.size());
    std::vector<Complex> a(n, Complex(0.0, 0.0));
    for (size_t i = 0; i < input.size(); ++i) a[i] = input[i];
    fft_inplace(a, false);
    return a;
}

std::vector<Complex> FFT::inverse(std::span<const Complex> input) {
    size_t n = next_pow2(input.size());
    std::vector<Complex> a(n, Complex(0.0, 0.0));
    for (size_t i = 0; i < input.size(); ++i) a[i] = input[i];
    fft_inplace(a, true);
    return a;
}

std::vector<Complex> FFT::forward_real(std::span<const double> input) {
    size_t n = next_pow2(input.size());
    std::vector<Complex> a(n, Complex(0.0, 0.0));
    for (size_t i = 0; i < input.size(); ++i) a[i] = Complex(input[i], 0.0);
    fft_inplace(a, false);
    return a;
}

std::vector<double> FFT::magnitude_spectrum(std::span<const double> input) {
    auto spec = forward_real(input);
    size_t n = spec.size();
    std::vector<double> mag(n / 2 + 1);
    for (size_t k = 0; k <= n / 2; ++k) {
        mag[k] = std::abs(spec[k]);
    }
    return mag;
}

} // namespace ulacr::transform
