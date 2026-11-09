#!/usr/bin/env python3
"""Per-file ULAC-R vs FLAC -8 benchmark for a supplied WAV corpus."""
import argparse, csv, pathlib, shutil, subprocess, tempfile, time
def run(cmd):
    start=time.perf_counter()
    p=subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return p.returncode, time.perf_counter()-start
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--corpus",required=True,type=pathlib.Path)
    ap.add_argument("--encoder",required=True,type=pathlib.Path)
    ap.add_argument("--decoder",required=True,type=pathlib.Path)
    ap.add_argument("--output",default=pathlib.Path("benchmark.csv"),type=pathlib.Path)
    a=ap.parse_args()
    files=sorted(p for p in a.corpus.rglob("*") if p.is_file() and p.suffix.lower()==".wav")
    if not files: raise SystemExit("No WAV files found")
    flac=shutil.which("flac"); rows=[]
    with tempfile.TemporaryDirectory(prefix="ulacr-bench-") as td:
        t=pathlib.Path(td)
        for src in files:
            rel=src.relative_to(a.corpus); enc=t/"out.ulacr"; dec=t/"decoded.wav"
            rc,et=run([str(a.encoder),"--profile","classic",str(src),str(enc)])
            if rc: rows.append([str(rel),src.stat().st_size,"","","","",round(et,4),"","encode-failed"]); continue
            rc,dt=run([str(a.decoder),str(enc),str(dec)])
            exact=rc==0 and dec.exists() and dec.read_bytes()==src.read_bytes()
            fb=""
            if flac:
                fo=t/"out.flac"; frc,_=run([flac,"-8","-f","-s","-o",str(fo),str(src)])
                if frc==0: fb=fo.stat().st_size
            n=src.stat().st_size; z=enc.stat().st_size
            rows.append([str(rel),n,z,round(100*z/n,4),fb,
                         round(100*z/fb,4) if fb else "",round(et,4),round(dt,4),
                         "exact" if exact else ("decode-failed" if rc else "byte-mismatch")])
    with a.output.open("w",newline="",encoding="utf-8") as f:
        w=csv.writer(f); w.writerow(["file","wav_bytes","ulacr_bytes","ulacr_pct_of_wav","flac8_bytes","ulacr_pct_of_flac8","encode_seconds","decode_seconds","roundtrip"]); w.writerows(rows)
    print(f"Benchmarked {len(rows)} files -> {a.output}")
if __name__=="__main__": main()
