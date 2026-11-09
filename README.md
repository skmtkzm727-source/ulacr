# ULAC-R (Ultra Lossless Audio Codec - Research Edition) — C++実装

仕様書 `ULAC-R Version 0.1 Draft` に基づく、実際に動作する研究用ロスレス音声コーデックの
リファレンス実装です。**実測でビット完全なロスレス圧縮を確認済み**です。

```
352,844 bytes (WAV) → 221,130 bytes (.ulacr) → 352,844 bytes (WAV, バイト完全一致)
```

---

## ビルド方法

### CMake を使う場合（推奨）

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build        # 単体テスト/結合テストを実行
```

### CMake が無い場合（手動ビルド）

```bash
mkdir -p build/obj
g++ -std=c++20 -O2 -Iinclude -Isrc -c src/**/*.cpp src/*.cpp   # 各 .o を build/obj へ
ar rcs build/libulacr.a build/obj/*.o
g++ -std=c++20 -O2 -Iinclude -o build/ulacr-encode tools/ulacr_encode.cpp build/libulacr.a
g++ -std=c++20 -O2 -Iinclude -o build/ulacr-decode tools/ulacr_decode.cpp build/libulacr.a
```

### 使い方

```bash
./build/ulacr-encode input.wav output.ulacr
./build/ulacr-decode output.ulacr restored.wav
```

---

## 実装状況

### メインパイプラインに統合済み（実際にビットストリームで使用）

| コンポーネント | ファイル | 説明 |
|---|---|---|
| WAV/RF64/RAW I/O | `io/wav_reader.*`, `io/wav_writer.*` | PCM(16/24/32bit)・浮動小数点(32/64bit)対応。浮動小数点はビットパターンを整数として可逆保存 |
| 適応的セグメンテーション | `analysis/segmenter.*` | エネルギー変化・スペクトルフラックス・ゼロ交差率・局所エントロピーに基づく可変長分割 |
| Block Graph Compression | `graph/block_graph.*`, `graph/lsh.*` | SimHash(LSH)で候補を探し、整数差分の推定符号量が実際に減る参照だけを採用 |
| Adaptive Integer Prediction | `pipeline_common.hpp` | ブロックごとに次数0〜4の整数固定予測器を比較。予測次数・初期値の符号量も含めて自動選択 |
| Stereo Mid/Side | `encoder.cpp`, `decoder.cpp` | 整数PCMステレオを可逆Mid/Side変換し、左右の相関を圧縮に利用 |
| Harmonic Modeling | `analysis/harmonic_model.*` | 自己相関による基本周波数推定 + Goertzelアルゴリズムによる倍音振幅・位相推定。差分のみ保存し完全可逆 |
| Hierarchical Wavelet Pyramid | `transform/wavelet.*` | CDF 5/3 整数リフティング（完全可逆、多段階層分解） |
| Residual Island Coding | `coding/residual_island.*` | ゼロ連続領域(island)と非ゼロ値を分離 |
| Bitplane Decomposition | `coding/bitplane.*` | 非ゼロ残差をzigzag→ビットプレーン分解（設定で有効化） |
| Context Modeling | `coding/context_model.*` | チャンネル・サブバンド・ストリーム種別・ビットプレーン位置に基づく適応的確率テーブル |
| Entropy Coding (rANS) | `coding/rans_coder.*` | Range Asymmetric Numeral Systems（ryg_rans方式） |

エンコーダ/デコーダの全体結線は `src/encoder.cpp` / `src/decoder.cpp`、両者が共有する
シンボル分類・コンテキストキー生成規則は `src/pipeline_common.hpp` にまとめています。

### 実装済み・単体動作するが、メインのビットストリームには未統合（研究/将来統合用）

| コンポーネント | ファイル | 状態 |
|---|---|---|
| Spectral Dictionary Learning (K-SVD/OMP) | `dictionary/spectral_dict.*` | K-SVD（パワー法による簡易版）+ OMP/Matching Pursuit によるスパース符号化を実装。浮動小数点辞書のため、ビット完全性を保ったままメイン残差ストリームへ統合するには量子化スキームの追加設計が必要 |
| FFT | `transform/fft.*` | Radix-2 Cooley-Tukey。segmenter のスペクトルフラックス計算と harmonic_model の一部で使用 |

これらは仕様書の「将来研究」セクションに対応する実験的コンポーネントとして、
独立にテスト可能な形で実装してあります。

---

## .ulacr ファイルフォーマット概要

現在のエンコーダーはフォーマット **0.2** を出力します。デコーダーは従来の0.1と0.2の両方を読み込めます。0.2ではステレオMid/Sideフラグとブロック単位の予測次数・予測初期値を追加しています。0.1対応の古いデコーダーでは0.2ファイルを読めません。

```
[Magic "ULCR" + Version]
[AudioSpec]                 sample_rate, channels, num_samples, format...
[DecodeFlags]                wavelet_levels, enable_* フラグ
[Segments]                   各セグメント長 (uvarint)
[BlockGraph edges]           (有効時) tgt_id, ref_id のペア一覧
[Per (channel, segment) meta] levels_done, 各サブバンドの非ゼロ値数/プレーン数,
                              (有効時) Harmonic Model パラメータ(f32)
[Context groups]             ContextKey + 確率テーブル + rANS符号化バイト列
[Raw mantissa bitstream]     bucket方式の仮数ビット
```

### 符号化方式（bucket方式）

各残差値は zigzag 符号化後、`bit_length`（0〜64）を「バケット」シンボルとして
コンテキストモデル + rANS で符号化し、残りの仮数ビットは生ビット列として
別途格納します。これにより Residual Island（ゼロ連続）とビットプレーン的な
「上位ビットほど高頻度でゼロ」という構造の両方を、一つの適応的スキームで
自然に圧縮します。`enable_bitplane` を有効にすると、非ゼロ値はこの代わりに
明示的なビットプレーン分解 + 二値コンテキストモデルで符号化されます。

---

## ディレクトリ構成

```
ulac-r/
├── CMakeLists.txt
├── include/ulacr/        公開ヘッダ
├── src/                   実装 (pipeline_common.hpp は内部共有ヘッダ)
├── tools/                 CLIツール (ulacr-encode, ulacr-decode, gen_test_wav)
└── tests/                 単体・結合テスト (rANS / Wavelet / Bitplane+Island / Roundtrip)
```

## テスト内容

- `test_rans`: rANSの符号化/復号ラウンドトリップ（一様分布・偏った分布・縮退ケース）
- `test_wavelet`: CDF5/3整数ウェーブレットの可逆性（偶数長/奇数長/定数信号）
- `test_bitplane_island`: zigzag/ビットプレーン/Residual Islandの可逆性
- `test_roundtrip`: Encoder→Decoderの全パイプラインを様々な設定
  （Block Graph有効/無効、Harmonic有効/無効、Bitplane有効/無効、ウェーブレット無効、
  16/24bit、モノラル/ステレオ）でビット完全性を検証

## 既知の制約・今後の課題

- AIFF入力は現状未対応（WAV/RF64/RAW PCMのみ）。`io::WavReader` の拡張で対応可能
- Spectral Dictionary のメインパイプライン統合（上記参照）
- Harmonic Modeling は1ブロックあたり最大 `(1 + 2*max_harmonics) * 4` バイトの
  メタデータを消費するため、小さい `min_block_size` では相対的にオーバーヘッドが
  大きくなる点に注意（`max_harmonics` を小さくするか、ブロックサイズを大きくして使用）
- スペクトル辞書のメインストリーム統合は未実装（現状は研究用モジュール）。最高圧縮プリセットでは誤解を避けるため無効化しています。
- マルチスレッド化（チャンネル/セグメント単位の並列化）は未実装


## Web Demo（ブラウザ版）

`web/` に、ULAC-Rをブラウザ上で試せる変換・再生ツールがあります。

- PCM WAV → `.ulacr` 圧縮
- `.ulacr` → WAV 復元
- 復元したWAVのブラウザ内試聴
- 変換前後のサイズと元サイズ比の表示
- 音声ファイルはブラウザ内で処理し、サーバーへ送信しません

### GitHub Pagesで公開

1. GitHubリポジトリの **Settings → Pages** を開きます。
2. **Build and deployment → Source** を **GitHub Actions** にします。
3. `main` ブランチへ変更をpushすると、`.github/workflows/deploy-pages.yml` がEmscriptenでWebAssemblyをビルドし、GitHub Pagesへ公開します。
4. Actionsタブで **Build and deploy ULAC-R Web** の完了を確認します。

### ローカルでWebAssemblyをビルド

Emscripten SDKを用意したうえで、リポジトリのルートから実行します。

```bash
emcmake cmake -S . -B build-wasm \
  -DULACR_BUILD_TOOLS=OFF \
  -DULACR_BUILD_TESTS=OFF \
  -DULACR_BUILD_WASM=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --parallel
```

生成物は `build-wasm/web-dist/` に出力されます。Web UI（`web/index.html`、`web/app.js`、`web/styles.css`）と同じディレクトリに配置して、HTTPサーバー経由で開いてください。


## ULAC-R 2 profiles

The encoder CLI accepts `--profile classic`, `--profile ultra`, and `--profile auto`.
`classic` writes the existing 0.2 stream byte-for-byte. `ultra` writes version 0.3 with a CRC32 trailer, extended integer predictors, fixed-point NLMS, backward scanning, integer-delay matching, reversible common-factor scaling, sparse-symbol mapping, and an integer Bayesian mixture range coder. Float32/Float64 payloads are kept as opaque bit patterns. `auto` encodes and fully decodes both candidates, then selects the smaller bit-exact file. The decoder reads 0.1/0.2 and the current 0.3 format.

Some planned research features are still not implemented: OLS/Cholesky prediction, exact dither PRNG identification/removal, MDCT-grid/cutoff forensics, full SSE/APM, fractional-delay/gain/sinc matching, and the optional neural model. The current mixture uses integerized logistic-like, Laplace-like and Student-t-like priors plus adaptive histogram/context experts; it is not a full continuous-density Logistic/Laplace/Student-t implementation. No FLAC -8 superiority or 50%-of-WAV ratio is claimed without a real licensed music corpus.

Run a real corpus comparison with `tools/bench/benchmark.py`; no claim of beating FLAC -8 or reaching 50% of WAV is made until measured on licensed real-world audio.
