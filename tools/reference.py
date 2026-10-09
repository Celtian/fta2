#!/usr/bin/env python3
"""Build/run the pinned development reference separately from the product."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REVISION = "1a3e816afd5ddc41cc2a5c51a5da292e6d61758c"

def run(args, cwd=None):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["build", "run"])
    parser.add_argument("--work-dir", type=Path, default=ROOT / ".cache/reference")
    parser.add_argument("--data-dir", type=Path, default=ROOT / "original-game")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    work = args.work_dir.resolve()
    source, build = work / "source", work / "build"
    work.mkdir(parents=True, exist_ok=True)
    if args.command == "build":
        if not (source / ".git").exists():
            source.mkdir(exist_ok=True)
            run(["git", "init", source])
            run(["git", "remote", "add", "origin", "https://github.com/scummvm/scummvm.git"], source)
            run(["git", "fetch", "--depth=1", "origin", REVISION], source)
            run(["git", "checkout", "--detach", "FETCH_HEAD"], source)
        head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
        if head != REVISION:
            raise RuntimeError("Reference checkout is at an unexpected revision")
        for name in ["dos-data.patch", "capture-probe.patch", "deterministic-motion.patch"]:
            patch = ROOT / "reference/patches" / name
            applied = subprocess.run(["git", "apply", "--reverse", "--check", str(patch)], cwd=source, capture_output=True)
            if applied.returncode:
                run(["git", "apply", "--check", patch], source)
                run(["git", "apply", patch], source)
        build.mkdir(exist_ok=True)
        run([source / "configure", "--backend=null", "--disable-all-engines", "--enable-engine=saga2",
             "--disable-detection-full", "--disable-mt32emu", "--disable-fluidsynth",
             "--disable-libcurl", "--disable-sdlnet", "--disable-nasm"], build)
        run(["make", f"-j{max(1, args.jobs)}"], build)
    else:
        sys.path.insert(0, str(ROOT / "tools"))
        from fta2_data import extract
        game = args.data_dir.resolve()
        if work == game or game in work.parents:
            raise RuntimeError("Reference output must be outside original data")
        staging, output, saves = work / "game", work / "output", work / "saves"
        for path in [staging, output, saves]:
            path.mkdir(exist_ok=True)
        extract(game, staging)
        for path in game.iterdir():
            if path.name.upper() in {"FTAVOICE.HRS", "999.SAV"}:
                continue
            link = staging / path.name
            if not link.exists():
                link.symlink_to(path, target_is_directory=path.is_dir())
        config = work / "reference.ini"
        config.write_text(f"""[scummvm]
native_probe=true
native_fixed_clock=true
native_output={output}
savepath={saves}
subtitles=true
speech_mute=true
music_driver=adlib
opl_driver=nuked
random_seed=42
[fta2]
engineid=saga2
gameid=fta2
path={staging}
""")
        with (output / "reference.log").open("w") as log:
            subprocess.run([str(build / "scummvm"), f"--config={config}", "fta2"], stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=120)
        print(json.dumps({"output": str(output), "saves": str(saves)}))

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"reference: {exc}", file=sys.stderr)
        sys.exit(1)
