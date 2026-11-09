# ULAC-R audio benchmark
実音源コーパスはユーザーが用意する。著作権付き音源は同梱しない。
```sh
python3 tools/bench/benchmark.py --corpus /path/to/corpus --encoder build/tools/ulacr-encode --decoder build/tools/ulacr-decode --profile auto --output benchmark.csv
```
FLACがインストールされていればFLAC -8も比較する。CSVはファイルごとに元サイズ、ULAC-Rサイズ/比率、FLACサイズ、符号化・復号時間、WAVバイト完全一致を記録する。corpusはpop/classical/electronic/speech/noise/mp3-derived/dithered等のジャンル別フォルダを推奨。結果を平均だけで報告しない。
このハーネスは与えられた音源だけを計測し、代表的なコーパスを捏造したりFLAC優位を主張したりしない。

The `--profile` option accepts `classic`, `ultra`, or `auto`. Auto encodes both and chooses the smallest candidate that also passes a full decode comparison. A benchmark output built from a synthetic fixture is only a smoke test, not evidence about music compression performance.
