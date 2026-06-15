#include "ulacr/core/types.hpp"

namespace ulacr {

uint32_t AudioSpec::bytes_per_sample() const noexcept {
    switch (format) {
        case SampleFormat::Int16:   return 2;
        case SampleFormat::Int24:   return 3;
        case SampleFormat::Int32:   return 4;
        case SampleFormat::Float32: return 4;
        case SampleFormat::Float64: return 8;
    }
    return 0;
}

uint32_t AudioSpec::bit_depth() const noexcept {
    switch (format) {
        case SampleFormat::Int16:   return 16;
        case SampleFormat::Int24:   return 24;
        case SampleFormat::Int32:   return 32;
        case SampleFormat::Float32: return 32;
        case SampleFormat::Float64: return 64;
    }
    return 0;
}

} // namespace ulacr
