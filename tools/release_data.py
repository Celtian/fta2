#!/usr/bin/env python3
"""Prepare and verify the pinned game-data input used only during release assembly."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from zipfile import BadZipFile, ZipFile, ZipInfo, ZIP_DEFLATED

from package_complete import GAME_FILES, PROFILE_PATH
from package_voices import VOICE_SHA256, VOICE_SIZE

CONFIG_PATH = Path(__file__).resolve().parents[1] / "packaging/release-data.json"


def specifications():
    profile = json.loads(PROFILE_PATH.read_text(encoding="utf-8"))["files"]
    files = {"data/" + name: profile[name] for name in GAME_FILES}
    files["media/FTAVOICE.HRS"] = {"size": VOICE_SIZE, "sha256": VOICE_SHA256}
    return files


def verify_payloads(payloads):
    expected = specifications()
    if payloads.keys() != expected.keys():
        raise ValueError("Release data must contain exactly eight game files and the voice archive")
    for name, payload in payloads.items():
        spec = expected[name]
        if len(payload) != spec["size"] or hashlib.sha256(payload).hexdigest() != spec["sha256"]:
            raise ValueError(f"Unsupported or damaged release data: {name}")


def create(data, voice, output):
    data, voice, output = (path.resolve() for path in (data, voice, output))
    if output == data or data in output.parents or output == voice:
        raise ValueError("Release data output must be outside the original inputs")
    if output.exists():
        raise ValueError(f"Output already exists: {output}")
    payloads = {"data/" + name: (data / name).read_bytes() for name in GAME_FILES}
    payloads["media/FTAVOICE.HRS"] = voice.read_bytes()
    verify_payloads(payloads)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, suffix=".zip", delete=False) as stream:
        temporary = Path(stream.name)
    try:
        with ZipFile(temporary, "w", compression=ZIP_DEFLATED) as archive:
            for name, payload in sorted(payloads.items()):
                info = ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                archive.writestr(info, payload, compresslevel=9)
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return {"asset": output.name, "size": output.stat().st_size,
            "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}


def unpack(archive_path, output):
    config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    if (archive_path.stat().st_size != config["size"] or
            hashlib.sha256(archive_path.read_bytes()).hexdigest() != config["sha256"]):
        raise ValueError("Release data archive does not match its pinned size and SHA-256")
    expected = specifications()
    with ZipFile(archive_path) as archive:
        members = archive.infolist()
        if len(members) != len(expected) or {info.filename for info in members} != expected.keys():
            raise ValueError("Unexpected or duplicate release data ZIP entries")
        for info in members:
            if (info.file_size != expected[info.filename]["size"] or
                    ((info.external_attr >> 16) & 0o170000) == 0o120000):
                raise ValueError(f"Invalid release data ZIP entry: {info.filename}")
        payloads = {info.filename: archive.read(info) for info in members}
    verify_payloads(payloads)
    if output.exists():
        raise ValueError(f"Output already exists: {output}")
    output.mkdir(parents=True)
    for name, payload in payloads.items():
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(payload)
    return output


def download(output):
    config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    repository = os.environ["GITHUB_REPOSITORY"]
    output.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run(["gh", "release", "download", config["release_tag"], "--repo",
                        repository, "--pattern", config["asset"],
                        "--dir", str(output)], check=True)
    except subprocess.CalledProcessError as error:
        raise ValueError(
            f"Cannot download {config['asset']} from {repository} release {config['release_tag']}. "
            "Complete the one-time maintainer draft setup in docs/releases.md and ensure "
            "GH_TOKEN has repository contents: write access to read drafts. "
            "Then rerun the failed release job; a new version is not required."
        ) from error
    return unpack(output / config["asset"], output / "verified")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prepare = commands.add_parser("create", help="Prepare the one-time verified release input")
    prepare.add_argument("--data-dir", required=True, type=Path)
    prepare.add_argument("--voice-file", required=True, type=Path)
    prepare.add_argument("--output", required=True, type=Path)
    fetch = commands.add_parser("download", help="Download and verify the repository release input")
    fetch.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = (create(args.data_dir, args.voice_file, args.output) if args.command == "create"
                  else download(args.output))
    except (OSError, ValueError, KeyError, BadZipFile, subprocess.CalledProcessError) as error:
        parser.exit(1, f"release_data: {error}\n")
    print(json.dumps(result) if isinstance(result, dict) else result)


if __name__ == "__main__":
    main()
