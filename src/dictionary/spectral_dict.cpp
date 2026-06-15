#include "ulacr/dictionary/spectral_dict.hpp"
#include <random>
#include <cmath>
#include <algorithm>

namespace ulacr {

namespace {

void normalize(std::vector<float>& v) noexcept {
    double norm = 0.0;
    for (float x : v) norm += static_cast<double>(x) * x;
    norm = std::sqrt(norm);
    if (norm > 1e-12) {
        for (float& x : v) x = static_cast<float>(x / norm);
    }
}

} // namespace

SpectralDictLearner::SpectralDictLearner(const CodecConfig& cfg) noexcept : cfg_(cfg) {}

// ---------------------------------------------------------------
// OMP (Matching Pursuit ベースの貪欲近似)
//
// 各ステップで残差と最も相関の高い辞書要素を選択し、
// 係数 = 内積（辞書要素は正規化済み）として残差から減算する。
// ---------------------------------------------------------------
SparseCode SpectralDictLearner::omp(std::span<const float>    signal,
                                    const SpectralDictionary& dict,
                                    uint32_t                  sparsity) const {
    SparseCode code;
    if (dict.dict_size == 0 || dict.atom_size == 0) return code;

    std::vector<float> residual(dict.atom_size, 0.0f);
    for (uint32_t d = 0; d < dict.atom_size && d < signal.size(); ++d) residual[d] = signal[d];

    std::vector<bool> used(dict.dict_size, false);

    for (uint32_t step = 0; step < sparsity; ++step) {
        int64_t best = -1;
        double  best_dot = 0.0;

        for (uint32_t k = 0; k < dict.dict_size; ++k) {
            if (used[k]) continue;
            double dot = 0.0;
            for (uint32_t d = 0; d < dict.atom_size; ++d) dot += static_cast<double>(residual[d]) * dict.atoms[k][d];
            if (std::abs(dot) > std::abs(best_dot)) {
                best_dot = dot;
                best = static_cast<int64_t>(k);
            }
        }

        if (best < 0 || std::abs(best_dot) < 1e-9) break;

        used[static_cast<size_t>(best)] = true;
        code.indices.push_back(static_cast<uint32_t>(best));
        code.coefficients.push_back(static_cast<float>(best_dot));

        for (uint32_t d = 0; d < dict.atom_size; ++d) {
            residual[d] -= static_cast<float>(best_dot * dict.atoms[static_cast<size_t>(best)][d]);
        }
    }

    return code;
}

// ---------------------------------------------------------------
// K-SVD 辞書更新ステップ（簡易版）
//
// 各辞書要素 k について、その要素を使用している全信号から
// 「他の要素による再構成」を除いた誤差行列 E_k を構築し、
// パワー法（power iteration）によって E_k の主成分方向へ
// atom_k を更新する。係数も射影により再計算する。
// ---------------------------------------------------------------
void SpectralDictLearner::ksvd_step(SpectralDictionary&                      dict,
                                    const std::vector<std::vector<float>>&   signals,
                                    std::vector<SparseCode>&                 codes) const {
    for (uint32_t k = 0; k < dict.dict_size; ++k) {
        std::vector<std::vector<float>> Ek;
        std::vector<size_t> sig_idx;
        std::vector<size_t> coef_pos;

        for (size_t i = 0; i < signals.size(); ++i) {
            auto& code = codes[i];
            for (size_t p = 0; p < code.indices.size(); ++p) {
                if (code.indices[p] != k) continue;

                std::vector<float> e(dict.atom_size, 0.0f);
                for (uint32_t d = 0; d < dict.atom_size && d < signals[i].size(); ++d) e[d] = signals[i][d];

                for (size_t q = 0; q < code.indices.size(); ++q) {
                    if (q == p) continue;
                    uint32_t idx2 = code.indices[q];
                    float    c2   = code.coefficients[q];
                    for (uint32_t d = 0; d < dict.atom_size; ++d) {
                        e[d] -= c2 * dict.atoms[idx2][d];
                    }
                }

                Ek.push_back(std::move(e));
                sig_idx.push_back(i);
                coef_pos.push_back(p);
                break; // 1信号あたり同じ要素は1回のみ想定
            }
        }

        if (Ek.empty()) continue;

        // パワー法で E_k の主成分方向を求める
        std::vector<float> atom = dict.atoms[k];
        if (atom.empty()) atom.assign(dict.atom_size, 0.0f);

        for (int iter = 0; iter < 10; ++iter) {
            std::vector<float> next(dict.atom_size, 0.0f);
            for (const auto& e : Ek) {
                double dot = 0.0;
                for (uint32_t d = 0; d < dict.atom_size; ++d) dot += static_cast<double>(e[d]) * atom[d];
                for (uint32_t d = 0; d < dict.atom_size; ++d) next[d] += static_cast<float>(dot * e[d]);
            }
            normalize(next);
            double n = 0.0;
            for (float v : next) n += static_cast<double>(v) * v;
            if (n < 1e-12) break;
            atom = next;
        }

        dict.atoms[k] = atom;

        // 係数を射影 (dot(e, atom)) で再計算
        for (size_t t = 0; t < Ek.size(); ++t) {
            double dot = 0.0;
            for (uint32_t d = 0; d < dict.atom_size; ++d) dot += static_cast<double>(Ek[t][d]) * atom[d];
            codes[sig_idx[t]].coefficients[coef_pos[t]] = static_cast<float>(dot);
        }
    }
}

// ---------------------------------------------------------------
// 辞書学習: 楽曲全体のスペクトルデータから K-SVD で辞書を構築する
// ---------------------------------------------------------------
SpectralDictionary SpectralDictLearner::learn(const std::vector<std::vector<float>>& spectra) const {
    SpectralDictionary dict;
    dict.atom_size = cfg_.dict_atom_size;
    dict.dict_size = cfg_.dict_size;
    dict.atoms.assign(dict.dict_size, std::vector<float>(dict.atom_size, 0.0f));

    std::mt19937_64 rng(0x5EED5EEDu);

    if (spectra.empty()) {
        std::normal_distribution<float> nd(0.0f, 1.0f);
        for (auto& atom : dict.atoms) {
            for (auto& v : atom) v = nd(rng);
            normalize(atom);
        }
        return dict;
    }

    // --- 初期化: データからランダムサンプリングして正規化 ---
    std::uniform_int_distribution<size_t> pick(0, spectra.size() - 1);
    for (uint32_t k = 0; k < dict.dict_size; ++k) {
        const auto& sample = spectra[pick(rng)];
        for (uint32_t d = 0; d < dict.atom_size && d < sample.size(); ++d) dict.atoms[k][d] = sample[d];
        normalize(dict.atoms[k]);
        bool all_zero = std::all_of(dict.atoms[k].begin(), dict.atoms[k].end(),
                                    [](float v){ return v == 0.0f; });
        if (all_zero) {
            std::normal_distribution<float> nd(0.0f, 1.0f);
            for (auto& v : dict.atoms[k]) v = nd(rng);
            normalize(dict.atoms[k]);
        }
    }

    uint32_t sparsity = std::max<uint32_t>(1, dict.dict_size / 64);
    std::vector<SparseCode> codes(spectra.size());

    for (uint32_t it = 0; it < cfg_.ksvd_iterations; ++it) {
        for (size_t i = 0; i < spectra.size(); ++i) {
            codes[i] = omp(spectra[i], dict, sparsity);
        }
        ksvd_step(dict, spectra, codes);
    }

    return dict;
}

// ---------------------------------------------------------------
// スパース符号化（単一ブロック） + 残差出力
// ---------------------------------------------------------------
SparseCode SpectralDictLearner::encode(std::span<const float>    spectrum,
                                       const SpectralDictionary&  dict,
                                       std::vector<float>&         residual_out) const {
    uint32_t sparsity = std::max<uint32_t>(1, dict.dict_size / 64);
    SparseCode code = omp(spectrum, dict, sparsity);

    std::vector<float> recon(dict.atom_size, 0.0f);
    for (size_t i = 0; i < code.indices.size(); ++i) {
        uint32_t idx  = code.indices[i];
        float    coef = code.coefficients[i];
        for (uint32_t d = 0; d < dict.atom_size; ++d) recon[d] += coef * dict.atoms[idx][d];
    }

    residual_out.assign(dict.atom_size, 0.0f);
    for (uint32_t d = 0; d < dict.atom_size; ++d) {
        float orig = (d < spectrum.size()) ? spectrum[d] : 0.0f;
        residual_out[d] = orig - recon[d];
    }

    return code;
}

// ---------------------------------------------------------------
// スパース符号 + 残差から元スペクトルを復元する
// ---------------------------------------------------------------
std::vector<float> SpectralDictLearner::decode(const SparseCode&          code,
                                               const SpectralDictionary&  dict,
                                               std::span<const float>     residual) const {
    std::vector<float> out(dict.atom_size, 0.0f);

    for (size_t i = 0; i < code.indices.size(); ++i) {
        uint32_t idx  = code.indices[i];
        float    coef = code.coefficients[i];
        for (uint32_t d = 0; d < dict.atom_size; ++d) out[d] += coef * dict.atoms[idx][d];
    }

    for (uint32_t d = 0; d < dict.atom_size && d < residual.size(); ++d) {
        out[d] += residual[d];
    }

    return out;
}

} // namespace ulacr
