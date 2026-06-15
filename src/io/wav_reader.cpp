#include "ulacr/io/wav_reader.hpp"
#include <fstream>
#include <cstring>
#include <array>

namespace ulacr::io {

namespace {

constexpr uint16_t WAVE_FORMAT_PCM        = 0x0001;
constexpr uint16_t WAVE_FORMAT_IEEE_FLOAT = 0x0003;
constexpr uint16_t WAVE_FORMAT_EXTENSIBLE = 0xFFFE;

uint32_t read_u32le(std::istream& is) {
    std::array<unsigned char, 4> b{};
    is.read(reinterpret_cast<char*>(b.data()), 4);
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
}

uint64_t read_u64le(std::istream& is) {
    std::array<unsigned char, 8> b{};
    is.read(reinterpret_cast<char*>(b.data()), 8);
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | b[i];
    return v;
}

uint16_t read_u16le(std::istream& is) {
    std::array<unsigned char, 2> b{};
    is.read(reinterpret_cast<char*>(b.data()), 2);
    return static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
}

std::string read_tag(std::istream& is) {
    char tag[4];
    is.read(tag, 4);
    return std::string(tag, 4);
}

/// fmt チャンク情報
struct FmtChunk {
    uint16_t format_tag      = WAVE_FORMAT_PCM;
    uint16_t num_channels    = 2;
    uint32_t sample_rate     = 44100;
    uint32_t byte_rate       = 0;
    uint16_t block_align     = 0;
    uint16_t bits_per_sample = 16;
    uint16_t valid_bits      = 0; // extensible のみ
};

SampleFormat resolve_format(const FmtChunk& fmt) {
    bool is_float = (fmt.format_tag == WAVE_FORMAT_IEEE_FLOAT);
    switch (fmt.bits_per_sample) {
        case 16: return SampleFormat::Int16;
        case 24: return SampleFormat::Int24;
        case 32: return is_float ? SampleFormat::Float32 : SampleFormat::Int32;
        case 64: return SampleFormat::Float64;
        default: return SampleFormat::Int16;
    }
}

/// 1サンプル(1チャンネル分)を Sample(int64) へビット保存変換する。
/// 整数PCM: 値そのもの（符号拡張）
/// 浮動小数点: ビットパターンを整数として再解釈（完全可逆のため）
Sample decode_one_sample(const unsigned char* p, SampleFormat fmt) {
    switch (fmt) {
        case SampleFormat::Int16: {
            int16_t v;
            std::memcpy(&v, p, 2);
            return static_cast<Sample>(v);
        }
        case SampleFormat::Int24: {
            int32_t v = (static_cast<int32_t>(p[0])) |
                        (static_cast<int32_t>(p[1]) << 8) |
                        (static_cast<int32_t>(p[2]) << 16);
            // 符号拡張 (24bit -> 32bit)
            if (v & 0x00800000) v |= 0xFF000000;
            return static_cast<Sample>(v);
        }
        case SampleFormat::Int32: {
            int32_t v;
            std::memcpy(&v, p, 4);
            return static_cast<Sample>(v);
        }
        case SampleFormat::Float32: {
            uint32_t bits;
            std::memcpy(&bits, p, 4);
            return static_cast<Sample>(static_cast<int32_t>(bits));
        }
        case SampleFormat::Float64: {
            uint64_t bits;
            std::memcpy(&bits, p, 8);
            return static_cast<Sample>(static_cast<int64_t>(bits));
        }
    }
    return 0;
}

} // namespace

// ---------------------------------------------------------------
// probe / read 共通: チャンク解析
// ---------------------------------------------------------------
namespace {

struct ParsedHeader {
    FmtChunk fmt;
    uint64_t data_offset = 0;
    uint64_t data_size   = 0; // バイト数
    bool     ok          = false;
};

ParsedHeader parse_riff(std::ifstream& f) {
    ParsedHeader result;

    std::string riff_tag = read_tag(f);
    uint32_t riff_size32 = read_u32le(f);
    std::string wave_tag = read_tag(f);

    bool is_rf64 = (riff_tag == "RF64");
    if (riff_tag != "RIFF" && !is_rf64) return result;
    if (wave_tag != "WAVE") return result;

    uint64_t override_data_size = 0;
    (void)riff_size32;

    while (f.good()) {
        std::string chunk_id = read_tag(f);
        if (f.eof() || chunk_id.size() < 4) break;
        uint32_t chunk_size = read_u32le(f);
        std::streampos chunk_data_pos = f.tellg();

        if (chunk_id == "ds64") {
            // RF64: 64bit サイズ情報
            uint64_t riff_size64 = read_u64le(f);
            uint64_t data_size64 = read_u64le(f);
            (void)riff_size64;
            override_data_size = data_size64;
            // sampleCount64 等は読み飛ばす
            f.seekg(chunk_data_pos + std::streamoff(chunk_size));
        } else if (chunk_id == "fmt ") {
            FmtChunk fmt;
            fmt.format_tag      = read_u16le(f);
            fmt.num_channels    = read_u16le(f);
            fmt.sample_rate     = read_u32le(f);
            fmt.byte_rate       = read_u32le(f);
            fmt.block_align     = read_u16le(f);
            fmt.bits_per_sample = read_u16le(f);

            if (fmt.format_tag == WAVE_FORMAT_EXTENSIBLE && chunk_size >= 40) {
                uint16_t cb_size = read_u16le(f);
                (void)cb_size;
                fmt.valid_bits = read_u16le(f);
                // チャンネルマスク (4 bytes)
                read_u32le(f);
                // SubFormat GUID の先頭2バイトが実フォーマット
                uint16_t sub_fmt = read_u16le(f);
                fmt.format_tag = sub_fmt;
                // GUID 残り 14 バイトは読み飛ばす
                f.seekg(chunk_data_pos + std::streamoff(chunk_size));
            } else {
                f.seekg(chunk_data_pos + std::streamoff(chunk_size));
            }
            result.fmt = fmt;
        } else if (chunk_id == "data") {
            result.data_offset = static_cast<uint64_t>(chunk_data_pos);
            result.data_size   = (chunk_size == 0xFFFFFFFFu && override_data_size > 0)
                                      ? override_data_size
                                      : chunk_size;
            result.ok = true;
            // data チャンク以降も他チャンクがあるが、読み取りには不要なので終了
            break;
        } else {
            // 未知のチャンクはスキップ（パディング含む）
            uint32_t padded = chunk_size + (chunk_size & 1);
            f.seekg(chunk_data_pos + std::streamoff(padded));
        }

        if (!f.good()) break;
    }

    return result;
}

} // namespace

Error WavReader::probe(const std::filesystem::path& path, AudioSpec& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return Error::IOError;

    auto hdr = parse_riff(f);
    if (!hdr.ok) return Error::UnsupportedFormat;

    out.sample_rate    = hdr.fmt.sample_rate;
    out.num_channels   = hdr.fmt.num_channels;
    out.format         = resolve_format(hdr.fmt);
    out.source_format  = SourceFormat::WAV;

    AudioSpec tmp = out;
    uint32_t bps  = tmp.bytes_per_sample();
    if (bps == 0 || hdr.fmt.num_channels == 0) return Error::UnsupportedFormat;

    uint64_t frame_size = static_cast<uint64_t>(bps) * hdr.fmt.num_channels;
    out.num_samples = (frame_size > 0) ? (hdr.data_size / frame_size) : 0;
    return Error::OK;
}

Error WavReader::read(const std::filesystem::path& path, AudioBuffer& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return Error::IOError;

    auto hdr = parse_riff(f);
    if (!hdr.ok) return Error::UnsupportedFormat;

    AudioSpec spec;
    spec.sample_rate   = hdr.fmt.sample_rate;
    spec.num_channels  = hdr.fmt.num_channels;
    spec.format        = resolve_format(hdr.fmt);
    spec.source_format = SourceFormat::WAV;

    uint32_t bps = spec.bytes_per_sample();
    if (bps == 0 || spec.num_channels == 0) return Error::UnsupportedFormat;

    uint64_t frame_size = static_cast<uint64_t>(bps) * spec.num_channels;
    uint64_t num_frames = (frame_size > 0) ? (hdr.data_size / frame_size) : 0;
    spec.num_samples = num_frames;

    out.spec = spec;
    out.channels.assign(spec.num_channels, SampleVec(num_frames));

    f.seekg(static_cast<std::streamoff>(hdr.data_offset));

    // チャンク単位で読み込み（メモリ効率のため一定サイズずつ）
    constexpr uint64_t FRAMES_PER_CHUNK = 65536;
    std::vector<unsigned char> raw(static_cast<size_t>(FRAMES_PER_CHUNK * frame_size));

    uint64_t frames_read = 0;
    while (frames_read < num_frames) {
        uint64_t this_chunk = std::min<uint64_t>(FRAMES_PER_CHUNK, num_frames - frames_read);
        size_t bytes_to_read = static_cast<size_t>(this_chunk * frame_size);
        f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(bytes_to_read));
        std::streamsize got = f.gcount();
        if (got <= 0) break;
        uint64_t got_frames = static_cast<uint64_t>(got) / frame_size;

        for (uint64_t i = 0; i < got_frames; ++i) {
            const unsigned char* frame_ptr = raw.data() + i * frame_size;
            for (uint32_t c = 0; c < spec.num_channels; ++c) {
                const unsigned char* sp = frame_ptr + c * bps;
                out.channels[c][frames_read + i] = decode_one_sample(sp, spec.format);
            }
        }
        frames_read += got_frames;
        if (got_frames < this_chunk) break; // 早期EOF
    }

    // 実際に読めたフレーム数に合わせて切り詰め
    if (frames_read < num_frames) {
        for (auto& ch : out.channels) ch.resize(frames_read);
        out.spec.num_samples = frames_read;
    }

    return Error::OK;
}

Error WavReader::read_raw(const std::filesystem::path& path,
                          const AudioSpec&             spec_in,
                          AudioBuffer&                 out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return Error::IOError;

    std::streamoff file_size = f.tellg();
    f.seekg(0);

    AudioSpec spec = spec_in;
    spec.source_format = SourceFormat::RAW;
    uint32_t bps = spec.bytes_per_sample();
    if (bps == 0 || spec.num_channels == 0) return Error::InvalidArg;

    uint64_t frame_size = static_cast<uint64_t>(bps) * spec.num_channels;
    uint64_t num_frames = static_cast<uint64_t>(file_size) / frame_size;
    if (spec.num_samples != 0) {
        num_frames = std::min<uint64_t>(num_frames, spec.num_samples);
    }
    spec.num_samples = num_frames;

    out.spec = spec;
    out.channels.assign(spec.num_channels, SampleVec(num_frames));

    constexpr uint64_t FRAMES_PER_CHUNK = 65536;
    std::vector<unsigned char> raw(static_cast<size_t>(FRAMES_PER_CHUNK * frame_size));

    uint64_t frames_read = 0;
    while (frames_read < num_frames) {
        uint64_t this_chunk = std::min<uint64_t>(FRAMES_PER_CHUNK, num_frames - frames_read);
        size_t bytes_to_read = static_cast<size_t>(this_chunk * frame_size);
        f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(bytes_to_read));
        std::streamsize got = f.gcount();
        if (got <= 0) break;
        uint64_t got_frames = static_cast<uint64_t>(got) / frame_size;

        for (uint64_t i = 0; i < got_frames; ++i) {
            const unsigned char* frame_ptr = raw.data() + i * frame_size;
            for (uint32_t c = 0; c < spec.num_channels; ++c) {
                const unsigned char* sp = frame_ptr + c * bps;
                out.channels[c][frames_read + i] = decode_one_sample(sp, spec.format);
            }
        }
        frames_read += got_frames;
        if (got_frames < this_chunk) break;
    }

    if (frames_read < num_frames) {
        for (auto& ch : out.channels) ch.resize(frames_read);
        out.spec.num_samples = frames_read;
    }

    return Error::OK;
}

} // namespace ulacr::io
