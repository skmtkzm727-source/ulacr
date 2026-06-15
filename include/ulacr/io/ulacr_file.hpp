#pragma once
#include "ulacr/core/types.hpp"
#include "ulacr/core/codec_config.hpp"
#include "ulacr/io/bitstream.hpp"
#include <cstdint>

namespace ulacr::io {

// ---------------------------------------------------------------
// .ulacr ファイル構造（概要）
//
//   [Magic "ULCR"][Version]
//   [AudioSpec]
//   [DecodeFlags]            ... デコードに必要な設定値
//   [Segments]                ... 共通セグメント長一覧
//   [BlockGraph edges]        ... (enable_block_graph時)
//   [Harmonic models]         ... (enable_harmonic時, 図参照)
//   [Subband metadata]        ... 各(channel,segment,subband)の
//                                  非ゼロ値数・ビットプレーン数
//   [Context groups]          ... rANS確率テーブル + 符号化データ
//   [Raw mantissa bitstream]  ... bucket方式の仮数ビット
//
// 本ヘッダはマジックナンバー・バージョン・基本仕様の
// 読み書きヘルパーのみを提供する。残りのセクションは
// Encoder/Decoder の実装内で直接 BitWriter/BitReader を用いて
// 読み書きされる（フォーマットの詳細は encoder.cpp/decoder.cpp 参照）。
// ---------------------------------------------------------------

/// デコードに必要なコーデック設定のサブセット
struct DecodeFlags {
    uint8_t  wavelet_levels      = 0;
    bool     enable_wavelet      = true;
    bool     enable_block_graph  = false;
    bool     enable_harmonic     = false;
    bool     enable_bitplane     = false;
    uint8_t  max_harmonics       = 0;
};

/// マジックナンバー + バージョンを書き込む
void write_file_magic(BitWriter& w);

/// マジックナンバー + バージョンを読み込み、検証する
/// 失敗時は BitstreamError を投げる
void read_and_check_file_magic(BitReader& r);

/// AudioSpec を書き込む
void write_audio_spec(BitWriter& w, const AudioSpec& spec);

/// AudioSpec を読み込む
AudioSpec read_audio_spec(BitReader& r);

/// デコードフラグを書き込む
void write_decode_flags(BitWriter& w, const DecodeFlags& flags);

/// デコードフラグを読み込む
DecodeFlags read_decode_flags(BitReader& r);

} // namespace ulacr::io
