#!/usr/bin/env python3
"""Exercise validated CD speech without modifying the DOS installation or media."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
VOICE_HASH = "34bd5c7e3b5982df3f84b274c05a0f056a2932dfd03f02d880be44e5a81353c3"
VOICE_SIZE = 268574344


def digest(path):
    with path.open("rb") as stream:
        checksum = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
        return checksum.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--voice-file", type=Path, required=True)
    parser.add_argument("--data-dir", type=Path, default=ROOT / "original-game")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    binary, voice, data, output = (p.resolve() for p in
                                  (args.binary, args.voice_file, args.data_dir, args.output))
    if output == data or data in output.parents or output.exists():
        raise ValueError("Use a fresh output directory outside the original installation")
    if voice.stat().st_size != VOICE_SIZE or digest(voice) != VOICE_HASH:
        raise ValueError("Unsupported CD voice archive")
    before = {str(p.relative_to(data)): digest(p) for p in data.rglob("*") if p.is_file()}
    output.mkdir(parents=True)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    results = []
    for wide in (False, True):
        for alternate in (False, True):
            run = output / f"wide-{int(wide)}-layout-{int(alternate)}"
            saves, captures = run / "saves", run / "output"
            saves.mkdir(parents=True)
            # Exercise migration of the forced mute persisted by old native releases.
            (saves / "config.ini").write_text(
                f"widescreen={str(wide).lower()}\nalternate_sidebar={str(alternate).lower()}\n"
                "speech_mute=true\nspeech_volume=192\nsubtitles=true\n")
            with (run / "native.log").open("w") as log:
                subprocess.run([str(binary), "--data-dir", str(data), "--voice-file", str(voice),
                                "--save-dir", str(saves), "--smoke-test", str(captures),
                                "--smoke-voices"], env=env, check=True, timeout=90,
                               stdout=log, stderr=subprocess.STDOUT)
            result = json.loads((captures / "voice-playback.json").read_text())
            assert result["status"] == "passed"
            assert "speech_mute=false" in (saves / "config.ini").read_text()
            results.append({"widescreen": wide, "alternate_sidebar": alternate, **result})
    damaged = output / "damaged-voice.hrs"
    damaged.write_bytes(b"HRES")
    rejected = subprocess.run([str(binary), "--data-dir", str(data), "--voice-file", str(damaged),
                               "--save-dir", str(output / "damaged-saves")], env=env,
                              capture_output=True, text=True, timeout=15)
    assert rejected.returncode == 1 and "Unsupported voice archive" in rejected.stderr
    assert all(digest(data / name) == sha for name, sha in before.items())
    assert digest(voice) == VOICE_HASH
    report = {"status": "passed", "voice_sha256": VOICE_HASH,
              "original_files_unchanged": len(before), "voice_unchanged": True,
              "damaged_explicit_archive_rejected": True, "runs": results}
    (output / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
