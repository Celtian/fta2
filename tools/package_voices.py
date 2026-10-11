#!/usr/bin/env python3
"""Package a verified, user-supplied CD voice archive as a separate local add-on."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tempfile
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

VOICE_SHA256 = "34bd5c7e3b5982df3f84b274c05a0f056a2932dfd03f02d880be44e5a81353c3"
VOICE_SIZE = 268574344


def package_voices(engine, voice, output):
    engine, voice, output = (p.resolve() for p in (engine, voice, output))
    if output.exists() or output == voice or output == engine:
        raise ValueError("Output already exists or would replace an input")
    if voice.stat().st_size != VOICE_SIZE:
        raise ValueError("Unsupported voice archive size")
    payload = voice.read_bytes()
    if hashlib.sha256(payload).hexdigest() != VOICE_SHA256:
        raise ValueError("Unsupported or damaged voice archive")
    with ZipFile(engine) as archive:
        names = [PurePosixPath(n) for n in archive.namelist()]
        roots = {n.parts[0] for n in names if n.parts}
        if len(roots) != 1 or any(n.is_absolute() or ".." in n.parts for n in names):
            raise ValueError("Engine ZIP must have one safe root directory")
        root = roots.pop()
        if f"{root}/bin/fta2-ce.exe" not in archive.namelist():
            raise ValueError("Expected a Windows engine ZIP")
    manifest = {"schema_version": 1, "file": "media/FTAVOICE.HRS", "size": VOICE_SIZE,
                "sha256": VOICE_SHA256, "recordings": 4320,
                "source": "English full CD, FTAVOICE.HRS",
                "license": "Original game audio; not relicensed under the engine GPL"}
    files = {
        f"{root}/media/FTAVOICE.HRS": payload,
        f"{root}/media/voice-manifest.json": (json.dumps(manifest, indent=2) + "\n").encode(),
        f"{root}/VOICE-ADDON.txt": (
            "Extract this ZIP beside the matching complete Windows package.\n"
            "The same game directory will contain bin/, data/ and media/.\n"
            "Start bin/fta2-ce.exe normally; voices are detected automatically.\n"
            "Speech Volume controls the original voices. Speech Text controls subtitles.\n"
            "Missing or silent voices keep dialogue text readable.\n\n"
            "Original game audio retains its copyright and is not GPL-licensed.\n"
            "This is a local user-requested supplement, separate from public engine packages.\n"
            "Development alpha; a full voiced playthrough remains unverified.\n"
        ).encode(),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, suffix=".zip", delete=False) as stream:
            temporary = Path(stream.name)
        with ZipFile(temporary, "w", compression=ZIP_DEFLATED) as archive:
            for name, content in sorted(files.items()):
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
    parser.add_argument("--voice-file", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    manifest = package_voices(args.engine_package, args.voice_file, args.output)
    print(json.dumps({"output": str(args.output), **manifest}, indent=2))


if __name__ == "__main__":
    main()
