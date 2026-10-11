"""Synthetic complete-package tests; no original game data required."""
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import sys
import subprocess
import tempfile
import unittest
import warnings
from unittest.mock import patch
from zipfile import ZipFile, ZipInfo

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import package_release
import release_data

spec = importlib.util.spec_from_file_location(
    "package_complete", Path(__file__).resolve().parents[1] / "tools/package_complete.py"
)
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


def runtime_zip(archive, root, platform="Windows", engine=b"engine"):
    windows = platform == "Windows"
    archive.writestr(root + ("/bin/fta2-ce.exe" if windows else "/bin/fta2-ce"), engine)
    archive.writestr(root + ("/bin/SDL3.dll" if windows else
                             "/lib/libSDL3.0.dylib" if platform == "Darwin" else
                             "/lib/libSDL3.so.0.4.18"), b"SDL")
    launcher = ("Faery Tale Adventure II.exe" if windows else
                "Faery Tale Adventure II.command" if platform == "Darwin" else
                "Faery Tale Adventure II")
    archive.writestr(root + "/" + launcher, b"launcher")
    archive.writestr(root + "/START-HERE.txt", b"startup notes")
    for name in package_release.LICENSE_FILES:
        archive.writestr(root + "/licenses/" + name, b"licence")


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
            runtime_zip(z, "engine", engine=b"synthetic engine")

    def build(self, output=None):
        return packager.package_complete(self.engine, self.game, output or self.output)

    def test_allowlist_manifest_and_repeatable_output(self):
        before = {p: p.read_bytes() for p in self.game.rglob("*") if p.is_file()}
        manifest = self.build()
        with ZipFile(self.output) as z:
            data = {n.removeprefix("engine/data/") for n in z.namelist()
                    if n.startswith("engine/data/")}
            self.assertEqual(data, set(packager.GAME_FILES))
            self.assertEqual(json.loads(z.read("engine/bin/package.json")),
                             manifest)
            self.assertFalse(any(n.startswith("engine/share/") for n in z.namelist()))
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
            runtime_zip(z, "engine-Windows")
        voice = self.root / "voice.hrs"
        voice.write_bytes(b"synthetic verified voice")
        with patch.object(packager, "VOICE_SIZE", voice.stat().st_size), \
                patch.object(packager, "VOICE_SHA256", hashlib.sha256(voice.read_bytes()).hexdigest()):
            manifest = packager.package_complete(self.engine, self.game, self.output, source, voice)
        with ZipFile(self.output) as z:
            self.assertEqual(z.read("engine-Windows/media/FTAVOICE.HRS"), voice.read_bytes())
            self.assertEqual(z.read("engine-Windows/" + manifest["source"]["file"]), source.read_bytes())
            self.assertIn(b"voices are included", z.read("engine-Windows/START-HERE.txt"))
            self.assertIn(b"licenses/", z.read("engine-Windows/START-HERE.txt"))
            self.assertNotIn("engine-Windows/source/README.txt", z.namelist())
            self.assertNotIn("engine-Windows/media/voice-manifest.json", z.namelist())
            self.assertEqual(json.loads(z.read("engine-Windows/bin/package.json"))["voice"],
                             manifest["voice"])
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
        with self.assertRaisesRegex(ValueError, "Unsafe ZIP"):
            self.build()
        self.assertFalse(self.output.exists())

    def test_existing_engine_data_is_rejected(self):
        with ZipFile(self.engine, "a") as z:
            z.writestr("engine/data/FTA2.EXE", b"unverified data")
        with self.assertRaisesRegex(ValueError, "already contains"):
            self.build()
        self.assertFalse(self.output.exists())


class ReleaseDataTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.game = self.root / "original-game"
        files = {}
        for name in packager.GAME_FILES:
            path = self.game / name
            path.parent.mkdir(parents=True, exist_ok=True)
            payload = name.encode()
            path.write_bytes(payload)
            files[name] = {"size": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}
        (self.game / "999.SAV").write_bytes(b"private save")
        self.voice = self.root / "voice.hrs"
        self.voice.write_bytes(b"synthetic verified voice")
        self.profile = self.root / "profile.json"
        self.profile.write_text(json.dumps({"files": files}))
        self.config = self.root / "release-data.json"
        for field, value in (("PROFILE_PATH", self.profile), ("CONFIG_PATH", self.config),
                             ("VOICE_SIZE", self.voice.stat().st_size),
                             ("VOICE_SHA256", hashlib.sha256(self.voice.read_bytes()).hexdigest())):
            patcher = patch.object(release_data, field, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        self.archive = self.root / "release-input.zip"
        self.output = self.root / "verified"

    def prepare(self):
        metadata = release_data.create(self.game, self.voice, self.archive)
        self.config.write_text(json.dumps(metadata))
        return metadata

    def repin(self):
        self.config.write_text(json.dumps({"size": self.archive.stat().st_size,
                                          "sha256": hashlib.sha256(self.archive.read_bytes()).hexdigest()}))

    def test_verified_round_trip_excludes_saves_and_preserves_inputs(self):
        before = {path: path.read_bytes() for path in self.game.rglob("*") if path.is_file()}
        self.prepare()
        release_data.unpack(self.archive, self.output)
        actual = {str(path.relative_to(self.output)).replace("\\", "/")
                  for path in self.output.rglob("*") if path.is_file()}
        self.assertEqual(actual, {"data/" + name for name in packager.GAME_FILES} | {"media/FTAVOICE.HRS"})
        self.assertEqual((self.output / "media/FTAVOICE.HRS").read_bytes(), self.voice.read_bytes())
        for path, payload in before.items():
            self.assertEqual(path.read_bytes(), payload)

    def test_changed_archive_pin_rejected_before_extraction(self):
        self.prepare()
        with self.archive.open("ab") as stream:
            stream.write(b"changed")
        with self.assertRaisesRegex(ValueError, "pinned size and SHA-256"):
            release_data.unpack(self.archive, self.output)
        self.assertFalse(self.output.exists())

    def test_unexpected_and_duplicate_entries_rejected(self):
        for extra in ("../outside", "data/999.SAV", "data/FTA2.EXE"):
            with self.subTest(extra=extra):
                self.archive.unlink(missing_ok=True)
                self.prepare()
                with ZipFile(self.archive, "a") as archive:
                    # Deliberately duplicate an entry to exercise the archive guard.
                    with warnings.catch_warnings():
                        warnings.simplefilter("ignore", UserWarning)
                        archive.writestr(extra, b"extra")
                self.repin()
                with self.assertRaisesRegex(ValueError, "Unexpected or duplicate"):
                    release_data.unpack(self.archive, self.output)
                self.assertFalse(self.output.exists())

    def test_file_hashes_verified_independently_of_archive_pin(self):
        self.prepare()
        with ZipFile(self.archive) as archive:
            payloads = {info.filename: archive.read(info) for info in archive.infolist()}
        payloads["data/FTA2.EXE"] = b"X" * len(payloads["data/FTA2.EXE"])
        with ZipFile(self.archive, "w") as archive:
            for name, payload in payloads.items():
                archive.writestr(name, payload)
        self.repin()
        with self.assertRaisesRegex(ValueError, "damaged release data: data/FTA2.EXE"):
            release_data.unpack(self.archive, self.output)
        self.assertFalse(self.output.exists())

    def test_existing_outputs_and_original_inputs_are_protected(self):
        self.prepare()
        before = self.archive.read_bytes()
        with self.assertRaisesRegex(ValueError, "already exists"):
            release_data.create(self.game, self.voice, self.archive)
        self.assertEqual(self.archive.read_bytes(), before)
        with self.assertRaisesRegex(ValueError, "outside"):
            release_data.create(self.game, self.voice, self.game / "archive.zip")
        self.output.mkdir()
        with self.assertRaisesRegex(ValueError, "already exists"):
            release_data.unpack(self.archive, self.output)

    def test_unverified_voice_rejected_before_output(self):
        self.voice.write_bytes(b"unverified")
        with self.assertRaisesRegex(ValueError, "damaged release data: media/FTAVOICE.HRS"):
            release_data.create(self.game, self.voice, self.archive)
        self.assertFalse(self.archive.exists())

    def test_download_uses_repository_input_and_verifies_it_before_returning(self):
        metadata = self.prepare()
        metadata["release_tag"] = "release-inputs-v1"
        self.config.write_text(json.dumps(metadata))
        downloaded = self.root / "downloaded"
        def fetch(command, check):
            self.assertEqual(command, ["gh", "release", "download", "release-inputs-v1", "--repo",
                                       "owner/repository", "--pattern", self.archive.name,
                                       "--dir", str(downloaded)])
            self.assertTrue(check)
            (downloaded / self.archive.name).write_bytes(self.archive.read_bytes())
        with patch.dict(release_data.os.environ, {"GITHUB_REPOSITORY": "owner/repository"}), \
                patch.object(release_data.subprocess, "run", side_effect=fetch):
            self.assertEqual(release_data.download(downloaded), downloaded / "verified")
        self.assertEqual((downloaded / "verified/media/FTAVOICE.HRS").read_bytes(), self.voice.read_bytes())

    def test_failed_download_does_not_fall_back_to_unverified_data(self):
        metadata = self.prepare()
        metadata["release_tag"] = "release-inputs-v1"
        self.config.write_text(json.dumps(metadata))
        downloaded = self.root / "downloaded"
        with patch.dict(release_data.os.environ, {"GITHUB_REPOSITORY": "owner/repository"}), \
                patch.object(release_data.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "gh")):
            with self.assertRaises(ValueError) as failure:
                release_data.download(downloaded)
        message = str(failure.exception)
        self.assertIn(self.archive.name, message)
        self.assertIn("owner/repository release release-inputs-v1", message)
        self.assertIn("docs/releases.md", message)
        self.assertIn("contents: write", message)
        self.assertIn("rerun the failed release job", message)
        self.assertIsInstance(failure.exception.__cause__, subprocess.CalledProcessError)
        self.assertFalse((downloaded / "verified").exists())


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.engine = self.root / "runtime.zip"
        self.source = self.root / "game-1-source.zip"
        self.output = self.root / "release.zip"
        with ZipFile(self.engine, "w") as z:
            runtime_zip(z, "game-1-Windows")
        with ZipFile(self.source, "w") as z:
            z.writestr("game-1-source/CMakeLists.txt", b"build instructions")
            z.writestr("game-1-source/LICENSE", b"licence")

    def build(self):
        return package_release.package_release(self.engine, self.source, self.output)

    def test_source_is_inside_single_repeatable_runtime(self):
        manifest = self.build()
        with ZipFile(self.output) as z:
            self.assertEqual(z.read("game-1-Windows/" + manifest["file"]), self.source.read_bytes())
            self.assertEqual(z.read("game-1-Windows/bin/fta2-ce.exe"), b"engine")
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
            z.writestr("game-1-Windows/source/manifest.json", b"conflict")
        with self.assertRaisesRegex(ValueError, "Unnecessary runtime file"):
            self.build()

    def test_common_layout_on_all_platforms(self):
        for platform in ("Windows", "Linux", "Darwin"):
            with self.subTest(platform=platform):
                root = f"game-1-{platform}-x86_64"
                with ZipFile(self.engine, "w") as z:
                    runtime_zip(z, root, platform)
                output = self.root / (platform + ".zip")
                package_release.package_release(self.engine, self.source, output)
                with ZipFile(output) as z:
                    # ZIP member names use POSIX separators on every host.
                    files = {str(PurePosixPath(n).relative_to(root)) for n in z.namelist()}
                    self.assertIn("START-HERE.txt", files)
                    self.assertIn("licenses/LICENSE", files)
                    self.assertIn("source/game-1-source.zip", files)
                    self.assertFalse(any("native" in n for n in files))

    def test_unneeded_player_files_rejected(self):
        for extra in ("docs/architecture.md", "share/icons/icon.png", "bin/developer-test.exe",
                      "lib/libSDL3-debug.a", "screenshots/capture.png", "assets/fta2.ico"):
            with self.subTest(extra=extra):
                with ZipFile(self.engine, "w") as z:
                    runtime_zip(z, "game-1-Windows")
                    z.writestr("game-1-Windows/" + extra, b"not needed by players")
                with self.assertRaisesRegex(ValueError, "Unnecessary runtime file"):
                    self.build()
                self.assertFalse(self.output.exists())

    def test_existing_output_preserved(self):
        self.output.write_bytes(b"existing")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.build()
        self.assertEqual(self.output.read_bytes(), b"existing")


if __name__ == "__main__":
    unittest.main()
