#pragma once
#include <cstdint>
namespace ulacr {
enum class EncoderProfile : uint8_t { Classic = 0, Ultra = 1, Auto = 2 };
struct CodecConfig {
    EncoderProfile profile = EncoderProfile::Classic;
    uint32_t min_block_size = 256;
    uint32_t max_block_size = 65536;
    float energy_threshold = 0.02f;
    float spectral_flux_threshold = 0.05f;
    bool enable_block_graph = true;
    float similarity_threshold = 0.92f;
    uint32_t lsh_num_tables = 8;
    uint32_t lsh_num_bits = 12;
    bool enable_spectral_dict = true;
    uint32_t dict_size = 1024;
    uint32_t dict_atom_size = 256;
    uint32_t ksvd_iterations = 20;
    bool enable_predictive_coding = true;
    bool enable_stereo_mid_side = true;
    bool enable_wavelet = true;
    uint32_t wavelet_levels = 6;
    bool enable_harmonic = true;
    uint32_t max_harmonics = 32;
    bool enable_residual_island = true;
    bool enable_bitplane = true;
    uint32_t context_history_len = 4;
    static CodecConfig best_compression() noexcept;
    static CodecConfig fast() noexcept;
};
} // namespace ulacr
