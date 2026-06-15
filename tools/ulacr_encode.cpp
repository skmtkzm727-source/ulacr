#include "ulacr/encoder.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: ulacr-encode <input.wav> <output.ulacr>\n";
        return 1;
    }
    ulacr::Encoder enc;
    enc.set_progress_callback([](float p, std::string_view phase) {
        std::cout << "\r[" << phase << "] " << int(p * 100) << "%" << std::flush;
    });

    auto err = enc.encode_file(argv[1], argv[2]);
    std::cout << "\n";
    if (err != ulacr::Error::OK) {
        std::cerr << "Encode failed: " << static_cast<int>(err) << "\n";
        return 1;
    }
    std::cout << "Done: " << argv[2] << "\n";
    return 0;
}
