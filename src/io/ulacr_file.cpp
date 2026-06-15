#include "ulacr/io/ulacr_file.hpp"

namespace ulacr::io {

void write_file_magic(BitWriter& w) {
    w.write_u32(ULACR_MAGIC);
    w.write_byte(static_cast<uint8_t>(ULACR_VERSION_MAJOR));
    w.write_byte(static_cast<uint8_t>(ULACR_VERSION_MINOR));
}

void read_and_check_file_magic(BitReader& r) {
    uint32_t magic = r.read_u32();
    if (magic != ULACR_MAGIC) {
        throw BitstreamError("invalid ULAC-R magic number");
    }
    uint8_t major = r.read_byte();
    uint8_t minor = r.read_byte();
    if (major != static_cast<uint8_t>(ULACR_VERSION_MAJOR)) {
        throw BitstreamError("unsupported ULAC-R major version");
    }
    (void)minor;
}

void write_audio_spec(BitWriter& w, const AudioSpec& spec) {
    w.write_u32(spec.sample_rate);
    w.write_u32(spec.num_channels);
    w.write_u64(spec.num_samples);
    w.write_byte(static_cast<uint8_t>(spec.format));
    w.write_byte(static_cast<uint8_t>(spec.source_format));
}

AudioSpec read_audio_spec(BitReader& r) {
    AudioSpec spec;
    spec.sample_rate   = r.read_u32();
    spec.num_channels  = r.read_u32();
    spec.num_samples   = r.read_u64();
    spec.format        = static_cast<SampleFormat>(r.read_byte());
    spec.source_format = static_cast<SourceFormat>(r.read_byte());
    return spec;
}

void write_decode_flags(BitWriter& w, const DecodeFlags& flags) {
    w.write_byte(flags.wavelet_levels);
    w.write_byte(flags.enable_wavelet     ? 1 : 0);
    w.write_byte(flags.enable_block_graph ? 1 : 0);
    w.write_byte(flags.enable_harmonic    ? 1 : 0);
    w.write_byte(flags.enable_bitplane    ? 1 : 0);
    w.write_byte(flags.max_harmonics);
}

DecodeFlags read_decode_flags(BitReader& r) {
    DecodeFlags flags;
    flags.wavelet_levels     = r.read_byte();
    flags.enable_wavelet     = r.read_byte() != 0;
    flags.enable_block_graph = r.read_byte() != 0;
    flags.enable_harmonic    = r.read_byte() != 0;
    flags.enable_bitplane    = r.read_byte() != 0;
    flags.max_harmonics      = r.read_byte();
    return flags;
}

} // namespace ulacr::io
