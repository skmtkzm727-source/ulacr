#include "ulacr/encoder.hpp"
#include "ulacr/decoder.hpp"
#include <emscripten/emscripten.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::vector<std::uint8_t> output_bytes;

int process_file(const std::uint8_t* data, std::size_t size, bool decode) {
    output_bytes.clear();
    if (data == nullptr || size == 0) {
        return static_cast<int>(ulacr::Error::InvalidArg);
    }

    const char* input_path = decode ? "/input.ulacr" : "/input.wav";
    const char* output_path = decode ? "/output.wav" : "/output.ulacr";

    try {
        {
            std::ofstream input(input_path, std::ios::binary | std::ios::trunc);
            if (!input) return static_cast<int>(ulacr::Error::IOError);
            input.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
            if (!input.good()) return static_cast<int>(ulacr::Error::IOError);
        }

        ulacr::Error error;
        if (decode) {
            ulacr::Decoder decoder;
            error = decoder.decode_file(input_path, output_path);
        } else {
            ulacr::CodecConfig config = ulacr::CodecConfig::best_compression();
            ulacr::Encoder encoder(config);
            error = encoder.encode_file(input_path, output_path);
        }
        if (error != ulacr::Error::OK) return static_cast<int>(error);

        std::ifstream output(output_path, std::ios::binary | std::ios::ate);
        if (!output) return static_cast<int>(ulacr::Error::IOError);
        const std::streamoff end = output.tellg();
        if (end <= 0) return static_cast<int>(ulacr::Error::IOError);
        output_bytes.resize(static_cast<std::size_t>(end));
        output.seekg(0, std::ios::beg);
        output.read(reinterpret_cast<char*>(output_bytes.data()), end);
        if (!output) {
            output_bytes.clear();
            return static_cast<int>(ulacr::Error::IOError);
        }

        std::remove(input_path);
        std::remove(output_path);
        return static_cast<int>(ulacr::Error::OK);
    } catch (...) {
        output_bytes.clear();
        std::remove(input_path);
        std::remove(output_path);
        return static_cast<int>(ulacr::Error::OutOfMemory);
    }
}
} // namespace

extern "C" {
EMSCRIPTEN_KEEPALIVE int ulacr_encode(const std::uint8_t* data, std::size_t size) {
    return process_file(data, size, false);
}

EMSCRIPTEN_KEEPALIVE int ulacr_decode(const std::uint8_t* data, std::size_t size) {
    return process_file(data, size, true);
}

EMSCRIPTEN_KEEPALIVE std::uintptr_t ulacr_output_ptr() {
    return reinterpret_cast<std::uintptr_t>(output_bytes.data());
}

EMSCRIPTEN_KEEPALIVE std::size_t ulacr_output_size() {
    return output_bytes.size();
}

EMSCRIPTEN_KEEPALIVE void ulacr_clear_output() {
    std::vector<std::uint8_t>().swap(output_bytes);
}
}
