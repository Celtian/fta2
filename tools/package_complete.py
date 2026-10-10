#!/usr/bin/env python3
"""Build one Windows release with sources, verified local data and optional voices."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
from zipfile import BadZipFile, ZipFile

from package_release import source_additions, write_bundle
from package_voices import VOICE_SHA256, VOICE_SIZE

PROFILE_PATH = Path(__file__).resolve().parents[1] / "research/original-files.json"
GAME_FILES = (
    "FTA2.EXE", "FTA.HRS", "FTADATA.HRS", "FTAIMAGE.HRS", "FTASOUND.HRS",
    "SCRIPTS.HRS", "DRIVERS/SAMPLE.AD", "DRIVERS/SAMPLE.OPL",
)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def package_complete(engine_package: Path, data_dir: Path, output: Path,
                     source_package: Path = None, voice_file: Path = None):
    engine_package, data_dir, output = (
        engine_package.resolve(), data_dir.resolve(), output.resolve()
    )
    if output == data_dir or data_dir in output.parents:
        raise ValueError("Package output must be outside the original game directory")
    if output.exists():
        raise ValueError(f"Output already exists: {output}")

    profile = json.loads(PROFILE_PATH.read_text(encoding="utf-8"))["files"]
    payloads, manifest_files = {}, {}
    for name in GAME_FILES:
        path = data_dir / name
        if not path.is_file():
            raise ValueError(f"Required game file is missing: {name}")
        payload = path.read_bytes()
        actual = sha256(payload)
        expected = profile[name]
        if len(payload) != expected["size"] or actual != expected["sha256"]:
            raise ValueError(
                f"Unsupported or damaged {name}: expected SHA-256 "
                f"{expected['sha256']}, found {actual}"
            )
        payloads[name] = payload
        manifest_files[name] = {"size": len(payload), "sha256": actual}

    with ZipFile(engine_package) as engine:
        members, seen = [], set()
        for info in engine.infolist():
            path = PurePosixPath(info.filename)
            if (path.is_absolute() or ".." in path.parts or "\\" in info.filename
                    or not path.parts or ":" in info.filename):
                raise ValueError(f"Unsafe engine ZIP entry: {info.filename}")
            key = str(path).casefold()
            if key in seen:
                raise ValueError(f"Duplicate engine ZIP entry: {info.filename}")
            seen.add(key)
            members.append((info, path))
        roots = {path.parts[0] for _, path in members}
        if len(roots) != 1:
            raise ValueError("Engine ZIP must contain a single package directory")
        root = next(iter(roots))
        for required in ("bin/fta2-native.exe", "bin/SDL3.dll"):
            if f"{root}/{required}".casefold() not in seen:
                raise ValueError(f"Windows engine ZIP is missing {required}")
        for info, path in members:
            if (len(path.parts) > 1 and path.parts[1].casefold() == "data"
                    or path.suffix.casefold() in (".hrs", ".sav", ".ppm", ".bin")
                    or path.name.casefold() == "fta2.exe"):
                raise ValueError(f"Engine ZIP already contains game/development data: {info.filename}")

        manifest = {
            "schema_version": 1,
            "profile": "english-dos-a7413ddf",
            "engine_archive_sha256": sha256(engine_package.read_bytes()),
            "data_license": "Original game data retains its original copyright; "
                            "the engine's GPL does not relicense it.",
            "files": manifest_files,
        }
        additions = {f"{root}/data/{name}": content for name, content in payloads.items()}
        if source_package is not None:
            sources, manifest["source"] = source_additions(source_package, root)
            additions.update(sources)
        if voice_file is not None:
            if voice_file.stat().st_size != VOICE_SIZE:
                raise ValueError("Unsupported voice archive size")
            voice = voice_file.read_bytes()
            if sha256(voice) != VOICE_SHA256:
                raise ValueError("Unsupported or damaged voice archive")
            manifest["voice"] = {"file": "media/FTAVOICE.HRS", "size": VOICE_SIZE,
                                 "sha256": VOICE_SHA256, "recordings": 4320,
                                 "license": "Original game audio; not relicensed under the engine GPL"}
            additions[f"{root}/media/FTAVOICE.HRS"] = voice
            additions[f"{root}/media/voice-manifest.json"] = (
                json.dumps(manifest["voice"], indent=2) + "\n"
            ).encode()
        additions[f"{root}/share/fta2-native/complete-package.json"] = (
            json.dumps(manifest, indent=2) + "\n"
        ).encode()
        additions[f"{root}/START-HERE.txt"] = (
            "Faery Tale Adventure II - complete Windows package\n\n"
            "Extract the entire ZIP and double-click bin/fta2-native.exe.\n"
            "Keep the extracted directory together when moving the game.\n"
            "Game data is included; no folder selection or separate installation is needed.\n"
            + ("Original English voices are included and detected automatically.\n"
               "Use Speech Volume and Speech Text in Options to adjust voices and subtitles.\n"
               if voice_file is not None else "")
            + ("Corresponding engine sources are included under source/.\n"
               if source_package is not None else "")
            + "Saves and settings are stored in your Windows user profile.\n"
            "Diagnostic logs: %APPDATA%\\fta2-native\\fta2-native\\logs\n"
            "When reporting a crash, include the latest .log and matching .dmp, if present.\n\n"
            "Original game data and audio retain their original copyright.\n"
            "This is a development alpha. A full playthrough remains pending.\n"
        ).encode()
        write_bundle(engine, members, additions, output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-package", required=True, type=Path)
    parser.add_argument("--data-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--source-package", required=True, type=Path)
    parser.add_argument("--voice-file", type=Path)
    args = parser.parse_args()
    try:
        manifest = package_complete(args.engine_package, args.data_dir, args.output,
                                    args.source_package, args.voice_file)
    except (OSError, ValueError, BadZipFile) as error:
        parser.exit(1, f"package_complete: {error}\n")
    print(json.dumps({"output": str(args.output), "data_files": len(manifest["files"]),
                      "sha256": sha256(args.output.read_bytes())}, indent=2))


if __name__ == "__main__":
    main()
