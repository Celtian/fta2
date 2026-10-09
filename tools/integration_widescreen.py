#!/usr/bin/env python3
"""Exercise live widescreen switching with immutable local original game data."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

from fta2_data import sha256
from compare_reference import chunks

ROOT = Path(__file__).resolve().parents[1]


def validate_exploration(output, saves):
    """Check isolated exploration evidence, including actual rendered map interiors."""
    exploration = json.loads((output / "exploration.json").read_text())
    assert exploration["status"] == "passed"
    assert exploration["footprints_compared"] == 16
    assert exploration["brother_maps_checked"] == 14
    assert exploration["world_switches_checked"] == 2
    assert exploration["shared_world_exploration"] and exploration["fixture_restored"]
    assert exploration["radius_metatiles"] == 2 and exploration["corners_excluded"]
    assert len(exploration["samples"]) == 30
    for mode in ("classic", "wide"):
        for brother in range(3):
            samples = [s for s in exploration["samples"]
                       if s["mode"] == mode and s["brother"] == brother]
            assert [s["step"] for s in samples] == list(range(5))
            assert samples[-1]["u"] - samples[0]["u"] == 128
            assert samples[-1]["v"] - samples[0]["v"] == 128
            if mode == "wide":
                assert all(s["side_pixels"] == 214 * 420 for s in samples)
    before_fixture, restored_fixture = chunks(saves / "fta2.003"), chunks(saves / "fta2.004")
    for tag in (b"AMAP", b"NRNG"):
        assert before_fixture[tag] == restored_fixture[tag], tag
    maps = {}
    for mode, width, origin_x in (("classic", 640, 52), ("wide", 854, 155)):
        for brother in range(3):
            for step in ((0, 4, 5) if brother == 2 else (0, 4)):
                image = (output / f"exploration-{mode}-{brother}-{step}.ppm").read_bytes()
                header = f"P6\n{width} 480\n255\n".encode()
                assert image.startswith(header) and len(image) == len(header) + width * 480 * 3
                pixels = image[len(header):]
                # Compare the opaque 524 x 298 map interior, excluding its shadow.
                # Classic retains the original (52,54) dialog position; wide centers at (155,54).
                crop = b"".join(pixels[(y * width + origin_x + 10) * 3:
                                       (y * width + origin_x + 534) * 3]
                                for y in range(64, 362))
                if mode == "classic":
                    maps[brother, step] = crop
                else:
                    assert crop == maps[brother, step], (brother, step)
    return dict(exploration, map_interior_parity=True, map_interiors_compared=7)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--data-dir", type=Path, default=ROOT / "original-game")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    binary, game, work = args.binary.resolve(), args.data_dir.resolve(), args.output.resolve()
    if work == game or game in work.parents:
        raise RuntimeError("Test output must be outside originals")
    before = {str(p.relative_to(game)): sha256(p.read_bytes())
              for p in game.rglob("*") if p.is_file()}
    work.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    results = []
    for index in range(2):
        saves, output = work / f"saves-{index}", work / f"output-{index}"
        saves.mkdir()
        output.mkdir()
        shutil.copyfile(game / "999.SAV", saves / "fta2.002")
        if index:
            shutil.copyfile(work / "saves-0/config.ini", saves / "config.ini")
        with (output / "native.log").open("w") as log:
            subprocess.run([str(binary), "--data-dir", str(game), "--save-dir", str(saves),
                            "--smoke-test", str(output), "--smoke-widescreen"],
                           env=env, check=True, timeout=120, stdout=log, stderr=subprocess.STDOUT)
        startup = json.loads((output / "startup.json").read_text())
        assert startup == {"widescreen": bool(index), "width": 854 if index else 640}
        result = json.loads((output / "widescreen.json").read_text())
        assert result["status"] == "passed"
        assert result["checkbox_redraw_parity"] and result["checkbox_edge_clicks"]
        assert result["options_layouts_checked"] == 4
        exploration = validate_exploration(output, saves)
        assert "widescreen=true" in (saves / "config.ini").read_text()
        for name in ("wide-group.ppm", "wide-individual.ppm", "wide-final.ppm"):
            image = (output / name).read_bytes()
            header = b"P6\n854 480\n255\n"
            assert image.startswith(header) and len(image) == len(header) + 854 * 480 * 3
        for mode, width in (("classic", 640), ("wide", 854)):
            for menu in ("game", "pregame"):
                for state in ("checked", "unchecked"):
                    image = (output / f"options-{mode}-{menu}-{state}.ppm").read_bytes()
                    header = f"P6\n{width} 480\n255\n".encode()
                    assert image.startswith(header) and len(image) == len(header) + width * 480 * 3
        results.append({"startup": startup, "checks": result, "exploration": exploration})
    after = {str(p.relative_to(game)): sha256(p.read_bytes())
             for p in game.rglob("*") if p.is_file()}
    assert before == after
    report = {"status": "passed", "original_files_unchanged": len(before), "runs": results,
              "backends": {"video": "dummy", "audio": "dummy"}}
    (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
