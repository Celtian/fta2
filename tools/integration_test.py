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
                            "--smoke-test", str(output)], env=env, check=True, timeout=120,
                           stdout=log, stderr=subprocess.STDOUT)
        states = [json.loads(line) for line in (output / "state.jsonl").read_text().splitlines()]
        assert states[-1]["phase"] == "finished"
        assert any(s["phase"] == "original-save-loaded" for s in states)
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
                          "audio": json.loads((output / "audio.json").read_text())}, indent=2))

if __name__ == "__main__":
    main()
