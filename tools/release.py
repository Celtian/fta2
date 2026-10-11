#!/usr/bin/env python3
"""Prepare versioned engine builds and complete GitHub release packages."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
from zipfile import BadZipFile, ZipFile

from package_release import package_release, safe_members, source_additions, validate_runtime_layout
from package_complete import package_complete
from release_data import verify_payloads

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


def version_changes(root, part):
    """Plan a version bump without rewriting historical validation records."""
    cmake = root / "CMakeLists.txt"
    readme = root / "README.md"
    originals = {path: path.read_bytes() for path in (cmake, readme)}
    version = cmake_version(originals[cmake].decode("utf-8"))
    numbers = [int(number) for number in version.split(".")]
    index = ("major", "minor", "patch").index(part)
    numbers[index] += 1
    numbers[index + 1:] = [0] * (2 - index)
    updated = ".".join(str(number) for number in numbers)
    marker = f"**🚧 Development alpha · version {version}.**".encode("utf-8")
    if originals[readme].count(marker) != 1:
        raise ValueError("README development-alpha version must match CMake exactly once")
    changes = {
        cmake: originals[cmake].replace(
            f"project(fta2-ce VERSION {version} LANGUAGES CXX)".encode(),
            f"project(fta2-ce VERSION {updated} LANGUAGES CXX)".encode(), 1),
        readme: originals[readme].replace(
            marker, f"**🚧 Development alpha · version {updated}.**".encode("utf-8"), 1),
    }
    return updated, originals, changes


def git(root, *args):
    result = subprocess.run(["git", *args], cwd=root, capture_output=True,
                            encoding="utf-8", errors="replace")
    if result.returncode:
        raise ValueError(f"git {args[0]} failed: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout.strip()


def prepare_version(root, part, dry_run=False):
    version, originals, changes = version_changes(root, part)
    tag = "v" + version
    if dry_run:
        return (f"Would update CMakeLists.txt and README.md, commit and tag {tag}, "
                "and enable repository-local push.followTags; no push")
    if Path(git(root, "rev-parse", "--show-toplevel")).resolve() != root.resolve():
        raise ValueError("Run version preparation from the project Git repository")
    if git(root, "status", "--porcelain", "--untracked-files=normal"):
        raise ValueError("Commit or stash working-tree changes before preparing a version")
    if git(root, "tag", "--list", tag):
        raise ValueError(f"Tag already exists: {tag}")
    paths = [str(path.relative_to(root)) for path in changes]
    try:
        for path, content in changes.items():
            path.write_bytes(content)
        git(root, "add", "--", *paths)
        git(root, "commit", "-m", f"chore(release): {tag}")
    except (OSError, ValueError):
        for path, content in originals.items():
            path.write_bytes(content)
        git(root, "reset", "--", *paths)
        raise
    try:
        git(root, "tag", "-a", tag, "-m", f"Community Edition {version} development alpha")
    except ValueError as error:
        raise ValueError(f"Version commit created, but {tag} was not tagged: {error}") from error
    try:
        git(root, "config", "--local", "push.followTags", "true")
    except ValueError as error:
        raise ValueError(f"Created commit and tag {tag}, but could not configure tag pushes: "
                         f"{error}. Publish with: git push origin HEAD {tag}") from error
    return (f"Created commit and tag {tag}. Enabled repository-local push.followTags.\n"
            "Publish with: git push")


def resolve_target(root, tag="", ref=""):
    """Resolve manual branch runs and tag pushes to one immutable source commit."""
    pushed_tag = ref.removeprefix("refs/tags/") if ref.startswith("refs/tags/") else ""
    if pushed_tag and tag and tag != pushed_tag:
        raise ValueError("Requested release tag differs from the workflow tag")
    version = cmake_version((root / "CMakeLists.txt").read_text(encoding="utf-8"))
    tag = pushed_tag or tag or "v" + version
    if not re.fullmatch(r"v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", tag):
        raise ValueError("Release tag must use vMAJOR.MINOR.PATCH")
    if git(root, "tag", "--list", tag):
        commit = git(root, "rev-parse", "--verify", f"refs/tags/{tag}^{{commit}}")
        version = cmake_version(git(root, "show", f"{commit}:CMakeLists.txt"))
    else:
        if pushed_tag:
            raise ValueError(f"Workflow tag is missing from the checkout: {tag}")
        commit = git(root, "rev-parse", "HEAD")
    verify_tag(version, tag)
    return {"tag": tag, "commit": commit}


def runtime_names(version):
    return {f"fta2-ce-{version}-{platform}.zip" for platform in PLATFORMS}


def audit_bundle(path, version, complete=False):
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
        package_manifest = None
        if complete:
            payloads = {str(member.relative_to(root)): archive.read(info)
                        for info, member in members if not info.is_dir() and
                        member.parts[1:2] in (("data",), ("media",))}
            verify_payloads(payloads)
            try:
                package_manifest = json.loads(archive.read(f"{root}/bin/package.json"))
                files = {name.removeprefix("data/"): {
                    "size": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}
                    for name, payload in payloads.items() if name.startswith("data/")}
                voice = payloads["media/FTAVOICE.HRS"]
                expected_voice = {"file": "media/FTAVOICE.HRS", "size": len(voice),
                                  "sha256": hashlib.sha256(voice).hexdigest(), "recordings": 4320,
                                  "license": "Original game audio; not relicensed under the engine GPL"}
                if (package_manifest["files"] != files or package_manifest["voice"] != expected_voice or
                        package_manifest["profile"] != "english-dos-a7413ddf" or
                        package_manifest["schema_version"] != 1):
                    raise ValueError("Complete package provenance does not match its contents")
            except (KeyError, TypeError) as error:
                raise ValueError("Complete package is missing its data/source provenance") from error
        runtime_members = [(info, member) for info, member in members
                           if member.parts[1:2] != ("source",) and not (complete and
                           (member.parts[1:2] in (("data",), ("media",)) or
                            str(member.relative_to(root)) == "bin/package.json"))]
        validate_runtime_layout(root, runtime_members)
        actual_source = {str(member.relative_to(root)) for info, member in source_members
                         if not info.is_dir()}
        expected_source = {f"source/{source_name}"}
        if not complete:
            expected_source.add("source/manifest.json")
        if actual_source != expected_source:
            raise ValueError("Runtime ZIP must embed exactly its corresponding source and manifest")
        payload = archive.read(f"{root}/source/{source_name}")
        manifest = (package_manifest.get("source") if complete else
                    json.loads(archive.read(f"{root}/source/manifest.json")))
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


def complete_packages(assets, data, voice, output, version):
    paths = sorted(assets.iterdir())
    if {path.name for path in paths} != runtime_names(version):
        raise ValueError("Complete release requires exactly the three engine player ZIPs")
    if output.exists():
        raise ValueError(f"Output already exists: {output}")
    for path in paths:
        audit_bundle(path, version)
    output.mkdir(parents=True)
    for path in paths:
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            engine = temporary / "engine.zip"
            source = temporary / f"fta2-ce-{version}-source.zip"
            with ZipFile(path) as original, ZipFile(engine, "w") as runtime:
                source.write_bytes(original.read(f"{path.stem}/source/{source.name}"))
                for info in original.infolist():
                    if not info.filename.startswith(f"{path.stem}/source/"):
                        runtime.writestr(info, original.read(info))
            destination = output / path.name
            package_complete(engine, data, destination, source, voice)
            audit_bundle(destination, version, complete=True)
    return output


def checksums(assets, version, all_platforms=False, complete=False):
    paths = sorted(assets.iterdir())
    names = {path.name for path in paths}
    expected = runtime_names(version)
    if not paths or names - expected or any(not path.is_file() or path.is_symlink() for path in paths):
        raise ValueError("Release assets must contain only matching public runtime ZIPs")
    if all_platforms and names != expected:
        raise ValueError("Release requires Linux x64, Windows x64 and macOS universal ZIPs")
    records = []
    for path in paths:
        audit_bundle(path, version, complete=complete)
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
    resolve_parser = commands.add_parser("resolve", help="Resolve a manual or tagged release to a commit")
    resolve_parser.add_argument("--tag", default="")
    resolve_parser.add_argument("--ref", default="")
    resolve_parser.add_argument("--github-output", type=Path, help="Append tag and commit job outputs")
    for part in ("patch", "minor", "major"):
        bump_parser = commands.add_parser(part, help=f"Bump {part}, commit and create an annotated tag")
        bump_parser.add_argument("--dry-run", action="store_true", help="Preview without changing files or Git")
    bundle_parser = commands.add_parser("bundle", help="Embed sources and audit the public player ZIP")
    bundle_parser.add_argument("--packages", type=Path, required=True)
    bundle_parser.add_argument("--output", type=Path, required=True)
    complete_parser = commands.add_parser("complete", help="Add verified game data and voices to all player ZIPs")
    complete_parser.add_argument("--assets", type=Path, required=True)
    complete_parser.add_argument("--data-dir", type=Path, required=True)
    complete_parser.add_argument("--voice-file", type=Path, required=True)
    complete_parser.add_argument("--output", type=Path, required=True)
    checksum_parser = commands.add_parser("checksums", help="Audit assets before writing SHA256SUMS")
    checksum_parser.add_argument("--assets", type=Path, required=True)
    checksum_parser.add_argument("--tag", required=True)
    checksum_parser.add_argument("--all-platforms", action="store_true")
    checksum_parser.add_argument("--complete", action="store_true", help="Require verified bundled data and voices")
    args = parser.parse_args()
    try:
        version = cmake_version((ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
        if args.command in ("patch", "minor", "major"):
            result = prepare_version(ROOT, args.command, args.dry_run)
        elif args.command == "resolve":
            target = resolve_target(ROOT, args.tag, args.ref)
            if args.github_output:
                with args.github_output.open("a", encoding="utf-8", newline="\n") as stream:
                    stream.writelines(f"{key}={value}\n" for key, value in target.items())
            result = json.dumps(target)
        elif args.command == "version":
            if args.tag:
                verify_tag(version, args.tag)
            result = version
        elif args.command == "bundle":
            result = bundle(args.packages, args.output, version)
        elif args.command == "complete":
            result = complete_packages(args.assets, args.data_dir, args.voice_file, args.output, version)
        else:
            verify_tag(version, args.tag)
            result = checksums(args.assets, version, args.all_platforms, args.complete)
    except (OSError, ValueError, KeyError, BadZipFile) as error:
        parser.exit(1, f"release: {error}\n")
    print(result)


if __name__ == "__main__":
    main()
