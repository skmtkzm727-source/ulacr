#pragma once
#include "ulacr/core/types.hpp"
#include "ulacr/core/codec_config.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>

namespace ulacr {

using ProgressCallback = std::function<void(float, std::string_view)>;

// ---------------------------------------------------------------
// Decoder
//
// デコードパイプライン（Encoder の逆順）:
//
//   .ulacr ファイル
//     └─► ヘッダ / 辞書 / グラフ / ウェーブレット / 残差 読み込み
//           └─► RansDecoder + ContextModel（エントロピー復号）
//           └─► BitplaneReconstructor
//           └─► ResidualIslandDecoder
//           └─► WaveletTransform::inverse
//           └─► SpectralDictLearner::decode
//           └─► BlockGraph 差分適用
//     └─► AudioBuffer（元 PCM と完全一致）
// ---------------------------------------------------------------
class Decoder {
public:
    Decoder();
    ~Decoder();

    void set_progress_callback(ProgressCallback cb) noexcept;

    /// .ulacr ファイルをデコードして AudioBuffer を返す
    Error decode(const std::filesystem::path& input_path,
                 AudioBuffer&                 buf_out);

    /// .ulacr → PCM ファイルへ直接書き出し
    Error decode_file(const std::filesystem::path& input_path,
                      const std::filesystem::path& output_path);

    /// ヘッダのみ読み込んで AudioSpec を返す（デコードは行わない）
    Error probe(const std::filesystem::path& input_path,
                AudioSpec&                   spec_out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ulacr
