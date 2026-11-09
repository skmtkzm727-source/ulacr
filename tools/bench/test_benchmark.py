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
    return tag + struct.pack("<I", len(payload)) + payload + (b"\\x00" if len(payload) & 1 else b"")

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
        data = b"\\x01\\x00\\xff\\x7f"
        a = riff([chunk(b"fmt ", fmt), chunk(b"data", data)])
        b = riff([chunk(b"JUNK", b"meta"), chunk(b"fmt ", fmt), chunk(b"data", data)])
        self.compare(a, b, True)
        self.compare(a, riff([chunk(b"fmt ", fmt), chunk(b"data", data[:-1] + b"\\x00")]), False)

    def test_float32_extensible_guid(self):
        pcm_guid = bytes.fromhex("0100000000001000800000aa00389b71")
        float_guid = bytes.fromhex("0300000000001000800000aa00389b71")
        base = struct.pack("<HHIIHH", 0xfffe, 1, 48000, 192000, 4, 32)
        ext = struct.pack("<HHI", 22, 32, 0) + float_guid
        a = riff([chunk(b"fmt ", base + ext), chunk(b"data", b"\\x00\\x00\\x80\\x3f")])
        b = riff([chunk(b"fmt ", base + ext), chunk(b"data", b"\\x00\\x00\\x80\\xbf")])
        self.compare(a, b, False)
        self.assertNotEqual(pcm_guid, float_guid)

    def test_truncated_chunk_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            p = pathlib.Path(td) / "bad.wav"
            p.write_bytes(b"RIFF\\x10\\x00\\x00\\x00WAVEdata")
            self.assertFalse(BENCH.pcm_equal(p, p))

if __name__ == "__main__":
    unittest.main()
