#!/usr/bin/env python3
"""Per-file ULAC-R vs FLAC -8 benchmark for a supplied WAV corpus."""
import argparse, csv, pathlib, shutil, struct, subprocess, tempfile, time
def run(cmd):
    start=time.perf_counter()
    p=subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return p.returncode, time.perf_counter()-start
def wav_payload(path):
    """Read RIFF/RF64 WAVE format metadata and the exact data chunk bytes."""
    data = path.read_bytes()
    if len(data) < 12 or data[8:12] != b"WAVE" or data[:4] not in (b"RIFF", b"RF64"):
        raise ValueError("not a RIFF/RF64 WAVE file")
    rf64 = data[:4] == b"RF64"
    offset = 12
    ds64_data_size = None
    fmt = None
    payload = None
    while offset + 8 <= len(data):
        chunk_id = data[offset:offset + 4]
        chunk_size = struct.unpack_from("<I", data, offset + 4)[0]
        offset += 8
        if chunk_id == b"ds64" and chunk_size >= 28 and offset + chunk_size <= len(data):
            ds64_data_size = struct.unpack_from("<Q", data, offset + 8)[0]
        actual_size = ds64_data_size if rf64 and chunk_id == b"data" and chunk_size == 0xffffffff else chunk_size
        if actual_size is None or offset + actual_size > len(data):
            raise ValueError("truncated WAVE chunk")
        chunk = data[offset:offset + actual_size]
        if chunk_id == b"fmt ":
            if len(chunk) < 16:
                raise ValueError("short fmt chunk")
            tag, channels, rate, byte_rate, align, bits = struct.unpack_from("<HHIIHH", chunk)
            valid_bits, channel_mask, subformat = bits, 0, b""
            if tag == 0xfffe:
                if len(chunk) < 40 or struct.unpack_from("<H", chunk, 16)[0] < 22:
                    raise ValueError("invalid WAVE_FORMAT_EXTENSIBLE")
                valid_bits, channel_mask = struct.unpack_from("<HI", chunk, 18)
                subformat = chunk[24:40]
                if subformat[:4] == b"\\x01\\x00\\x00\\x00":
                    tag = 1
                elif subformat[:4] == b"\\x03\\x00\\x00\\x00":
                    tag = 3
            fmt = (tag, channels, rate, byte_rate, align, bits, valid_bits, channel_mask, subformat)
        elif chunk_id == b"data":
            payload = chunk
        offset += actual_size + (actual_size & 1)
    if fmt is None or payload is None:
        raise ValueError("WAVE fmt/data chunk missing")
    return fmt, payload
def pcm_equal(a,b):
    try:
        return wav_payload(a) == wav_payload(b)
    except (OSError, ValueError, struct.error):
        return False
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--corpus",required=True,type=pathlib.Path)
    ap.add_argument("--encoder",required=True,type=pathlib.Path)
    ap.add_argument("--decoder",required=True,type=pathlib.Path)
    ap.add_argument("--output",default=pathlib.Path("benchmark.csv"),type=pathlib.Path)
    ap.add_argument("--profile",choices=("classic","ultra","auto"),default="auto")
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
