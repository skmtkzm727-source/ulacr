#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace ulacr::coding {

// Deterministic adaptive order-0 arithmetic/range coder.
// Counts start at one (Laplace prior) and are rescaled at a fixed total.
class AdaptiveRangeEncoder {
public:
    std::vector<uint8_t> encode(std::span<const uint32_t> symbols,
                                uint32_t alphabet_size) const;
};

class AdaptiveRangeDecoder {
public:
    std::vector<uint32_t> decode(std::span<const uint8_t> bytes,
                                 uint32_t alphabet_size,
                                 uint64_t symbol_count) const;
};

// 0.3 ensemble model: adaptive histogram, previous-symbol conditional model,
// integer logistic-like, Laplace-like, Student-t-like and uniform priors.
// Weights are Bayesian-updated using integer likelihoods.
class AdaptiveMixtureRangeEncoder {
public:
    std::vector<uint8_t> encode(
        std::span<const uint32_t> symbols,
        uint32_t alphabet_size,
        const std::function<void(float)>& progress = {}) const;
};

class AdaptiveMixtureRangeDecoder {
public:
    std::vector<uint32_t> decode(std::span<const uint8_t> bytes,
                                 uint32_t alphabet_size,
                                 uint64_t symbol_count) const;
};

} // namespace ulacr::coding
