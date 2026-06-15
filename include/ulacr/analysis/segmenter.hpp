#pragma once
#include "ulacr/core/types.hpp"
#include "ulacr/core/codec_config.hpp"
#include <vector>

namespace ulacr {

// ---------------------------------------------------------------
// セグメント（可変長ブロック）
// ---------------------------------------------------------------
struct Segment {
    uint64_t offset;       // チャンネルあたりのサンプルオフセット
    uint32_t length;       // サンプル数
    float    local_entropy; // 局所エントロピー推定値（デバッグ用）
};

// ---------------------------------------------------------------
// 適応的セグメンテーション
//
// 役割:
//   AudioBuffer を信号特性に応じた可変長 Segment 列へ分割する。
//   境界決定には以下の指標を使用する:
//     - エネルギー変化量
//     - スペクトルフラックス
//     - ゼロ交差率
//     - 局所エントロピー
// ---------------------------------------------------------------
class Segmenter {
public:
    explicit Segmenter(const CodecConfig& cfg) noexcept;

    /// AudioBuffer 全体をセグメント列に分割して返す
    std::vector<Segment> segment(const AudioBuffer& buf) const;

private:
    float compute_energy_change(const SampleVec& ch,
                                uint64_t pos, uint32_t win) const noexcept;
    float compute_spectral_flux(const SampleVec& ch,
                                uint64_t pos, uint32_t win) const noexcept;
    float compute_zero_crossing_rate(const SampleVec& ch,
                                     uint64_t pos, uint32_t win) const noexcept;
    float compute_local_entropy(const SampleVec& ch,
                                uint64_t pos, uint32_t win) const noexcept;
    bool  is_boundary(float ec, float sf, float zcr, float le) const noexcept;

    const CodecConfig& cfg_;
};

} // namespace ulacr
