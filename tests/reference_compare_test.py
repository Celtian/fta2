"""Synthetic bounds for the one intentional Community Edition reference difference."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


class ReferenceComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for folder in ("reference", "engine", "reference-saves", "engine-saves"):
            (self.root / folder).mkdir()
        self.header = b"P6\n640 480\n255\n"
        self.original = bytes(640 * 480 * 3)
        self.icon = bytearray(self.original)
        for y in range(446, 459):
            for x in range(443, 456):
                if (x - 449) ** 2 + (y - 452) ** 2 <= 36:
                    offset = (y * 640 + x) * 3
                    self.icon[offset:offset + 3] = b"\x80\x60\x20"
        for name in ("initial.ppm", "controlled.ppm"):
            (self.root / "reference" / name).write_bytes(self.header + self.original)
        self.images(self.original)
        states = [{"phase": phase, "frame": 1, "actor": 1, "u": 2, "v": 3, "z": 4, "rng": 5}
                  for phase in ("initial", "moved", "switched", "saved")]
        (self.root / "engine/state.jsonl").write_text(
            "\n".join(json.dumps(state) for state in states))
        (self.root / "reference/reference.log").write_text("\n".join(
            f"REFERENCE_PROBE {state['phase']} frame=1 actor=1 u=2 v=3 z=4 rng=5"
            for state in states))
        for folder in ("reference-saves", "engine-saves"):
            for name in ("fta2.999", "fta2.001"):
                (self.root / folder / name).write_bytes(
                    b"FTA2" + bytes(124) + b"STAT" + struct.pack("<I", 1) + b"x")

    def images(self, pixels):
        for name in ("initial.ppm", "controlled.ppm"):
            (self.root / "engine" / name).write_bytes(self.header + pixels)

    def compare(self, allow=False):
        command = [sys.executable, str(Path(__file__).resolve().parents[1] / "tools/compare_reference.py"),
                   "--reference-output", str(self.root / "reference"),
                   "--reference-saves", str(self.root / "reference-saves"),
                   "--engine-output", str(self.root / "engine"),
                   "--engine-saves", str(self.root / "engine-saves")]
        if allow:
            command.append("--allow-about-icon")
        return subprocess.run(command, capture_output=True, text=True)

    def test_strict_default_and_explicit_icon_exception(self):
        self.assertEqual(self.compare().returncode, 0)
        self.images(self.icon)
        self.assertNotEqual(self.compare().returncode, 0)
        result = self.compare(allow=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["intentional_about_icon_pixels"],
                         {"initial.ppm": 113, "controlled.ppm": 113})

    def test_partial_icon_and_changes_outside_circle_fail(self):
        for offset in ((452 * 640 + 449) * 3, (446 * 640 + 443) * 3, 0):
            with self.subTest(offset=offset):
                pixels = bytearray(self.icon)
                pixels[offset:offset + 3] = (b"\0\0\0" if offset == (452 * 640 + 449) * 3
                                             else b"\x80\x60\x20")
                self.images(pixels)
                self.assertNotEqual(self.compare(allow=True).returncode, 0)

    def test_save_changes_remain_strict(self):
        self.images(self.icon)
        save = self.root / "engine-saves/fta2.001"
        save.write_bytes(save.read_bytes()[:-1] + b"y")
        result = self.compare(allow=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("World save chunks differ", result.stderr)


if __name__ == "__main__":
    unittest.main()
