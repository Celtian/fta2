"""Synthetic complete-package tests; no original game data required."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile, ZipInfo

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import package_release

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

    def test_single_package_with_verified_voices_and_sources(self):
        source = self.root / "engine-source.zip"
        with ZipFile(source, "w") as z:
            z.writestr("engine-source/CMakeLists.txt", b"build")
            z.writestr("engine-source/LICENSE", b"licence")
        # Use a versioned runtime root as CPack does.
        with ZipFile(self.engine, "w") as z:
            z.writestr("engine-Windows/bin/fta2-native.exe", b"engine")
            z.writestr("engine-Windows/bin/SDL3.dll", b"SDL")
        voice = self.root / "voice.hrs"
        voice.write_bytes(b"synthetic verified voice")
        with patch.object(packager, "VOICE_SIZE", voice.stat().st_size), \
                patch.object(packager, "VOICE_SHA256", hashlib.sha256(voice.read_bytes()).hexdigest()):
            manifest = packager.package_complete(self.engine, self.game, self.output, source, voice)
        with ZipFile(self.output) as z:
            self.assertEqual(z.read("engine-Windows/media/FTAVOICE.HRS"), voice.read_bytes())
            self.assertEqual(z.read("engine-Windows/" + manifest["source"]["file"]), source.read_bytes())
            self.assertIn(b"voices are included", z.read("engine-Windows/START-HERE.txt"))
        self.assertEqual(len(manifest["files"]), 8)

    def test_invalid_voice_rejected_without_output(self):
        with self.assertRaisesRegex(ValueError, "voice archive size"):
            packager.package_complete(self.engine, self.game, self.output,
                                      voice_file=self.game / "FTAVOICE.HRS")
        self.assertFalse(self.output.exists())

    def test_damaged_voice_hash_rejected_without_output(self):
        voice = self.root / "bad-voice.hrs"
        voice.write_bytes(b"damaged voice")
        with patch.object(packager, "VOICE_SIZE", voice.stat().st_size):
            with self.assertRaisesRegex(ValueError, "damaged voice archive"):
                packager.package_complete(self.engine, self.game, self.output, voice_file=voice)
        self.assertFalse(self.output.exists())

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


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.engine = self.root / "runtime.zip"
        self.source = self.root / "game-1-source.zip"
        self.output = self.root / "release.zip"
        with ZipFile(self.engine, "w") as z:
            z.writestr("game-1-Windows/bin/fta2-native.exe", b"engine")
        with ZipFile(self.source, "w") as z:
            z.writestr("game-1-source/CMakeLists.txt", b"build instructions")
            z.writestr("game-1-source/LICENSE", b"licence")

    def build(self):
        return package_release.package_release(self.engine, self.source, self.output)

    def test_source_is_inside_single_repeatable_runtime(self):
        manifest = self.build()
        with ZipFile(self.output) as z:
            self.assertEqual(z.read("game-1-Windows/" + manifest["file"]), self.source.read_bytes())
            self.assertEqual(z.read("game-1-Windows/bin/fta2-native.exe"), b"engine")
        second = self.root / "second.zip"
        package_release.package_release(self.engine, self.source, second)
        self.assertEqual(self.output.read_bytes(), second.read_bytes())

    def test_runtime_library_symlink_preserved(self):
        link = ZipInfo("game-1-Windows/lib/libSDL3.so.0")
        link.create_system = 3
        link.external_attr = 0o120777 << 16
        with ZipFile(self.engine, "a") as z:
            z.writestr(link, b"libSDL3.so.0.4.18")
        self.build()
        with ZipFile(self.output) as z:
            self.assertEqual(z.read(link.filename), b"libSDL3.so.0.4.18")
            self.assertEqual(z.getinfo(link.filename).external_attr, link.external_attr)

    def test_escaping_runtime_symlink_rejected(self):
        link = ZipInfo("game-1-Windows/lib/bad")
        link.create_system = 3
        link.external_attr = 0o120777 << 16
        with ZipFile(self.engine, "a") as z:
            z.writestr(link, b"../../outside")
        with self.assertRaisesRegex(ValueError, "unsafe symbolic link"):
            self.build()
        self.assertFalse(self.output.exists())

    def test_source_version_mismatch(self):
        with self.assertRaisesRegex(ValueError, "version does not match"):
            package_release.source_additions(self.source, "game-2-Windows")
        self.assertFalse(self.output.exists())

    def test_source_data_and_unsafe_paths_rejected(self):
        for name in ("original-game/FTA2.EXE", "media/FTAVOICE.HRS", "../escape", "data/save.sav"):
            with self.subTest(name=name):
                with ZipFile(self.source, "w") as z:
                    z.writestr("game-1-source/CMakeLists.txt", b"build")
                    z.writestr("game-1-source/LICENSE", b"licence")
                    z.writestr("game-1-source/" + name, b"forbidden")
                with self.assertRaises(ValueError):
                    self.build()
                self.assertFalse(self.output.exists())

    def test_conflicting_entries_rejected(self):
        with ZipFile(self.engine, "a") as z:
            z.writestr("game-1-Windows/source/README.txt", b"conflict")
        with self.assertRaisesRegex(ValueError, "conflicts"):
            self.build()

    def test_existing_output_preserved(self):
        self.output.write_bytes(b"existing")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.build()
        self.assertEqual(self.output.read_bytes(), b"existing")


if __name__ == "__main__":
    unittest.main()
