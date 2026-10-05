import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "rnet-bin2md.py"
spec = importlib.util.spec_from_file_location("rnet_bin2md", MODULE_PATH)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class RNetBin2MdTests(unittest.TestCase):
    def test_decode_bits(self):
        self.assertEqual(mod.decode_bits(0x81), [1, 8])
        self.assertEqual(mod.decode_bits(0x8F), [1, 2, 3, 4, 8])
        self.assertEqual(mod.decode_bits(0x00), [])

    def test_read_rnb2(self):
        selector = bytes.fromhex("01000100")
        payload = b"abc"
        raw = (
            b"RNB2"
            + struct.pack("<I", 1)
            + selector
            + struct.pack("<III", 1, 38, len(payload))
            + payload
        )
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "state.bin"
            p.write_bytes(raw)
            blocks = mod.read_rnb2(p)
        self.assertEqual(blocks[selector]["value89"], 38)
        self.assertEqual(blocks[selector]["data"], payload)

    def test_merge_blocks_overlay_wins(self):
        selector = bytes.fromhex("01000100")
        base = {selector: {"data": b"old"}}
        overlay = {selector: {"data": b"new"}}
        merged = mod.merge_blocks(base, overlay)
        self.assertEqual(merged[selector]["data"], b"new")


if __name__ == "__main__":
    unittest.main()
