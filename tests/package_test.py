"""Synthetic complete-package tests; no original game data required."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

spec = importlib.util.spec_from_file_location(
    "package_complete", Path(__file__).resolve().parents[1] / "tools/package_complete.py"
)
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / "original-game"
        self.engine = self.root / "engine.zip"
        self.output = self.root / "complete.zip"
        files = {}
        for name in packager.GAME_FILES:
            content = name.encode()
            path = self.game / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
            files[name] = {"size": len(content), "sha256": hashlib.sha256(content).hexdigest()}
        (self.game / "999.SAV").write_bytes(b"must never be included")
        (self.game / "FTAVOICE.HRS").write_bytes(b"must never be included")
        self.profile = self.root / "profile.json"
        self.profile.write_text(json.dumps({"files": files}), encoding="utf-8")
        self.patch = patch.object(packager, "PROFILE_PATH", self.profile)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        with ZipFile(self.engine, "w") as z:
            z.writestr("engine/bin/fta2-native.exe", b"synthetic native engine")
            z.writestr("engine/bin/SDL3.dll", b"synthetic SDL")

    def build(self, output=None):
        return packager.package_complete(self.engine, self.game, output or self.output)

    def test_allowlist_manifest_and_repeatable_output(self):
        before = {p: p.read_bytes() for p in self.game.rglob("*") if p.is_file()}
        manifest = self.build()
        with ZipFile(self.output) as z:
            data = {n.removeprefix("engine/data/") for n in z.namelist()
                    if n.startswith("engine/data/")}
            self.assertEqual(data, set(packager.GAME_FILES))
            self.assertEqual(json.loads(z.read("engine/share/fta2-native/complete-package.json")),
                             manifest)
            for name in data:
                self.assertEqual(z.read("engine/data/" + name), (self.game / name).read_bytes())
        second = self.root / "second.zip"
        self.build(second)
        self.assertEqual(self.output.read_bytes(), second.read_bytes())
        self.assertTrue(all(p.read_bytes() == content for p, content in before.items()))

    def test_unknown_executable_is_rejected_before_output(self):
        (self.game / "FTA2.EXE").write_bytes(b"unknown exe")
        with self.assertRaisesRegex(ValueError, "FTA2.EXE: expected SHA-256"):
            self.build()
        self.assertFalse(self.output.exists())

    def test_damaged_archive_is_rejected_before_output(self):
        (self.game / "SCRIPTS.HRS").write_bytes(b"bad archive")
        with self.assertRaisesRegex(ValueError, "SCRIPTS.HRS: expected SHA-256"):
            self.build()
        self.assertFalse(self.output.exists())

    def test_missing_bank_is_rejected(self):
        (self.game / "DRIVERS/SAMPLE.OPL").unlink()
        with self.assertRaisesRegex(ValueError, "missing: DRIVERS/SAMPLE.OPL"):
            self.build()

    def test_output_inside_originals_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "outside"):
            self.build(self.game / "must-not-be-created/out.zip")
        self.assertFalse((self.game / "must-not-be-created").exists())

    def test_existing_output_is_preserved(self):
        self.output.write_bytes(b"existing output")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.build()
        self.assertEqual(self.output.read_bytes(), b"existing output")

    def test_unsafe_engine_path_is_rejected(self):
        with ZipFile(self.engine, "a") as z:
            z.writestr("engine/../bad", b"bad")
        with self.assertRaisesRegex(ValueError, "Unsafe engine ZIP"):
            self.build()
        self.assertFalse(self.output.exists())

    def test_existing_engine_data_is_rejected(self):
        with ZipFile(self.engine, "a") as z:
            z.writestr("engine/data/FTA2.EXE", b"unverified data")
        with self.assertRaisesRegex(ValueError, "already contains"):
            self.build()
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
