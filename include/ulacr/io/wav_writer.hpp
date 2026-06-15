#pragma once
#include "ulacr/core/types.hpp"
#include <filesystem>

namespace ulacr::io {

// ---------------------------------------------------------------
// WavWriter
//
// AudioBuffer を標準的な RIFF/WAVE ファイルへ書き出す。
// 4GB を超えるデータの場合は自動的に RF64 形式で書き出す。
// ---------------------------------------------------------------
class WavWriter {
public:
    static Error write(const std::filesystem::path& path, const AudioBuffer& buf);

    /// RAW PCM として書き出す（ヘッダなし）
    static Error write_raw(const std::filesystem::path& path, const AudioBuffer& buf);
};

} // namespace ulacr::io
