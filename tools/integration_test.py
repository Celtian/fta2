#!/usr/bin/env python3
"""Run the private-data integration checks without modifying the installation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from fta2_data import EXE_SHA256, extract, sha256
from integration_widescreen import validate_container_owners


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--data-dir", type=Path, default=ROOT / "original-game")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    binary, game = args.binary.resolve(), args.data_dir.resolve()
    before = {str(p.relative_to(game)): sha256(p.read_bytes()) for p in game.rglob("*") if p.is_file()}
    with tempfile.TemporaryDirectory(prefix="fta2-integration-") as temp:
        work = args.output.resolve() if args.output else Path(temp)
        if work == game or game in work.parents:
            raise RuntimeError("Test output must be outside originals")
        saves, output = work / "saves", work / "output"
        saves.mkdir(parents=True, exist_ok=True)
        output.mkdir(exist_ok=True)
        assert not (output / "state.jsonl").exists(), "Use a fresh integration output directory"
        shutil.copyfile(game / "999.SAV", saves / "fta2.002")
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        with (output / "native.log").open("w") as log:
            subprocess.run([str(binary), "--data-dir", str(game), "--save-dir", str(saves),
                            "--smoke-test", str(output)], env=env, check=True, timeout=300,
                           stdout=log, stderr=subprocess.STDOUT)
        states = [json.loads(line) for line in (output / "state.jsonl").read_text().splitlines()]
        assert states[-1]["phase"] == "finished"
        assert any(s["phase"] == "original-save-loaded" for s in states)
        assert any(s["phase"] == "hotkeys-checked" for s in states)
        assert any(s["phase"] == "music-toggle-checked" for s in states)
        assert any(s["phase"] == "options-regressions-checked" for s in states)
        assert any(s["phase"] == "speech-text-checked" for s in states)
        assert any(s["phase"] == "document-cursors-checked" for s in states)
        assert any(s["phase"] == "document-wheel-checked" for s in states)
        assert any(s["phase"] == "container-escape-checked" for s in states)
        assert any(s["phase"] == "map-shortcut-checked" for s in states)
        assert json.loads((output / "speech-startup.json").read_text())["subtitles"] is True
        assert "subtitles=false" in (saves / "config.ini").read_text()
        # A second native process must retain the unchecked preference.
        restart_saves, restart_output = work / "restart-saves", work / "restart-output"
        restart_saves.mkdir()
        restart_output.mkdir()
        shutil.copyfile(saves / "config.ini", restart_saves / "config.ini")
        shutil.copyfile(game / "999.SAV", restart_saves / "fta2.002")
        with (restart_output / "native.log").open("w") as log:
            subprocess.run([str(binary), "--data-dir", str(game), "--save-dir", str(restart_saves),
                            "--smoke-test", str(restart_output)], env=env, check=True, timeout=300,
                           stdout=log, stderr=subprocess.STDOUT)
        assert json.loads((restart_output / "speech-startup.json").read_text())["subtitles"] is False
        restart_states = [json.loads(line) for line in (restart_output / "state.jsonl").read_text().splitlines()]
        assert restart_states[-1]["phase"] == "finished"
        assert any(s["phase"] == "speech-text-checked" for s in restart_states)
        assert any(s["phase"] == "document-cursors-checked" for s in restart_states)
        assert any(s["phase"] == "document-wheel-checked" for s in restart_states)
        assert any(s["phase"] == "container-escape-checked" for s in restart_states)
        assert any(s["phase"] == "map-shortcut-checked" for s in restart_states)
        for variant in ("game", "pregame"):
            for kind in ("scroll", "parchment"):
                for capture_dir in (output, restart_output):
                    before_page = (capture_dir / f"wheel-{variant}-{kind}-before.ppm").read_bytes()
                    after_page = (capture_dir / f"wheel-{variant}-{kind}-after.ppm").read_bytes()
                    header = b"P6\n640 480\n255\n"
                    assert before_page.startswith(header) and after_page.startswith(header)
                    assert len(before_page) == len(after_page) == len(header) + 640 * 480 * 3
                    assert before_page != after_page
            for name in ("left", "right", "bookmark", "closed-escape", "closed-bookmark"):
                for capture_dir in (output, restart_output):
                    screenshot = (capture_dir / f"cursor-{variant}-{name}.ppm").read_bytes()
                    header = b"P6\n640 480\n255\n"
                    assert screenshot.startswith(header) and len(screenshot) == len(header) + 640 * 480 * 3
        for capture_dir in (output, restart_output):
            validate_container_owners(capture_dir)
            assert json.loads((capture_dir / "container-tooltip.json").read_text()) == {
                "status": "passed", "physical_closes": 12, "delayed_text_cleared": True}
        for name in ("map-brother-1.ppm", "map-brother-2.ppm", "map-brother-3.ppm",
                     "map-click-brother-1.ppm", "map-click-brother-2.ppm", "map-click-brother-3.ppm",
                     "map-missing.ppm", "container-open.ppm", "container-closed.ppm",
                     "options.ppm", "hotkeys.ppm", "options-returned.ppm",
                     "pregame-options.ppm", "pregame-hotkeys.ppm", "pregame-returned.ppm",
                     "load-empty.ppm", "credits.ppm", "credits-next.ppm", "credits-last.ppm",
                     "save-menu.ppm", "load-saved.ppm", "speech-options-on.ppm",
                     "speech-options-off.ppm", "speech-pregame-on.ppm", "speech-pregame-off.ppm",
                     "speech-no-voice.ppm", "speech-missing-voice.ppm"):
            for capture_dir in (output, restart_output):
                screenshot = (capture_dir / name).read_bytes()
                header = b"P6\n640 480\n255\n"
                assert screenshot.startswith(header) and len(screenshot) == len(header) + 640 * 480 * 3
        assert (output / "credits.ppm").read_bytes() != (output / "credits-next.ppm").read_bytes()
        saved = next(s for s in states if s["phase"] == "saved")
        restored = next(s for s in states if s["phase"] == "restored")
        assert all(saved[k] == restored[k] for k in ["actor", "u", "v", "z", "rng"])
        independent = work / "python-extraction"
        manifest = extract(game, independent)
        native = saves / "resources/fta2-dos-resources.bin"
        assert native.read_bytes() == (independent / native.name).read_bytes()
        assert json.loads((saves / "resources/manifest.json").read_text()) == manifest
        unknown = work / "unknown-game"
        unknown.mkdir()
        (unknown / "FTA2.EXE").write_bytes(b"MZ" + bytes(100))
        rejected = subprocess.run([str(binary), "--data-dir", str(unknown), "--save-dir",
                                   str(work / "unknown-saves")], env=env, capture_output=True,
                                  text=True, timeout=10)
        assert rejected.returncode == 1 and "Unsupported FTA2.EXE" in rejected.stderr
        assert EXE_SHA256 in rejected.stderr
        assert not (work / "unknown-saves/resources").exists()
        rejected = subprocess.run([str(binary), "--data-dir", str(game), "--save-dir",
                                   str(game / "must-not-be-created")], env=env,
                                  capture_output=True, text=True, timeout=10)
        assert rejected.returncode == 1 and "outside" in rejected.stderr
        assert not (game / "must-not-be-created").exists()
        assert all(sha256((game / name).read_bytes()) == expected for name, expected in before.items())
        print(json.dumps({"status": "passed", "original_files_unchanged": len(before),
                          "unknown_exe_rejected": True, "output_inside_originals_rejected": True,
                          "pack_sha256": manifest["pack_sha256"], "states": states,
                          "speech_preference_restart": {"first_start": True, "second_start": False},
                          "audio": json.loads((output / "audio.json").read_text())}, indent=2))

if __name__ == "__main__":
    main()
