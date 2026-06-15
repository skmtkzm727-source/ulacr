# ULAC-R C++ アーキテクチャ設計

## プロジェクト構造

```
ulac-r/
├── CMakeLists.txt
├── include/ulacr/
│   ├── core/
│   │   ├── types.hpp          ← 基本型 (AudioBuffer, Sample, Error…)
│   │   └── codec_config.hpp   ← エンコード全設定
│   ├── analysis/
│   │   └── segmenter.hpp      ← 適応的セグメンテーション
│   ├── graph/
│   │   └── block_graph.hpp    ← Block Graph Compression (LSH)
│   ├── dictionary/
│   │   └── spectral_dict.hpp  ← Spectral Dictionary (K-SVD / OMP)
│   ├── transform/
│   │   └── wavelet.hpp        ← CDF 5/3 整数ウェーブレット
│   ├── coding/
│   │   ├── rans_coder.hpp     ← rANS エンコーダ / デコーダ
│   │   ├── context_model.hpp  ← 多コンテキスト確率モデル
│   │   ├── residual_island.hpp← Residual Island Coding
│   │   └── bitplane.hpp       ← Bitplane Decomposition
│   ├── encoder.hpp            ← パイプライン統括 (Pimpl)
│   └── decoder.hpp            ← パイプライン統括 (Pimpl)
├── src/                       ← 実装ファイル群
├── tools/
│   ├── ulacr_encode.cpp       ← CLI エンコーダ
│   └── ulacr_decode.cpp       ← CLI デコーダ
└── tests/
```

## エンコードパイプライン

```
WAV/AIFF/RAW
     │
     ▼
┌─────────────────────┐
│  WavReader          │  PCM → AudioBuffer (int64 正規化)
└─────────────────────┘
     │
     ▼
┌─────────────────────┐
│  Segmenter          │  可変長ブロック分割
│  (energy/flux/zcr)  │  min:256 ～ max:65536 samples
└─────────────────────┘
     │  Segment[]
     ├──────────────────────────────────────────────┐
     ▼                                              ▼
┌─────────────────────┐                  ┌─────────────────────┐
│  BlockGraphBuilder  │                  │  SpectralDictLearner│
│  (LSH + similarity) │                  │  (K-SVD 全曲学習)   │
└─────────────────────┘                  └─────────────────────┘
     │  BlockGraph                             │  SpectralDictionary
     │                                         │
     └────────────────┬────────────────────────┘
                      ▼
          ┌─────────────────────┐
          │  WaveletTransform   │  CDF 5/3 多段整数変換
          └─────────────────────┘
                      │  WaveletCoeffs
                      ▼
          ┌─────────────────────┐
          │  ResidualIsland     │  ゼロ領域の効率的表現
          │  + BitplaneDecomp   │  上位ビット優先符号化
          └─────────────────────┘
                      │
                      ▼
          ┌─────────────────────┐
          │  ContextModel       │  多コンテキスト確率推定
          │  + RansEncoder      │  rANS エントロピー符号化
          └─────────────────────┘
                      │
                      ▼
               .ulacr ファイル


## クラス責務まとめ

| クラス                | 責務                                      |
|-----------------------|-------------------------------------------|
| `Segmenter`           | 信号特性に応じた可変長ブロック分割         |
| `BlockGraphBuilder`   | LSH で類似ブロックを発見、グラフ構築       |
| `SpectralDictLearner` | K-SVD 辞書学習 + OMP スパース符号化        |
| `WaveletTransform`    | CDF 5/3 整数リフティング変換（完全可逆）   |
| `ContextModel`        | 多次元コンテキストによる適応的確率テーブル |
| `RansEncoder/Decoder` | rANS エントロピー符号化 / 復号             |
| `Encoder`             | 全コンポーネントのパイプライン管理 (Pimpl) |
| `Decoder`             | 逆パイプライン管理 (Pimpl)                 |

## 実装優先順位（推奨）

1. **types + codec_config** ← 全体の土台
2. **rANS coder**           ← コア圧縮エンジン（単体でテスト可）
3. **Context model**        ← rANS に重ねる確率推定
4. **Wavelet transform**    ← CDF 5/3 は比較的実装が明確
5. **Segmenter**            ← ブロック分割
6. **Spectral Dictionary**  ← K-SVD/OMP（数学的難度が高め）
7. **Block Graph / LSH**    ← 構造圧縮の核心
8. **Encoder / Decoder**    ← パイプライン結合
```
