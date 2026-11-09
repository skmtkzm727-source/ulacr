import importlib.util
import pathlib
import struct
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).with_name("benchmark.py")
SPEC = importlib.util.spec_from_file_location("ulacr_benchmark", SCRIPT)
BENCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BENCH)

def chunk(tag, payload):
    return tag + struct.pack("<I", len(payload)) + payload + (bytes([0]) if len(payload) & 1 else b"")

def riff(chunks):
    body = b"WAVE" + b"".join(chunks)
    return b"RIFF" + struct.pack("<I", len(body)) + body

class WavPayloadTests(unittest.TestCase):
    def compare(self, left, right, expected):
        with tempfile.TemporaryDirectory() as td:
            a = pathlib.Path(td) / "a.wav"
            b = pathlib.Path(td) / "b.wav"
            a.write_bytes(left)
            b.write_bytes(right)
            self.assertEqual(BENCH.pcm_equal(a, b), expected)

    def test_pcm_data_exact_and_header_chunks_ignored(self):
        fmt = struct.pack("<HHIIHH", 1, 2, 48000, 192000, 4, 16)
        data = bytes([1, 0, 255, 127])
        a = riff([chunk(b"fmt ", fmt), chunk(b"data", data)])
        b = riff([chunk(b"JUNK", b"meta"), chunk(b"fmt ", fmt), chunk(b"data", data)])
        self.compare(a, b, True)
        self.compare(a, riff([chunk(b"fmt ", fmt), chunk(b"data", data[:-1] + bytes([0]))]), False)

    def test_float32_extensible_guid(self):
        float_guid = bytes.fromhex("0300000000001000800000aa00389b71")
        base = struct.pack("<HHIIHH", 0xfffe, 1, 48000, 192000, 4, 32)
        ext = struct.pack("<HHI", 22, 32, 0) + float_guid
        a = riff([chunk(b"fmt ", base + ext), chunk(b"data", bytes([0, 0, 128, 63]))])
        b = riff([chunk(b"fmt ", base + ext), chunk(b"data", bytes([0, 0, 128, 191]))])
        self.compare(a, b, False)

    def test_truncated_chunk_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            p = pathlib.Path(td) / "bad.wav"
            p.write_bytes(b"RIFF" + struct.pack("<I", 16) + b"WAVEdata")
            self.assertFalse(BENCH.pcm_equal(p, p))

if __name__ == "__main__":
    unittest.main()
