#include "ulacr/analysis/feature_extractor.hpp"
#include "ulacr/transform/fft.hpp"
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace ulacr::analysis {

double FeatureExtractor::energy(std::span<const Sample> window) noexcept {
    if (window.empty()) return 0.0;
    double sum_sq = 0.0;
    for (Sample s : window) {
        double v = static_cast<double>(s);
        sum_sq += v * v;
    }
    return sum_sq / static_cast<double>(window.size());
}

double FeatureExtractor::spectral_flux(std::span<const Sample> prev_window,
                                       std::span<const Sample> curr_window) {
    if (prev_window.empty() || curr_window.empty()) return 0.0;

    std::vector<double> prev_d(prev_window.size());
    std::vector<double> curr_d(curr_window.size());
    for (size_t i = 0; i < prev_window.size(); ++i) prev_d[i] = static_cast<double>(prev_window[i]);
    for (size_t i = 0; i < curr_window.size(); ++i) curr_d[i] = static_cast<double>(curr_window[i]);

    auto prev_mag = transform::FFT::magnitude_spectrum(prev_d);
    auto curr_mag = transform::FFT::magnitude_spectrum(curr_d);

    size_t n = std::min(prev_mag.size(), curr_mag.size());
    double sum_sq = 0.0;
    for (size_t k = 0; k < n; ++k) {
        double diff = curr_mag[k] - prev_mag[k];
        sum_sq += diff * diff;
    }
    return std::sqrt(sum_sq);
}

double FeatureExtractor::zero_crossing_rate(std::span<const Sample> window) noexcept {
    if (window.size() < 2) return 0.0;
    uint64_t crossings = 0;
    for (size_t i = 1; i < window.size(); ++i) {
        bool prev_neg = window[i - 1] < 0;
        bool curr_neg = window[i] < 0;
        if (prev_neg != curr_neg) ++crossings;
    }
    return static_cast<double>(crossings) / static_cast<double>(window.size() - 1);
}

double FeatureExtractor::local_entropy(std::span<const Sample> window, uint32_t num_bins) {
    if (window.empty() || num_bins == 0) return 0.0;

    Sample min_v = window[0], max_v = window[0];
    for (Sample s : window) {
        min_v = std::min(min_v, s);
        max_v = std::max(max_v, s);
    }

    if (min_v == max_v) return 0.0; // 全て同値 → エントロピー0

    std::vector<uint64_t> hist(num_bins, 0);
    double range = static_cast<double>(max_v - min_v);

    for (Sample s : window) {
        double normalized = static_cast<double>(s - min_v) / range; // [0,1]
        uint32_t bin = static_cast<uint32_t>(normalized * static_cast<double>(num_bins));
        if (bin >= num_bins) bin = num_bins - 1;
        hist[bin]++;
    }

    double n = static_cast<double>(window.size());
    double h = 0.0;
    for (uint64_t count : hist) {
        if (count == 0) continue;
        double p = static_cast<double>(count) / n;
        h -= p * std::log2(p);
    }
    return h;
}

} // namespace ulacr::analysis
