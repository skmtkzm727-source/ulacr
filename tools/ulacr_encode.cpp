#include "ulacr/encoder.hpp"
#include <iostream>
#include <string>
#include <vector>
namespace {
void usage() {
    std::cerr << "Usage: ulacr-encode [--profile classic|ultra|auto] <input.wav> <output.ulacr>\n";
}
}
int main(int argc, char* argv[]) {
    ulacr::EncoderProfile profile = ulacr::EncoderProfile::Classic;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--profile") {
            if (i + 1 >= argc) { usage(); return 1; }
            const std::string value = argv[++i];
            if (value == "classic") profile = ulacr::EncoderProfile::Classic;
            else if (value == "ultra") profile = ulacr::EncoderProfile::Ultra;
            else if (value == "auto") profile = ulacr::EncoderProfile::Auto;
            else { std::cerr << "Unknown profile: " << value << "\n"; usage(); return 1; }
        } else if (arg == "--help" || arg == "-h") { usage(); return 0; }
        else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n"; usage(); return 1;
        } else positional.push_back(arg);
    }
    if (positional.size() != 2) { usage(); return 1; }
    ulacr::CodecConfig cfg;
    cfg.profile = profile;
    if (profile != ulacr::EncoderProfile::Classic) cfg.max_predictor_order = 16;
    ulacr::Encoder enc(cfg);
    enc.set_progress_callback([](float p, std::string_view phase) {
        std::cout << "\r[" << phase << "] " << int(p * 100) << "%" << std::flush;
    });
    auto err = enc.encode_file(positional[0], positional[1]);
    std::cout << "\n";
    if (err != ulacr::Error::OK) {
        std::cerr << "Encode failed: " << static_cast<int>(err) << "\n";
        return 1;
    }
    std::cout << "Done: " << positional[1] << "\n";
    return 0;
}
