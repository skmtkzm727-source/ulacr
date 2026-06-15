#include "ulacr/coding/residual_island.hpp"

namespace ulacr::coding {

ResidualIslandData ResidualIslandCoder::encode(std::span<const Sample> residuals) {
    ResidualIslandData out;
    out.total_length = residuals.size();

    uint32_t run = 0;
    for (Sample s : residuals) {
        if (s == 0) {
            ++run;
        } else {
            out.zero_runs.push_back(run);
            out.values.push_back(s);
            run = 0;
        }
    }
    // 末尾のゼロ連続領域
    out.zero_runs.push_back(run);

    return out;
}

SampleVec ResidualIslandCoder::decode(const ResidualIslandData& data) {
    SampleVec out;
    out.reserve(data.total_length);

    const size_t num_values = data.values.size();
    for (size_t i = 0; i < num_values; ++i) {
        uint32_t run = data.zero_runs[i];
        for (uint32_t j = 0; j < run; ++j) out.push_back(0);
        out.push_back(data.values[i]);
    }
    // 末尾のゼロ連続領域
    if (!data.zero_runs.empty()) {
        uint32_t trailing = data.zero_runs.back();
        for (uint32_t j = 0; j < trailing; ++j) out.push_back(0);
    }

    // total_length に合わせて調整（安全策）
    out.resize(data.total_length, 0);
    return out;
}

} // namespace ulacr::coding
