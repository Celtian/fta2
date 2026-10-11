#!/usr/bin/env python3
"""Build a desktop release with sources, verified game data and optional voices."""

import argparse
import hashlib
import json
from pathlib import Path
from zipfile import BadZipFile, ZipFile

from package_release import safe_members, source_additions, write_bundle, validate_runtime_layout
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
        # Validate relative library links as well as names before copying ZIP entries.
        root, members = safe_members(engine, allow_symlinks=True)
        seen = {str(path).casefold() for _, path in members}
        for info, path in members:
            if (len(path.parts) > 1 and path.parts[1].casefold() == "data"
                    or path.suffix.casefold() in (".hrs", ".sav", ".ppm", ".bin")
                    or path.name.casefold() == "fta2.exe"):
                raise ValueError(f"Engine ZIP already contains game/development data: {info.filename}")

        validate_runtime_layout(root, members)
        windows = f"{root}/bin/fta2-ce.exe".casefold() in seen
        mac = f"{root}/Faery Tale Adventure II.command".casefold() in seen
        launcher = ("Faery Tale Adventure II.exe" if windows else
                    "Faery Tale Adventure II.command" if mac else "Faery Tale Adventure II")
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
        additions[f"{root}/bin/package.json"] = (
            json.dumps(manifest, indent=2) + "\n"
        ).encode()
        additions[f"{root}/START-HERE.txt"] = (
            "Faery Tale Adventure II: Community Edition - complete desktop package\n\n"
            f"Extract the entire ZIP and open {launcher}.\n"
            + ("On Linux/macOS, allow execution of the launcher if required.\n" if not windows else "")
            + "Keep the extracted directory together when moving the game.\n"
            "Game data is included; no folder selection or separate installation is needed.\n"
            + ("Original English voices are included and detected automatically.\n"
               "Use Speech Volume and Speech Text in Options to adjust voices and subtitles.\n"
               if voice_file is not None else "")
            + ("Corresponding engine sources are included under source/.\n"
               "Build instructions, developer documentation and validation records are inside that ZIP.\n"
               if source_package is not None else "")
            + "Engine and third-party licence notices are under licenses/.\n"
            "Saves and settings are stored in your user profile.\n"
            "Print Screen saves a PNG, including the cursor, under screenshots/ beside this file.\n"
            "If this directory is not writable, screenshots use the user profile.\n"
            "Diagnostic logs are in logs/ under the selected user/save directory.\n"
            "When reporting a crash, include the latest .log and matching .dmp, if present.\n\n"
            "Original game data and audio retain their original copyright.\n"
            "This is a development alpha. A full playthrough remains pending.\n"
        ).encode()
        # The complete edition replaces the common engine-only startup note.
        members = [(info, path) for info, path in members
                   if str(path) != f"{root}/START-HERE.txt"]
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
