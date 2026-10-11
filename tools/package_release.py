#!/usr/bin/env python3
"""Create one distributable runtime ZIP with its corresponding source inside."""
import argparse
import hashlib
import json
import posixpath
from pathlib import Path, PurePosixPath
import tempfile
from zipfile import BadZipFile, ZipFile, ZipInfo, ZIP_DEFLATED


def safe_members(archive, allow_symlinks=False):
    members, seen = [], set()
    for info in archive.infolist():
        path = PurePosixPath(info.filename)
        if (path.is_absolute() or ".." in path.parts or not path.parts
                or "\\" in info.filename or ":" in info.filename):
            raise ValueError(f"Unsafe ZIP entry: {info.filename}")
        key = str(path).casefold()
        if key in seen:
            raise ValueError(f"Duplicate ZIP entry: {info.filename}")
        seen.add(key)
        members.append((info, path))
    roots = {path.parts[0] for _, path in members}
    if len(roots) != 1:
        raise ValueError("ZIP must contain a single package directory")
    root = next(iter(roots))
    for info, path in members:
        if ((info.external_attr >> 16) & 0o170000) == 0o120000:
            if not allow_symlinks:
                raise ValueError(f"Source ZIP contains a symbolic link: {path}")
            target = archive.read(info).decode("utf-8")
            resolved = PurePosixPath(posixpath.normpath(str(path.parent / target)))
            if (PurePosixPath(target).is_absolute() or "\\" in target or ":" in target
                    or not resolved.parts or resolved.parts[0] != root):
                raise ValueError(f"Runtime ZIP contains an unsafe symbolic link: {path}")
    return root, members


def source_additions(source_package, root):
    source_package = source_package.resolve()
    with ZipFile(source_package) as source:
        source_root, members = safe_members(source)
        names = {str(path) for _, path in members}
        for required in ("CMakeLists.txt", "LICENSE"):
            if f"{source_root}/{required}" not in names:
                raise ValueError(f"Source ZIP is missing {required}")
        expected_root = source_root.removesuffix("-source") + "-"
        if not source_root.endswith("-source") or not root.startswith(expected_root):
            raise ValueError("Source ZIP version does not match the runtime package")
        for _, path in members:
            if (any(part.casefold() in ("original-game", "data", "media", ".cache", "dist", "screenshots")
                    for part in path.parts[1:])
                    or path.suffix.casefold() in (".hrs", ".sav", ".ppm", ".exe", ".dll")):
                raise ValueError(f"Source ZIP contains game/development data: {path}")
    payload = source_package.read_bytes()
    manifest = {"file": "source/" + source_package.name, "size": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest()}
    return {
        f"{root}/{manifest['file']}": payload,
    }, manifest


def write_bundle(engine, members, additions, output):
    if output.exists():
        raise ValueError(f"Output already exists: {output}")
    seen = {str(path).casefold() for _, path in members}
    if any(name.casefold() in seen for name in additions):
        raise ValueError("Runtime ZIP conflicts with release metadata")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, suffix=".zip", delete=False) as stream:
            temporary = Path(stream.name)
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


def package_release(engine_package, source_package, output):
    with ZipFile(engine_package) as engine:
        root, members = safe_members(engine, allow_symlinks=True)
        additions, manifest = source_additions(source_package, root)
        additions[f"{root}/source/manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
        write_bundle(engine, members, additions, output.resolve())
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-package", required=True, type=Path)
    parser.add_argument("--source-package", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        manifest = package_release(args.engine_package, args.source_package, args.output)
    except (OSError, ValueError, BadZipFile) as error:
        parser.exit(1, f"package_release: {error}\n")
    print(json.dumps({"output": str(args.output), "source": manifest}, indent=2))


if __name__ == "__main__":
    main()
