#!/usr/bin/env python3
"""Validate persistent gameplay logs and isolated original villager-death regressions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--voice-file", type=Path)
    parser.add_argument("--data-dir", type=Path, default=ROOT / "original-game")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    binary, game, output = args.binary.resolve(), args.data_dir.resolve(), args.output.resolve()
    assert not output.exists(), "Use a fresh output directory"
    assert game not in output.parents and game != output
    before = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in game.rglob("*") if p.is_file()}
    output.mkdir(parents=True)
    results = []
    for actor in (32788, 32797):
        for wide in (False, True):
            for layout in (False, True):
                name = f"death-{actor}-wide-{int(wide)}-layout-{int(layout)}"
                work = output / name
                saves, captures = work / "saves", work / "output"
                saves.mkdir(parents=True)
                captures.mkdir()
                (saves / "config.ini").write_text(
                    f"widescreen={str(wide).lower()}\nalternate_sidebar={str(layout).lower()}\n"
                    "subtitles=true\nspeech_volume=192\n", encoding="utf-8")
                command = [str(binary), "--data-dir", str(game), "--save-dir", str(saves),
                           "--smoke-test", str(captures), "--smoke-death", str(actor)]
                if args.voice_file:
                    command += ["--voice-file", str(args.voice_file.resolve())]
                with (work / "process.log").open("w") as log:
                    subprocess.run(command, env=dict(os.environ, SDL_VIDEODRIVER="dummy",
                                   SDL_AUDIODRIVER="dummy"), stdout=log, stderr=subprocess.STDOUT,
                                   timeout=180, check=True)
                result = json.loads((captures / "villager-death.json").read_text())
                assert result["status"] == "passed" and result["actor"] == actor
                logs = list((saves / "logs").glob("*.log"))
                assert len(logs) == 1
                text = logs[0].read_text(encoding="utf-8")
                assert f"die actor={actor}" in text, "Original death callback was not reached"
                assert "Game exited normally" in text and "FATAL:" not in text
                assert "[WORLD]" in text and "[SAVE] Completed slot=999" in text
                results.append({"actor": actor, "widescreen": wide, "new_layout": layout,
                                "status": "passed"})
    # A caught startup failure must also persist its reason before returning.
    bad = output / "invalid-voice"
    bad.mkdir()
    result = subprocess.run([str(binary), "--data-dir", str(game), "--save-dir", str(bad),
                             "--voice-file", str(bad / "missing.hrs")],
                            env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy"),
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    assert result.returncode == 1 and b"Diagnostic log:" in result.stderr
    assert "FATAL:" in next((bad / "logs").glob("*.log")).read_text()
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == value for p, value in before.items())
    report = {"status": "passed", "runs": results, "caught_startup_failure_logged": True,
              "original_files_unchanged": len(before), "video": "dummy", "audio": "dummy"}
    (output / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
