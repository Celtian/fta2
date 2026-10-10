"""Independent malformed-input tests for the resource inspection tool."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("fta2_data", Path(__file__).resolve().parents[1] / "tools/fta2_data.py")
data = importlib.util.module_from_spec(spec)
spec.loader.exec_module(data)

class ArchiveTests(unittest.TestCase):
    def archive(self):
        return (b"HRES" + struct.pack("<II", 12, 12)
                + struct.pack("<4sII", b"TEST", 24, 12)
                + struct.pack("<4sII", b"ITEM", 36, 3) + b"abc")

    def test_last_resource_ends_at_eof(self):
        result = data.hrs_inventory(self.archive())
        self.assertEqual(result["groups"][0]["entries"][0]["size"], 3)

    def test_truncated_payload_is_rejected(self):
        with self.assertRaises(data.DataError):
            data.hrs_inventory(self.archive()[:-1])

    def test_overflowing_root_is_rejected(self):
        with self.assertRaises(data.DataError):
            data.hrs_inventory(b"HRES" + struct.pack("<II", 0xFFFFFFFC, 12))

    def test_incomplete_table_entry_is_rejected(self):
        damaged = bytearray(self.archive())
        struct.pack_into("<I", damaged, 20, 11)
        with self.assertRaises(data.DataError):
            data.hrs_inventory(damaged)

    def test_unknown_executable_is_rejected_with_expected_hash(self):
        with self.assertRaisesRegex(data.DataError, data.EXE_SHA256):
            data.recover(b"MZ" + bytes(100))

    def test_color_ranges_retain_empty_first_bank_and_last_valid_bank(self):
        # A missing initial bank shifts every sprite's chosen color scheme.
        executable = bytearray(0x220000)
        for offset in data.FONTS.values():
            struct.pack_into("<HHH", executable, offset, 1, 1, 1)
        offset = 0x1C3B1C
        ranges = bytes(8) + bytes(value for bank in range(1, 198)
                                  for value in (bank,) * 8)
        executable[offset:offset + len(ranges)] = ranges
        executable[offset + len(ranges):offset + len(ranges) + 8] = b"NOTBANK!"
        executable = bytes(executable)
        with patch.object(data, "EXE_SHA256", data.sha256(executable)):
            assets = {name: (start, payload)
                      for name, start, payload in data.recover(executable)}
        self.assertEqual(assets["ColorMapRanges"], (offset, ranges))

    def test_refuse_extraction_into_originals_before_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaisesRegex(data.DataError, "outside"):
                data.extract(root, root / "extracted")
            self.assertEqual(list(root.iterdir()), [])

if __name__ == "__main__":
    unittest.main()
