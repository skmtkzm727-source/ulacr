#include "ulacr/coding/range_coder.hpp"
#include <algorithm>
#include <stdexcept>
#include <numeric>
#include <array>
#include <limits>

namespace ulacr::coding {
namespace {
constexpr uint64_t TOP = 0xFFFFFFFFull;
constexpr uint64_t HALF = 0x80000000ull;
constexpr uint64_t FIRST_QTR = 0x40000000ull;
constexpr uint64_t THIRD_QTR = 0xC0000000ull;
constexpr uint32_t RESCALE_TOTAL = 16384;

class BitOutput {
public:
    void put(uint32_t bit) {
        current_ = static_cast<uint8_t>((current_ << 1) | (bit & 1u));
        if (++used_ == 8) { bytes_.push_back(current_); current_ = 0; used_ = 0; }
    }
    void finish_padding() {
        if (used_) { current_ <<= (8 - used_); bytes_.push_back(current_); }
        // Arithmetic decoders prime a 32-bit code register and may read ahead.
        while (bytes_.size() < 4) bytes_.push_back(0);
        for (int i = 0; i < 4; ++i) bytes_.push_back(0);
    }
    std::vector<uint8_t> take() { return std::move(bytes_); }
private:
    std::vector<uint8_t> bytes_;
    uint8_t current_ = 0;
    uint32_t used_ = 0;
};

class BitInput {
public:
    explicit BitInput(std::span<const uint8_t> bytes) : bytes_(bytes) {}
    uint32_t get() {
        if (position_ >= bytes_.size() * 8) return 0;
        const uint32_t b = (bytes_[position_ / 8] >> (7 - (position_ % 8))) & 1u;
        ++position_;
        return b;
    }
private:
    std::span<const uint8_t> bytes_;
    size_t position_ = 0;
};

void update_counts(std::vector<uint32_t>& counts, uint64_t& total, uint32_t symbol) {
    ++counts[symbol];
    ++total;
    if (total >= RESCALE_TOTAL) {
        total = 0;
        for (uint32_t& count : counts) {
            count = (count + 1u) >> 1;
            total += count;
        }
    }
}

void emit_with_follow(BitOutput& out, uint32_t bit, uint64_t& pending) {
    out.put(bit);
    while (pending) {
        out.put(bit ^ 1u);
        --pending;
    }
}

constexpr uint32_t MIX_TOTAL = 1u << 20;
constexpr uint32_t MIX_WEIGHT_TOTAL = 1u << 16;
constexpr size_t MIX_EXPERTS = 6;

std::vector<uint32_t> normalize_positive_weights(
        const std::vector<uint64_t>& raw, uint32_t target_total) {
    if (raw.empty() || target_total < raw.size())
        throw std::invalid_argument("invalid probability normalization");
    uint64_t sum = 0;
    for (uint64_t value : raw) sum += value;
    if (sum == 0) throw std::invalid_argument("zero probability mass");

    const uint64_t remaining = target_total - raw.size();
    std::vector<uint32_t> frequencies(raw.size(), 1u);
    std::vector<std::pair<uint64_t, size_t>> remainders;
    remainders.reserve(raw.size());
    uint64_t assigned = raw.size();
    for (size_t i = 0; i < raw.size(); ++i) {
        const uint64_t scaled = raw[i] * remaining;
        const uint64_t quotient = scaled / sum;
        frequencies[i] += static_cast<uint32_t>(quotient);
        assigned += quotient;
        remainders.emplace_back(scaled % sum, i);
    }
    std::stable_sort(remainders.begin(), remainders.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });
    uint64_t leftover = target_total - assigned;
    for (size_t i = 0; i < leftover; ++i)
        ++frequencies[remainders[i].second];
    return frequencies;
}

struct MixtureSnapshot {
    std::vector<uint32_t> mixture;
    std::array<std::vector<uint32_t>, MIX_EXPERTS> experts;
};

class AdaptiveMixtureModel {
public:
    explicit AdaptiveMixtureModel(uint32_t alphabet)
        : alphabet_(alphabet),
          counts_(alphabet, 1u),
          context_counts_{std::vector<uint32_t>(alphabet, 1u),
                          std::vector<uint32_t>(alphabet, 1u)},
          context_totals_{alphabet, alphabet},
          rescale_threshold_(std::max<uint64_t>(RESCALE_TOTAL,
                                                 static_cast<uint64_t>(alphabet) * 2u)) {
        if (alphabet == 0 || alphabet > 65536)
            throw std::invalid_argument("invalid adaptive mixture alphabet");

        std::vector<uint64_t> logistic(alphabet), laplace(alphabet),
                              student(alphabet), uniform(alphabet, 1u);
        uint64_t logistic_mass = 1u << 20;
        uint64_t laplace_mass = 1u << 20;
        for (uint32_t i = 0; i < alphabet; ++i) {
            logistic[i] = std::max<uint64_t>(1, logistic_mass);
            laplace[i] = std::max<uint64_t>(1, laplace_mass);
            const uint64_t x = i;
            student[i] = 1u + ((uint64_t{1} << 30) / (16u + x * x));
            logistic_mass = std::max<uint64_t>(1, logistic_mass * 358u / 1000u);
            laplace_mass = std::max<uint64_t>(1, laplace_mass / 2u);
        }
        priors_[0] = normalize_positive_weights(logistic, MIX_TOTAL);
        priors_[1] = normalize_positive_weights(laplace, MIX_TOTAL);
        priors_[2] = normalize_positive_weights(student, MIX_TOTAL);
        priors_[3] = normalize_positive_weights(uniform, MIX_TOTAL);
        weights_.fill(MIX_WEIGHT_TOTAL / static_cast<uint32_t>(MIX_EXPERTS));
        for (size_t i = 0; i < MIX_WEIGHT_TOTAL % MIX_EXPERTS; ++i) ++weights_[i];
    }

    MixtureSnapshot snapshot() const {
        MixtureSnapshot result;
        result.experts[0] = normalize_positive_weights(as_u64(counts_), MIX_TOTAL);
        result.experts[1] = normalize_positive_weights(as_u64(context_counts_[previous_nonzero_ ? 1 : 0]),
                                                       MIX_TOTAL);
        result.experts[2] = priors_[0];
        result.experts[3] = priors_[1];
        result.experts[4] = priors_[2];
        result.experts[5] = priors_[3];

        std::vector<uint64_t> mixed(alphabet_, 0);
        for (uint32_t s = 0; s < alphabet_; ++s) {
            uint64_t mass = 0;
            for (size_t e = 0; e < MIX_EXPERTS; ++e)
                mass += static_cast<uint64_t>(weights_[e]) * result.experts[e][s];
            mixed[s] = mass;
        }
        result.mixture = normalize_positive_weights(mixed, MIX_TOTAL);
        return result;
    }

    void update(uint32_t symbol, const MixtureSnapshot& snapshot) {
        std::vector<uint64_t> posterior(MIX_EXPERTS);
        for (size_t e = 0; e < MIX_EXPERTS; ++e)
            posterior[e] = static_cast<uint64_t>(weights_[e]) * snapshot.experts[e][symbol];
        const auto updated_weights = normalize_positive_weights(posterior, MIX_WEIGHT_TOTAL);
        std::copy(updated_weights.begin(), updated_weights.end(), weights_.begin());

        ++counts_[symbol];
        ++total_;
        if (total_ >= rescale_threshold_) rescale(counts_, total_);

        const size_t context = previous_nonzero_ ? 1u : 0u;
        ++context_counts_[context][symbol];
        ++context_totals_[context];
        if (context_totals_[context] >= rescale_threshold_)
            rescale(context_counts_[context], context_totals_[context]);
        previous_nonzero_ = symbol != 0;
    }

private:
    static std::vector<uint64_t> as_u64(const std::vector<uint32_t>& values) {
        return std::vector<uint64_t>(values.begin(), values.end());
    }
    static void rescale(std::vector<uint32_t>& counts, uint64_t& total) {
        total = 0;
        for (uint32_t& value : counts) {
            value = (value + 1u) >> 1;
            total += value;
        }
    }

    uint32_t alphabet_;
    std::vector<uint32_t> counts_;
    std::array<std::vector<uint32_t>, 2> context_counts_;
    std::array<uint64_t, 2> context_totals_;
    std::array<std::vector<uint32_t>, 4> priors_;
    std::array<uint32_t, MIX_EXPERTS> weights_{};
    uint64_t total_ = alphabet_;
    uint64_t rescale_threshold_;
    bool previous_nonzero_ = false;
};

} // namespace

std::vector<uint8_t> AdaptiveRangeEncoder::encode(std::span<const uint32_t> symbols,
                                                   uint32_t alphabet_size) const {
    if (alphabet_size == 0 || alphabet_size > 65536)
        throw std::invalid_argument("invalid adaptive range alphabet");
    std::vector<uint32_t> counts(alphabet_size, 1u);
    uint64_t total = alphabet_size;
    uint64_t low = 0, high = TOP, pending = 0;
    BitOutput output;

    for (uint32_t symbol : symbols) {
        if (symbol >= alphabet_size) throw std::invalid_argument("symbol outside alphabet");
        uint64_t cum_low = 0;
        for (uint32_t i = 0; i < symbol; ++i) cum_low += counts[i];
        const uint64_t frequency = counts[symbol];
        const uint64_t range = high - low + 1;
        const uint64_t old_low = low;
        high = old_low + (range * (cum_low + frequency)) / total - 1;
        low = old_low + (range * cum_low) / total;

        for (;;) {
            if (high < HALF) {
                emit_with_follow(output, 0, pending);
            } else if (low >= HALF) {
                emit_with_follow(output, 1, pending);
                low -= HALF; high -= HALF;
            } else if (low >= FIRST_QTR && high < THIRD_QTR) {
                ++pending;
                low -= FIRST_QTR; high -= FIRST_QTR;
            } else {
                break;
            }
            low = (low << 1) & TOP;
            high = ((high << 1) | 1u) & TOP;
        }
        update_counts(counts, total, symbol);
    }

    ++pending;
    emit_with_follow(output, low < FIRST_QTR ? 0u : 1u, pending);
    output.finish_padding();
    return output.take();
}

std::vector<uint32_t> AdaptiveRangeDecoder::decode(std::span<const uint8_t> bytes,
                                                    uint32_t alphabet_size,
                                                    uint64_t symbol_count) const {
    if (alphabet_size == 0 || alphabet_size > 65536 || bytes.size() < 4)
        throw std::invalid_argument("invalid adaptive range stream");
    if (symbol_count > static_cast<uint64_t>(SIZE_MAX))
        throw std::invalid_argument("adaptive range symbol count too large");
    std::vector<uint32_t> counts(alphabet_size, 1u);
    uint64_t total = alphabet_size;
    uint64_t low = 0, high = TOP, code = 0;
    BitInput input(bytes);
    for (int i = 0; i < 32; ++i) code = (code << 1) | input.get();

    std::vector<uint32_t> output;
    output.reserve(static_cast<size_t>(symbol_count));
    for (uint64_t n = 0; n < symbol_count; ++n) {
        const uint64_t range = high - low + 1;
        const uint64_t scaled = ((code - low + 1) * total - 1) / range;
        uint64_t cumulative = 0;
        uint32_t symbol = 0;
        while (symbol < alphabet_size && cumulative + counts[symbol] <= scaled) {
            cumulative += counts[symbol];
            ++symbol;
        }
        if (symbol >= alphabet_size) throw std::runtime_error("invalid adaptive range code");
        const uint64_t frequency = counts[symbol];
        const uint64_t old_low = low;
        high = old_low + (range * (cumulative + frequency)) / total - 1;
        low = old_low + (range * cumulative) / total;

        for (;;) {
            if (high < HALF) {
                // no offset
            } else if (low >= HALF) {
                code -= HALF; low -= HALF; high -= HALF;
            } else if (low >= FIRST_QTR && high < THIRD_QTR) {
                code -= FIRST_QTR; low -= FIRST_QTR; high -= FIRST_QTR;
            } else {
                break;
            }
            low = (low << 1) & TOP;
            high = ((high << 1) | 1u) & TOP;
            code = ((code << 1) | input.get()) & TOP;
        }
        output.push_back(symbol);
        update_counts(counts, total, symbol);
    }
    return output;
}

std::vector<uint8_t> AdaptiveMixtureRangeEncoder::encode(
        std::span<const uint32_t> symbols, uint32_t alphabet_size,
        const std::function<void(float)>& progress) const {
    if (alphabet_size == 0 || alphabet_size > 65536)
        throw std::invalid_argument("invalid adaptive mixture alphabet");
    AdaptiveMixtureModel model(alphabet_size);
    uint64_t low = 0, high = TOP, pending = 0;
    BitOutput output;
    const uint64_t progress_stride =
        std::max<uint64_t>(1, static_cast<uint64_t>(symbols.size()) / 100u);

    for (size_t index = 0; index < symbols.size(); ++index) {
        const uint32_t symbol = symbols[index];
        if (symbol >= alphabet_size) throw std::invalid_argument("symbol outside mixture alphabet");
        const MixtureSnapshot snapshot = model.snapshot();
        uint64_t cumulative = 0;
        for (uint32_t i = 0; i < symbol; ++i) cumulative += snapshot.mixture[i];
        const uint64_t frequency = snapshot.mixture[symbol];
        const uint64_t range = high - low + 1;
        const uint64_t old_low = low;
        high = old_low + (range * (cumulative + frequency)) / MIX_TOTAL - 1;
        low = old_low + (range * cumulative) / MIX_TOTAL;

        for (;;) {
            if (high < HALF) {
                emit_with_follow(output, 0, pending);
            } else if (low >= HALF) {
                emit_with_follow(output, 1, pending);
                low -= HALF; high -= HALF;
            } else if (low >= FIRST_QTR && high < THIRD_QTR) {
                ++pending;
                low -= FIRST_QTR; high -= FIRST_QTR;
            } else {
                break;
            }
            low = (low << 1) & TOP;
            high = ((high << 1) | 1u) & TOP;
        }
        model.update(symbol, snapshot);
        if (progress &&
            ((static_cast<uint64_t>(index) + 1u) % progress_stride == 0u ||
             index + 1u == symbols.size())) {
            progress(static_cast<float>(index + 1u) /
                     static_cast<float>(symbols.size()));
        }
    }

    ++pending;
    emit_with_follow(output, low < FIRST_QTR ? 0u : 1u, pending);
    output.finish_padding();
    return output.take();
}

std::vector<uint32_t> AdaptiveMixtureRangeDecoder::decode(
        std::span<const uint8_t> bytes, uint32_t alphabet_size,
        uint64_t symbol_count) const {
    if (alphabet_size == 0 || alphabet_size > 65536 || bytes.size() < 4)
        throw std::invalid_argument("invalid adaptive mixture stream");
    if (symbol_count > static_cast<uint64_t>(SIZE_MAX))
        throw std::invalid_argument("adaptive mixture symbol count too large");

    AdaptiveMixtureModel model(alphabet_size);
    uint64_t low = 0, high = TOP, code = 0;
    BitInput input(bytes);
    for (int i = 0; i < 32; ++i) code = (code << 1) | input.get();

    std::vector<uint32_t> output;
    output.reserve(static_cast<size_t>(symbol_count));
    for (uint64_t n = 0; n < symbol_count; ++n) {
        const MixtureSnapshot snapshot = model.snapshot();
        const uint64_t range = high - low + 1;
        const uint64_t scaled = ((code - low + 1) * MIX_TOTAL - 1) / range;
        uint64_t cumulative = 0;
        uint32_t symbol = 0;
        while (symbol < alphabet_size &&
               cumulative + snapshot.mixture[symbol] <= scaled) {
            cumulative += snapshot.mixture[symbol];
            ++symbol;
        }
        if (symbol >= alphabet_size)
            throw std::runtime_error("invalid adaptive mixture code");

        const uint64_t frequency = snapshot.mixture[symbol];
        const uint64_t old_low = low;
        high = old_low + (range * (cumulative + frequency)) / MIX_TOTAL - 1;
        low = old_low + (range * cumulative) / MIX_TOTAL;

        for (;;) {
            if (high < HALF) {
                // no offset
            } else if (low >= HALF) {
                code -= HALF; low -= HALF; high -= HALF;
            } else if (low >= FIRST_QTR && high < THIRD_QTR) {
                code -= FIRST_QTR; low -= FIRST_QTR; high -= FIRST_QTR;
            } else {
                break;
            }
            low = (low << 1) & TOP;
            high = ((high << 1) | 1u) & TOP;
            code = ((code << 1) | input.get()) & TOP;
        }
        output.push_back(symbol);
        model.update(symbol, snapshot);
    }
    return output;
}
} // namespace ulacr::coding
