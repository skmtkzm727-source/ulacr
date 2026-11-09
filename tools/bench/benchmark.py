#!/usr/bin/env python3
"""Per-file ULAC-R vs FLAC -8 benchmark for a supplied WAV corpus."""
import argparse, csv, pathlib, shutil, subprocess, tempfile, time, wave
def run(cmd):
    start=time.perf_counter()
    p=subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return p.returncode, time.perf_counter()-start
def pcm_frames(path):
    with wave.open(str(path),"rb") as w:
        return (w.getparams(), w.readframes(w.getnframes()))
def pcm_equal(a,b):
    try:
        pa,da=pcm_frames(a); pb,db=pcm_frames(b)
        return (pa.nchannels,pa.sampwidth,pa.framerate,pa.nframes)==(pb.nchannels,pb.sampwidth,pb.framerate,pb.nframes) and da==db
    except (wave.Error,EOFError):
        return False
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--corpus",required=True,type=pathlib.Path)
    ap.add_argument("--encoder",required=True,type=pathlib.Path)
    ap.add_argument("--decoder",required=True,type=pathlib.Path)
    ap.add_argument("--output",default=pathlib.Path("benchmark.csv"),type=pathlib.Path)\n    ap.add_argument("--profile",choices=("classic","ultra","auto"),default="auto")
    a=ap.parse_args()
    files=sorted(p for p in a.corpus.rglob("*") if p.is_file() and p.suffix.lower()==".wav")
    if not files: raise SystemExit("No WAV files found")
    flac=shutil.which("flac"); rows=[]
    with tempfile.TemporaryDirectory(prefix="ulacr-bench-") as td:
        t=pathlib.Path(td)
        for idx,src in enumerate(files):
            rel=src.relative_to(a.corpus); enc=t/f"{idx}.ulacr"; dec=t/f"{idx}.decoded.wav"
            rc,et=run([str(a.encoder),"--profile",a.profile,str(src),str(enc)])
            if rc: rows.append([str(rel),a.profile,src.stat().st_size,"","","","",round(et,4),"","encode-failed"]); continue
            rc,dt=run([str(a.decoder),str(enc),str(dec)])
            exact=rc==0 and dec.exists() and pcm_equal(src,dec)
            fb=""
            if flac:
                fo=t/f"{idx}.flac"; frc,_=run([flac,"-8","-f","-s","-o",str(fo),str(src)])
                if frc==0: fb=fo.stat().st_size
            n=src.stat().st_size; z=enc.stat().st_size
            rows.append([str(rel),a.profile,n,z,round(100*z/n,4),fb,
                         round(100*z/fb,4) if fb else "",round(et,4),round(dt,4),
                         "pcm-exact" if exact else ("decode-failed" if rc else "pcm-mismatch")])
    with a.output.open("w",newline="",encoding="utf-8") as f:
        w=csv.writer(f); w.writerow(["file","profile","wav_bytes","ulacr_bytes","ulacr_pct_of_wav","flac8_bytes","ulacr_pct_of_flac8","encode_seconds","decode_seconds","roundtrip"]); w.writerows(rows)
    print(f"Benchmarked {len(rows)} files -> {a.output}")
if __name__=="__main__": main()
