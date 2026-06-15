#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include <span>
#include <string_view>

namespace ulacr {

// ---------------------------------------------------------------
// バージョン
// ---------------------------------------------------------------
inline constexpr uint32_t ULACR_VERSION_MAJOR = 0;
inline constexpr uint32_t ULACR_VERSION_MINOR = 1;
inline constexpr uint32_t ULACR_MAGIC = 0x554C4352; // 'ULCR'

// ---------------------------------------------------------------
// サンプル型
// ---------------------------------------------------------------
enum class SampleFormat : uint8_t {
    Int16   = 0,
    Int24   = 1,
    Int32   = 2,
    Float32 = 3,
    Float64 = 4,
};

// ---------------------------------------------------------------
// 入力ファイル種別
// ---------------------------------------------------------------
enum class SourceFormat : uint8_t {
    WAV  = 0,
    RF64 = 1,
    AIFF = 2,
    RAW  = 3,
};

// ---------------------------------------------------------------
// PCM音声仕様
// ---------------------------------------------------------------
struct AudioSpec {
    uint32_t    sample_rate    = 44100;
    uint32_t    num_channels   = 2;
    uint64_t    num_samples    = 0;   // チャンネルあたりのサンプル数
    SampleFormat format        = SampleFormat::Int16;
    SourceFormat source_format = SourceFormat::WAV;

    /// 1サンプルのバイト数
    uint32_t bytes_per_sample() const noexcept;
    /// ビット深度
    uint32_t bit_depth() const noexcept;
};

// ---------------------------------------------------------------
// 整数サンプル（内部処理は 64bit 整数に正規化）
// ---------------------------------------------------------------
using Sample   = int64_t;
using SampleVec = std::vector<Sample>;

// ---------------------------------------------------------------
// チャンネル別サンプル列
// ---------------------------------------------------------------
struct AudioBuffer {
    AudioSpec           spec;
    std::vector<SampleVec> channels; // [channel][sample]

    bool valid() const noexcept {
        return !channels.empty() && channels.size() == spec.num_channels;
    }
};

// ---------------------------------------------------------------
// エラーコード
// ---------------------------------------------------------------
enum class Error : int32_t {
    OK               =  0,
    InvalidArg       = -1,
    UnsupportedFormat= -2,
    IOError          = -3,
    CorruptedData    = -4,
    OutOfMemory      = -5,
};

} // namespace ulacr
