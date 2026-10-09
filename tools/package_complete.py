#!/usr/bin/env python3
"""Build a complete Windows ZIP from an engine ZIP and verified local game data."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tempfile
from zipfile import ZIP_DEFLATED, BadZipFile, ZipFile, ZipInfo

PROFILE_PATH = Path(__file__).resolve().parents[1] / "research/original-files.json"
GAME_FILES = (
    "FTA2.EXE", "FTA.HRS", "FTADATA.HRS", "FTAIMAGE.HRS", "FTASOUND.HRS",
    "SCRIPTS.HRS", "DRIVERS/SAMPLE.AD", "DRIVERS/SAMPLE.OPL",
)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def package_complete(engine_package: Path, data_dir: Path, output: Path):
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
        additions[f"{root}/share/fta2-native/complete-package.json"] = (
            json.dumps(manifest, indent=2) + "\n"
        ).encode()
        additions[f"{root}/START-HERE.txt"] = (
            "Faery Tale Adventure II - complete Windows package\n\n"
            "Extract the entire ZIP and double-click bin/fta2-native.exe.\n"
            "Keep bin, data and share together when moving the game.\n"
            "Game data is included; no folder selection or separate installation is needed.\n"
            "Saves and settings are stored in your Windows user profile.\n\n"
            "This is a development alpha. A full playthrough remains pending.\n"
        ).encode()
        if any(name.casefold() in seen for name in additions):
            raise ValueError("Engine ZIP conflicts with complete-package metadata")

        output.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=output.parent, suffix=".zip", delete=False) as tmp:
                temporary = Path(tmp.name)
            with ZipFile(temporary, "w", compression=ZIP_DEFLATED, compresslevel=9) as archive:
                for info, _ in members:
                    archive.writestr(info, engine.read(info))
                for name, content in sorted(additions.items()):
                    info = ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                    info.compress_type = ZIP_DEFLATED
                    info.external_attr = 0o100644 << 16
                    archive.writestr(info, content, compresslevel=9)
            temporary.replace(output)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-package", required=True, type=Path)
    parser.add_argument("--data-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        manifest = package_complete(args.engine_package, args.data_dir, args.output)
    except (OSError, ValueError, BadZipFile) as error:
        parser.exit(1, f"package_complete: {error}\n")
    print(json.dumps({"output": str(args.output), "data_files": len(manifest["files"]),
                      "sha256": sha256(args.output.read_bytes())}, indent=2))


if __name__ == "__main__":
    main()
