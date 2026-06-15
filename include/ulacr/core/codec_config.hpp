#pragma once
#include <cstdint>

namespace ulacr {

// ---------------------------------------------------------------
// エンコード設定
// ---------------------------------------------------------------
struct CodecConfig {

    // --- セグメンテーション ---
    uint32_t min_block_size = 256;    // 最小ブロックサイズ（サンプル数）
    uint32_t max_block_size = 65536;  // 最大ブロックサイズ
    float    energy_threshold  = 0.02f; // エネルギー変化検出閾値
    float    spectral_flux_threshold = 0.05f;

    // --- Block Graph ---
    bool     enable_block_graph = true;
    float    similarity_threshold = 0.92f; // 類似ブロック接続閾値
    uint32_t lsh_num_tables   = 8;
    uint32_t lsh_num_bits     = 12;

    // --- Spectral Dictionary ---
    bool     enable_spectral_dict = true;
    uint32_t dict_size         = 1024;  // 辞書要素数 (512〜4096)
    uint32_t dict_atom_size    = 256;   // 辞書原子のサイズ（周波数ビン数）
    uint32_t ksvd_iterations   = 20;

    // --- Wavelet ---
    bool     enable_wavelet    = true;
    uint32_t wavelet_levels    = 6;     // ウェーブレット分解段数

    // --- Harmonic Modeling ---
    bool     enable_harmonic   = true;
    uint32_t max_harmonics     = 32;

    // --- Residual Island ---
    bool     enable_residual_island = true;

    // --- Bitplane Decomposition ---
    bool     enable_bitplane   = true;

    // --- Context Model ---
    uint32_t context_history_len = 4;   // 残差履歴参照数

    // デフォルト：最高圧縮率プリセット
    static CodecConfig best_compression() noexcept;
    // 高速プリセット（一部機能無効）
    static CodecConfig fast() noexcept;
};

} // namespace ulacr
