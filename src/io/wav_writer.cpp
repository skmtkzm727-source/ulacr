#include "ulacr/io/wav_writer.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>

namespace ulacr::io {

namespace {

constexpr uint16_t WAVE_FORMAT_PCM        = 0x0001;
constexpr uint16_t WAVE_FORMAT_IEEE_FLOAT = 0x0003;

void write_u32le(std::ostream& os, uint32_t v) {
    unsigned char b[4] = {
        static_cast<unsigned char>(v & 0xFF),
        static_cast<unsigned char>((v >> 8) & 0xFF),
        static_cast<unsigned char>((v >> 16) & 0xFF),
        static_cast<unsigned char>((v >> 24) & 0xFF)
    };
    os.write(reinterpret_cast<char*>(b), 4);
}

void write_u64le(std::ostream& os, uint64_t v) {
    unsigned char b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
    os.write(reinterpret_cast<char*>(b), 8);
}

void write_u16le(std::ostream& os, uint16_t v) {
    unsigned char b[2] = {
        static_cast<unsigned char>(v & 0xFF),
        static_cast<unsigned char>((v >> 8) & 0xFF)
    };
    os.write(reinterpret_cast<char*>(b), 2);
}

void write_tag(std::ostream& os, const char* tag) {
    os.write(tag, 4);
}

/// Sample(int64) を出力バイト列へエンコードする（decode_one_sample の逆）
void encode_one_sample(Sample s, SampleFormat fmt, unsigned char* out) {
    switch (fmt) {
        case SampleFormat::Int16: {
            int16_t v = static_cast<int16_t>(s);
            std::memcpy(out, &v, 2);
            break;
        }
        case SampleFormat::Int24: {
            int32_t v = static_cast<int32_t>(s);
            out[0] = static_cast<unsigned char>(v & 0xFF);
            out[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
            out[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
            break;
        }
        case SampleFormat::Int32: {
            int32_t v = static_cast<int32_t>(s);
            std::memcpy(out, &v, 4);
            break;
        }
        case SampleFormat::Float32: {
            uint32_t bits = static_cast<uint32_t>(static_cast<int32_t>(s));
            std::memcpy(out, &bits, 4);
            break;
        }
        case SampleFormat::Float64: {
            uint64_t bits = static_cast<uint64_t>(s);
            std::memcpy(out, &bits, 8);
            break;
        }
    }
}

uint16_t format_tag_for(SampleFormat fmt) {
    switch (fmt) {
        case SampleFormat::Float32:
        case SampleFormat::Float64:
            return WAVE_FORMAT_IEEE_FLOAT;
        default:
            return WAVE_FORMAT_PCM;
    }
}

} // namespace

Error WavWriter::write(const std::filesystem::path& path, const AudioBuffer& buf) {
    if (!buf.valid()) return Error::InvalidArg;

    const AudioSpec& spec = buf.spec;
    uint32_t bps          = spec.bytes_per_sample();
    if (bps == 0 || spec.num_channels == 0) return Error::InvalidArg;

    uint64_t frame_size = static_cast<uint64_t>(bps) * spec.num_channels;
    uint64_t data_size  = frame_size * spec.num_samples;

    bool need_rf64 = (data_size + 44) > 0xFFFFFFFFull;

    std::ofstream f(path, std::ios::binary);
    if (!f) return Error::IOError;

    uint16_t bits_per_sample = static_cast<uint16_t>(spec.bit_depth());
    uint16_t format_tag      = format_tag_for(spec.format);
    uint32_t byte_rate       = spec.sample_rate * static_cast<uint32_t>(frame_size);
    uint16_t block_align     = static_cast<uint16_t>(frame_size);

    if (!need_rf64) {
        uint32_t riff_size = static_cast<uint32_t>(36 + data_size);
        write_tag(f, "RIFF");
        write_u32le(f, riff_size);
        write_tag(f, "WAVE");

        write_tag(f, "fmt ");
        write_u32le(f, 16); // fmt chunk size
        write_u16le(f, format_tag);
        write_u16le(f, static_cast<uint16_t>(spec.num_channels));
        write_u32le(f, spec.sample_rate);
        write_u32le(f, byte_rate);
        write_u16le(f, block_align);
        write_u16le(f, bits_per_sample);

        write_tag(f, "data");
        write_u32le(f, static_cast<uint32_t>(data_size));
    } else {
        // RF64 / BW64
        write_tag(f, "RF64");
        write_u32le(f, 0xFFFFFFFFu); // size unknown -> use ds64
        write_tag(f, "WAVE");

        write_tag(f, "ds64");
        write_u32le(f, 28); // ds64 chunk size
        write_u64le(f, 36 + data_size); // riffSize
        write_u64le(f, data_size);      // dataSize
        write_u64le(f, spec.num_samples); // sampleCount
        write_u32le(f, 0); // table length

        write_tag(f, "fmt ");
        write_u32le(f, 16);
        write_u16le(f, format_tag);
        write_u16le(f, static_cast<uint16_t>(spec.num_channels));
        write_u32le(f, spec.sample_rate);
        write_u32le(f, byte_rate);
        write_u16le(f, block_align);
        write_u16le(f, bits_per_sample);

        write_tag(f, "data");
        write_u32le(f, 0xFFFFFFFFu); // size in ds64
    }

    // データ本体（チャンク単位で書き出し）
    constexpr uint64_t FRAMES_PER_CHUNK = 65536;
    std::vector<unsigned char> raw(static_cast<size_t>(FRAMES_PER_CHUNK * frame_size));

    uint64_t frames_written = 0;
    while (frames_written < spec.num_samples) {
        uint64_t this_chunk = std::min<uint64_t>(FRAMES_PER_CHUNK, spec.num_samples - frames_written);
        for (uint64_t i = 0; i < this_chunk; ++i) {
            unsigned char* frame_ptr = raw.data() + i * frame_size;
            for (uint32_t c = 0; c < spec.num_channels; ++c) {
                Sample s = buf.channels[c][frames_written + i];
                encode_one_sample(s, spec.format, frame_ptr + c * bps);
            }
        }
        f.write(reinterpret_cast<char*>(raw.data()),
                static_cast<std::streamsize>(this_chunk * frame_size));
        frames_written += this_chunk;
    }

    // パディングバイト（奇数サイズの場合）
    if (!need_rf64 && (data_size & 1)) {
        char zero = 0;
        f.write(&zero, 1);
    }

    return f.good() ? Error::OK : Error::IOError;
}

Error WavWriter::write_raw(const std::filesystem::path& path, const AudioBuffer& buf) {
    if (!buf.valid()) return Error::InvalidArg;

    const AudioSpec& spec = buf.spec;
    uint32_t bps          = spec.bytes_per_sample();
    if (bps == 0 || spec.num_channels == 0) return Error::InvalidArg;

    std::ofstream f(path, std::ios::binary);
    if (!f) return Error::IOError;

    uint64_t frame_size = static_cast<uint64_t>(bps) * spec.num_channels;
    constexpr uint64_t FRAMES_PER_CHUNK = 65536;
    std::vector<unsigned char> raw(static_cast<size_t>(FRAMES_PER_CHUNK * frame_size));

    uint64_t frames_written = 0;
    while (frames_written < spec.num_samples) {
        uint64_t this_chunk = std::min<uint64_t>(FRAMES_PER_CHUNK, spec.num_samples - frames_written);
        for (uint64_t i = 0; i < this_chunk; ++i) {
            unsigned char* frame_ptr = raw.data() + i * frame_size;
            for (uint32_t c = 0; c < spec.num_channels; ++c) {
                Sample s = buf.channels[c][frames_written + i];
                encode_one_sample(s, spec.format, frame_ptr + c * bps);
            }
        }
        f.write(reinterpret_cast<char*>(raw.data()),
                static_cast<std::streamsize>(this_chunk * frame_size));
        frames_written += this_chunk;
    }

    return f.good() ? Error::OK : Error::IOError;
}

} // namespace ulacr::io
