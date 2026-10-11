#!/usr/bin/env python3
"""Build and audit public release assets without proprietary game data."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tempfile
from zipfile import BadZipFile, ZipFile

from package_release import package_release, safe_members, source_additions, validate_runtime_layout

ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = ("Linux-x86_64", "Windows-x86_64", "Darwin-universal")


def cmake_version(content):
    match = re.search(r"^project\(fta2-ce VERSION ((?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)) LANGUAGES CXX\)",
                      content, re.MULTILINE)
    if not match:
        raise ValueError("Cannot read the Community Edition version from CMakeLists.txt")
    return match[1]


def verify_tag(version, tag):
    if tag != "v" + version:
        raise ValueError(f"Release tag must equal v{version}, got {tag!r}")


def runtime_names(version):
    return {f"fta2-ce-{version}-{platform}.zip" for platform in PLATFORMS}


def audit_bundle(path, version):
    if path.name not in runtime_names(version):
        raise ValueError(f"Unexpected release asset: {path.name}")
    expected_root = path.stem
    source_name = f"fta2-ce-{version}-source.zip"
    with ZipFile(path) as archive:
        root, members = safe_members(archive, allow_symlinks=True)
        if root != expected_root:
            raise ValueError(f"Runtime ZIP root does not match its filename: {root}")
        source_members = [(info, member) for info, member in members
                          if member.parts[1:2] == ("source",)]
        runtime_members = [(info, member) for info, member in members
                           if member.parts[1:2] != ("source",)]
        validate_runtime_layout(root, runtime_members)
        actual_source = {str(member.relative_to(root)) for info, member in source_members
                         if not info.is_dir()}
        if actual_source != {f"source/{source_name}", "source/manifest.json"}:
            raise ValueError("Runtime ZIP must embed exactly its corresponding source and manifest")
        payload = archive.read(f"{root}/source/{source_name}")
        manifest = json.loads(archive.read(f"{root}/source/manifest.json"))
        with tempfile.TemporaryDirectory() as temporary:
            source_path = Path(temporary) / source_name
            source_path.write_bytes(payload)
            _, expected_manifest = source_additions(source_path, root)
            with ZipFile(source_path) as source:
                content = source.read(f"{source_name.removesuffix('.zip')}/CMakeLists.txt")
                if cmake_version(content.decode("utf-8")) != version:
                    raise ValueError("Embedded source CMake version does not match release")
                if source.testzip() is not None:
                    raise ValueError("Embedded source ZIP failed its CRC check")
        if manifest != expected_manifest:
            raise ValueError("Embedded source checksum/size manifest does not match its contents")
        if archive.testzip() is not None:
            raise ValueError("Runtime ZIP failed its CRC check")


def bundle(packages, output, version):
    source = packages / f"fta2-ce-{version}-source.zip"
    runtime = sorted(path for path in packages.glob("*.zip") if path != source)
    if not source.is_file() or len(runtime) != 1 or runtime[0].name not in runtime_names(version):
        raise ValueError("Expected exactly one matching engine ZIP and one source ZIP")
    destination = output / runtime[0].name
    package_release(runtime[0], source, destination)
    audit_bundle(destination, version)
    return destination


def checksums(assets, version, all_platforms=False):
    paths = sorted(assets.iterdir())
    names = {path.name for path in paths}
    expected = runtime_names(version)
    if not paths or names - expected or any(not path.is_file() or path.is_symlink() for path in paths):
        raise ValueError("Release assets must contain only matching public runtime ZIPs")
    if all_platforms and names != expected:
        raise ValueError("Release requires Linux x64, Windows x64 and macOS universal ZIPs")
    records = []
    for path in paths:
        audit_bundle(path, version)
        with path.open("rb") as stream:
            hasher = hashlib.sha256()
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                hasher.update(block)
        records.append(f"{hasher.hexdigest()}  {path.name}\n")
    destination = assets / "SHA256SUMS"
    with destination.open("x", encoding="utf-8", newline="\n") as stream:
        stream.writelines(records)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    version_parser = commands.add_parser("version", help="Read CMake version and optionally verify its tag")
    version_parser.add_argument("--tag", default="")
    bundle_parser = commands.add_parser("bundle", help="Embed sources and audit the public player ZIP")
    bundle_parser.add_argument("--packages", type=Path, required=True)
    bundle_parser.add_argument("--output", type=Path, required=True)
    checksum_parser = commands.add_parser("checksums", help="Audit assets before writing SHA256SUMS")
    checksum_parser.add_argument("--assets", type=Path, required=True)
    checksum_parser.add_argument("--tag", required=True)
    checksum_parser.add_argument("--all-platforms", action="store_true")
    args = parser.parse_args()
    try:
        version = cmake_version((ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
        if args.command == "version":
            if args.tag:
                verify_tag(version, args.tag)
            result = version
        elif args.command == "bundle":
            result = bundle(args.packages, args.output, version)
        else:
            verify_tag(version, args.tag)
            result = checksums(args.assets, version, args.all_platforms)
    except (OSError, ValueError, KeyError, BadZipFile) as error:
        parser.exit(1, f"release: {error}\n")
    print(result)


if __name__ == "__main__":
    main()
