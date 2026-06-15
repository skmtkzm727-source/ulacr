#pragma once
#include "ulacr/core/types.hpp"
#include "ulacr/core/codec_config.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>

namespace ulacr {

// ---------------------------------------------------------------
// エンコード進捗コールバック
// ---------------------------------------------------------------
using ProgressCallback = std::function<void(float progress,   // 0.0〜1.0
                                            std::string_view phase)>;

// ---------------------------------------------------------------
// Encoder
//
// エンコードパイプライン（全コンポーネントの統括）:
//
//   AudioBuffer
//     └─► Segmenter          （適応的セグメンテーション）
//           └─► BlockGraphBuilder  （自己類似性グラフ構築）
//           └─► SpectralDictLearner（辞書学習）
//           └─► WaveletTransform   （マルチスケール変換）
//           └─► HarmonicAnalyzer   （倍音モデリング）  ← 将来
//           └─► ResidualIslandCoder（残差アイランド）
//           └─► BitplaneDecomposer （ビットプレーン分解）
//           └─► ContextModel + RansEncoder（エントロピー符号化）
//     └─► .ulacr ファイル出力
// ---------------------------------------------------------------
class Encoder {
public:
    explicit Encoder(const CodecConfig& cfg = {}) noexcept;
    ~Encoder();

    /// コールバックを登録（省略可）
    void set_progress_callback(ProgressCallback cb) noexcept;

    /// AudioBuffer をエンコードしてファイルに書き出す
    Error encode(const AudioBuffer&       buf,
                 const std::filesystem::path& output_path);

    /// 入力ファイルから直接エンコード（WAV/AIFF/RAW 対応）
    Error encode_file(const std::filesystem::path& input_path,
                      const std::filesystem::path& output_path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ulacr
