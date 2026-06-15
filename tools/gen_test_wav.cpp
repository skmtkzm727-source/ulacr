#include "ulacr/io/wav_writer.hpp"
#include <cmath>
#include <random>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) { std::cerr << "usage: gen_wav <output.wav>\n"; return 1; }

    ulacr::AudioBuffer buf;
    buf.spec.sample_rate   = 44100;
    buf.spec.num_channels  = 2;
    buf.spec.num_samples   = 44100 * 2; // 2秒
    buf.spec.format        = ulacr::SampleFormat::Int16;
    buf.spec.source_format = ulacr::SourceFormat::WAV;

    buf.channels.assign(2, ulacr::SampleVec(buf.spec.num_samples));

    std::mt19937 rng(99);
    std::uniform_int_distribution<int> noise(-200, 200);

    for (uint32_t c = 0; c < 2; ++c) {
        for (uint64_t i = 0; i < buf.spec.num_samples; ++i) {
            double t = static_cast<double>(i) / buf.spec.sample_rate;
            double f1 = 440.0 * (c + 1);
            double f2 = 880.0;
            double v = 8000.0 * std::sin(2 * M_PI * f1 * t)
                     + 3000.0 * std::sin(2 * M_PI * f2 * t)
                     + noise(rng);
            // 静寂区間を挿入（Residual Island の効果確認用）
            if (i > 20000 && i < 25000) v = 0;
            buf.channels[c][i] = static_cast<ulacr::Sample>(v);
        }
    }

    auto err = ulacr::io::WavWriter::write(argv[1], buf);
    if (err != ulacr::Error::OK) { std::cerr << "write failed\n"; return 1; }
    std::cout << "wrote " << argv[1] << "\n";
    return 0;
}
