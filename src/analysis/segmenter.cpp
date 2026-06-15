#include "ulacr/analysis/segmenter.hpp"
#include "ulacr/analysis/feature_extractor.hpp"
#include <algorithm>
#include <cmath>

namespace ulacr {

using analysis::FeatureExtractor;

Segmenter::Segmenter(const CodecConfig& cfg) noexcept : cfg_(cfg) {}

float Segmenter::compute_energy_change(const SampleVec& ch, uint64_t pos, uint32_t win) const noexcept {
    uint64_t total = ch.size();
    uint64_t prev_start = (pos >= win) ? pos - win : 0;
    uint64_t prev_len   = pos - prev_start;
    uint64_t curr_len   = (pos < total) ? std::min<uint64_t>(win, total - pos) : 0;
    if (prev_len == 0 || curr_len == 0) return 0.0f;

    double e_prev = FeatureExtractor::energy(
        std::span<const Sample>(ch.data() + prev_start, prev_len));
    double e_curr = FeatureExtractor::energy(
        std::span<const Sample>(ch.data() + pos, curr_len));

    double denom = std::max(e_prev, 1.0);
    return static_cast<float>(std::abs(e_curr - e_prev) / denom);
}

float Segmenter::compute_spectral_flux(const SampleVec& ch, uint64_t pos, uint32_t win) const noexcept {
    uint64_t total = ch.size();
    uint64_t prev_start = (pos >= win) ? pos - win : 0;
    uint64_t prev_len   = pos - prev_start;
    uint64_t curr_len   = (pos < total) ? std::min<uint64_t>(win, total - pos) : 0;
    if (prev_len < 2 || curr_len < 2) return 0.0f;

    double flux = FeatureExtractor::spectral_flux(
        std::span<const Sample>(ch.data() + prev_start, prev_len),
        std::span<const Sample>(ch.data() + pos, curr_len));

    return static_cast<float>(flux / static_cast<double>(win));
}

float Segmenter::compute_zero_crossing_rate(const SampleVec& ch, uint64_t pos, uint32_t win) const noexcept {
    uint64_t total = ch.size();
    uint64_t prev_start = (pos >= win) ? pos - win : 0;
    uint64_t prev_len   = pos - prev_start;
    uint64_t curr_len   = (pos < total) ? std::min<uint64_t>(win, total - pos) : 0;
    if (prev_len < 2 || curr_len < 2) return 0.0f;

    double z_prev = FeatureExtractor::zero_crossing_rate(
        std::span<const Sample>(ch.data() + prev_start, prev_len));
    double z_curr = FeatureExtractor::zero_crossing_rate(
        std::span<const Sample>(ch.data() + pos, curr_len));

    return static_cast<float>(std::abs(z_curr - z_prev));
}

float Segmenter::compute_local_entropy(const SampleVec& ch, uint64_t pos, uint32_t win) const noexcept {
    uint64_t total = ch.size();
    uint64_t len = (pos < total) ? std::min<uint64_t>(win, total - pos) : 0;
    if (len == 0) return 0.0f;
    return static_cast<float>(FeatureExtractor::local_entropy(
        std::span<const Sample>(ch.data() + pos, len)));
}

bool Segmenter::is_boundary(float ec, float sf, float zcr, float le) const noexcept {
    (void)le; // 現状は energy / spectral flux / zcr の組み合わせで判定する
    return (ec  > cfg_.energy_threshold) ||
           (sf  > cfg_.spectral_flux_threshold) ||
           (zcr > 0.2f);
}

std::vector<Segment> Segmenter::segment(const AudioBuffer& buf) const {
    std::vector<Segment> segments;
    if (!buf.valid() || buf.spec.num_samples == 0) return segments;

    const uint64_t total = buf.spec.num_samples;

    // 解析用にチャンネルを平均化したモノラル信号を作成する
    SampleVec mono(total);
    const size_t num_ch = buf.channels.size();
    for (uint64_t i = 0; i < total; ++i) {
        Sample sum = 0;
        for (const auto& ch : buf.channels) sum += ch[i];
        mono[i] = sum / static_cast<Sample>(num_ch);
    }

    // 解析窓の大きさ: min_block_size を基本単位とする
    const uint32_t analysis_win = std::max<uint32_t>(64, cfg_.min_block_size / 4);

    uint64_t seg_start = 0;
    while (seg_start < total) {
        uint64_t pos = seg_start + std::max<uint64_t>(cfg_.min_block_size, analysis_win);

        while (pos < total) {
            uint64_t cur_len = pos - seg_start;
            if (cur_len >= cfg_.max_block_size) break;

            float ec  = compute_energy_change(mono, pos, analysis_win);
            float sf  = compute_spectral_flux(mono, pos, analysis_win);
            float zcr = compute_zero_crossing_rate(mono, pos, analysis_win);
            float le  = compute_local_entropy(mono, pos, analysis_win);

            if (is_boundary(ec, sf, zcr, le)) break;

            pos += analysis_win;
        }

        uint64_t end = std::min<uint64_t>(pos, total);
        uint64_t length64 = end - seg_start;
        if (length64 > cfg_.max_block_size) length64 = cfg_.max_block_size;
        if (length64 == 0) length64 = std::min<uint64_t>(total - seg_start, cfg_.min_block_size);

        Segment seg;
        seg.offset = seg_start;
        seg.length = static_cast<uint32_t>(length64);
        seg.local_entropy = compute_local_entropy(mono, seg_start, seg.length);
        segments.push_back(seg);

        seg_start += length64;
    }

    return segments;
}

} // namespace ulacr
