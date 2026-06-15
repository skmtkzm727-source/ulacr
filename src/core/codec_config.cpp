#include "ulacr/core/codec_config.hpp"

namespace ulacr {

CodecConfig CodecConfig::best_compression() noexcept {
    CodecConfig c;
    c.min_block_size           = 256;
    c.max_block_size           = 65536;
    c.energy_threshold         = 0.02f;
    c.spectral_flux_threshold  = 0.05f;

    c.enable_block_graph       = true;
    c.similarity_threshold     = 0.92f;
    c.lsh_num_tables           = 8;
    c.lsh_num_bits             = 12;

    c.enable_spectral_dict     = true;
    c.dict_size                = 1024;
    c.dict_atom_size           = 256;
    c.ksvd_iterations          = 20;

    c.enable_wavelet           = true;
    c.wavelet_levels           = 6;

    c.enable_harmonic          = true;
    c.max_harmonics            = 32;

    c.enable_residual_island   = true;
    c.enable_bitplane          = true;

    c.context_history_len      = 4;
    return c;
}

CodecConfig CodecConfig::fast() noexcept {
    CodecConfig c;
    c.min_block_size           = 1024;
    c.max_block_size           = 8192;
    c.energy_threshold         = 0.05f;
    c.spectral_flux_threshold  = 0.1f;

    c.enable_block_graph       = false;
    c.lsh_num_tables           = 4;
    c.lsh_num_bits             = 10;

    c.enable_spectral_dict     = false;
    c.dict_size                = 512;
    c.dict_atom_size           = 128;
    c.ksvd_iterations          = 5;

    c.enable_wavelet           = true;
    c.wavelet_levels           = 3;

    c.enable_harmonic          = false;
    c.max_harmonics            = 8;

    c.enable_residual_island   = true;
    c.enable_bitplane          = false;

    c.context_history_len      = 2;
    return c;
}

} // namespace ulacr
