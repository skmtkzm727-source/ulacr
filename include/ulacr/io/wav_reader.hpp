#pragma once
#include "ulacr/core/types.hpp"
#include <filesystem>

namespace ulacr::io {

// ---------------------------------------------------------------
// WavReader
//
// 対応:
//   - WAV (RIFF/WAVE), RF64 (BW64) の基本サポート
//   - PCM (Int16/24/32) および IEEE Float (32/64) の 'fmt ' チャンク
//   - RAW PCM (拡張子 .raw / .pcm) はヘッダなしとして
//     呼び出し側が AudioSpec を明示的に与える必要がある
// ---------------------------------------------------------------
class WavReader {
public:
    /// WAV/RF64 ファイルを読み込み AudioBuffer に変換する
    static Error read(const std::filesystem::path& path, AudioBuffer& out);

    /// RAW PCM ファイルを読み込む（spec はあらかじめ呼び出し側で設定）
    static Error read_raw(const std::filesystem::path& path,
                          const AudioSpec&             spec,
                          AudioBuffer&                 out);

    /// ヘッダのみ読み込んで AudioSpec を取得する（高速プローブ）
    static Error probe(const std::filesystem::path& path, AudioSpec& out);
};

} // namespace ulacr::io
