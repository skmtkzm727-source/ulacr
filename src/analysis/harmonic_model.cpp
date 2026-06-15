#include "ulacr/analysis/harmonic_model.hpp"
#include <cmath>
#include <numbers>
#include <algorithm>

namespace ulacr::analysis {

HarmonicAnalyzer::HarmonicAnalyzer(const CodecConfig& cfg) noexcept : cfg_(cfg) {}

double HarmonicAnalyzer::estimate_fundamental(std::span<const Sample> samples,
                                              uint32_t                 sample_rate) const {
    const size_t N = samples.size();
    if (N < 32 || sample_rate == 0) return 0.0;

    constexpr double MIN_FREQ = 50.0;
    constexpr double MAX_FREQ = 2000.0;

    size_t min_lag = static_cast<size_t>(static_cast<double>(sample_rate) / MAX_FREQ);
    size_t max_lag = static_cast<size_t>(static_cast<double>(sample_rate) / MIN_FREQ);
    if (min_lag < 1) min_lag = 1;
    if (max_lag >= N) max_lag = N - 1;
    if (max_lag <= min_lag) return 0.0;

    double best_corr = -1.0;
    size_t best_lag  = 0;

    for (size_t lag = min_lag; lag <= max_lag; ++lag) {
        double sum = 0.0, norm1 = 0.0, norm2 = 0.0;
        for (size_t i = 0; i + lag < N; ++i) {
            double a = static_cast<double>(samples[i]);
            double b = static_cast<double>(samples[i + lag]);
            sum   += a * b;
            norm1 += a * a;
            norm2 += b * b;
        }
        double denom = std::sqrt(norm1 * norm2);
        double corr  = (denom > 1e-9) ? (sum / denom) : 0.0;
        if (corr > best_corr) {
            best_corr = corr;
            best_lag  = lag;
        }
    }

    // 弱い周期性は非調性（打楽器・ノイズ等）とみなしモデル適用を見送る
    if (best_lag == 0 || best_corr < 0.3) return 0.0;

    return static_cast<double>(sample_rate) / static_cast<double>(best_lag);
}

void HarmonicAnalyzer::goertzel(std::span<const Sample> samples, uint32_t sample_rate,
                                double freq, double& amplitude_out, double& phase_out) const {
    const size_t N = samples.size();
    if (N == 0 || sample_rate == 0) {
        amplitude_out = 0.0;
        phase_out     = 0.0;
        return;
    }

    double w     = 2.0 * std::numbers::pi * freq / static_cast<double>(sample_rate);
    double coeff = 2.0 * std::cos(w);

    double s_prev = 0.0, s_prev2 = 0.0;
    for (size_t i = 0; i < N; ++i) {
        double s = static_cast<double>(samples[i]) + coeff * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev  = s;
    }

    double real = s_prev - s_prev2 * std::cos(w);
    double imag = s_prev2 * std::sin(w);
    double mag  = std::sqrt(real * real + imag * imag);

    amplitude_out = (2.0 * mag) / static_cast<double>(N);
    phase_out     = std::atan2(imag, real);
}

HarmonicModel HarmonicAnalyzer::analyze(std::span<const Sample> samples, uint32_t sample_rate) const {
    HarmonicModel model;
    model.length      = static_cast<uint32_t>(samples.size());
    model.sample_rate = sample_rate;
    model.fundamental_freq = estimate_fundamental(samples, sample_rate);

    uint32_t num_harmonics = cfg_.max_harmonics;
    model.amplitudes.assign(num_harmonics, 0.0);
    model.phases.assign(num_harmonics, 0.0);

    if (model.fundamental_freq <= 0.0 || sample_rate == 0) return model;

    double nyquist = static_cast<double>(sample_rate) / 2.0;
    for (uint32_t h = 0; h < num_harmonics; ++h) {
        double freq = model.fundamental_freq * static_cast<double>(h + 1);
        if (freq >= nyquist) break;
        double amp = 0.0, phase = 0.0;
        goertzel(samples, sample_rate, freq, amp, phase);
        model.amplitudes[h] = amp;
        model.phases[h]     = phase;
    }

    return model;
}

SampleVec HarmonicAnalyzer::synthesize(const HarmonicModel& model) const {
    SampleVec out(model.length, 0);
    if (model.fundamental_freq <= 0.0 || model.sample_rate == 0) return out;

    for (uint32_t n = 0; n < model.length; ++n) {
        double sum = 0.0;
        for (size_t h = 0; h < model.amplitudes.size(); ++h) {
            double freq = model.fundamental_freq * static_cast<double>(h + 1);
            double w    = 2.0 * std::numbers::pi * freq / static_cast<double>(model.sample_rate);
            sum += model.amplitudes[h] * std::cos(w * static_cast<double>(n) + model.phases[h]);
        }
        out[n] = static_cast<Sample>(std::llround(sum));
    }

    return out;
}

SampleVec HarmonicAnalyzer::residual(std::span<const Sample> samples, const HarmonicModel& model) const {
    SampleVec synth = synthesize(model);
    SampleVec out(samples.size(), 0);
    for (size_t i = 0; i < samples.size(); ++i) {
        Sample s = (i < synth.size()) ? synth[i] : 0;
        out[i] = samples[i] - s;
    }
    return out;
}

SampleVec HarmonicAnalyzer::reconstruct(std::span<const Sample> residual_in, const HarmonicModel& model) const {
    SampleVec synth = synthesize(model);
    SampleVec out(residual_in.size(), 0);
    for (size_t i = 0; i < residual_in.size(); ++i) {
        Sample s = (i < synth.size()) ? synth[i] : 0;
        out[i] = residual_in[i] + s;
    }
    return out;
}

} // namespace ulacr::analysis
