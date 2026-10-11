#!/usr/bin/env python3
"""Exercise live language switching and localized speech saves with local game data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def chunks(path: Path) -> dict[bytes, bytes]:
    raw = path.read_bytes()
    result, offset = {}, 128
    while offset < len(raw):
        tag, size = struct.unpack_from("<4sI", raw, offset)
        result[tag] = raw[offset + 8:offset + 8 + size]
        offset += 8 + size
    return result


def check_speech(path: Path) -> None:
    data = chunks(path)
    canonical, localized = data[b"SPCH"], data[b"NLOC"]
    assert struct.unpack_from("<H", canonical)[0] == 2
    assert b"Synthetic choice: @Yes@No" in canonical
    assert b"Synthetic continued speech" in canonical
    assert "Zkouška: @Ano@Ne".encode("cp1250") not in canonical
    assert struct.unpack_from("<HH", localized) == (1, 2)
    assert "Zkouška: @Ano@Ne".encode("cp1250") in localized


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--data-dir", type=Path, default=ROOT / "original-game")
    parser.add_argument("--voice-file", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    game, work, binary = args.data_dir.resolve(), args.output.resolve(), args.binary.resolve()
    if work.exists() or work.is_relative_to(game):
        raise RuntimeError("Use a fresh output directory outside the original installation")
    before = {str(p.relative_to(game)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in game.rglob("*") if p.is_file()}
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    results = []
    for initial_language in ("en", "cs"):
        run = work / initial_language
        saves, output = run / "saves", run / "output"
        saves.mkdir(parents=True)
        output.mkdir()
        if initial_language == "en":
            (saves / "config.ini").write_text("language=en\nsubtitles=false\n")
        else:
            # Restart with the previous process's saved language and a later user choice to hide subtitles.
            previous = (work / "en/saves/config.ini").read_text()
            (saves / "config.ini").write_text(previous.replace("subtitles=true", "subtitles=false"))
        command = [str(binary), "--data-dir", str(game), "--save-dir", str(saves),
                   "--smoke-test", str(output), "--smoke-localization"]
        if args.voice_file:
            command += ["--voice-file", str(args.voice_file.resolve())]
        with (output / "native.log").open("w") as log:
            subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=300)
        result = json.loads((output / "localization.json").read_text())
        assert result["status"] == "passed" and result["menu_cases"] == 32
        assert result["startup_language"] == initial_language and not result["startup_subtitles"]
        assert result["text_overflows"] == 0
        assert result["radio_clicks"] == 96 and result["status_queue_refresh"]
        assert result["immediate_language_apply"] and result["same_language_preserves_subtitles"]
        assert result["live_options_cases"] == 32
        screenshots = list((output / "application/screenshots").glob("*.png"))
        assert result["modal_screenshots"] == 96 and result["document_screenshots"] == 12
        assert result["gameplay_screenshots"] == 1 and len(screenshots) == 109
        sizes = set()
        for screenshot in screenshots:
            raw = screenshot.read_bytes()
            assert raw[:8] == b"\x89PNG\r\n\x1a\n" and raw[12:16] == b"IHDR"
            sizes.add(struct.unpack_from(">II", raw, 16))
        assert sizes == {(640, 480), (854, 480)}
        assert result["mana_tooltip_cases"] == 144
        assert result["file_dialog_cases"] == 16 and result["placard_cases"] == 664
        documents = json.loads((output / "documents.json").read_text())
        assert documents["page_spreads"] == result["document_spreads"]
        assert documents["redraw_comparisons"] > 2 * documents["page_spreads"]
        assert documents["wheel_checks"] > 0 and documents["multipage_documents"] > 0
        result["document_rendering"] = documents
        assert "language=cs" in (saves / "config.ini").read_text()
        for slot in (9, 10): check_speech(saves / f"fta2.{slot:03}")
        results.append(result)
    after = {str(p.relative_to(game)): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in game.rglob("*") if p.is_file()}
    assert before == after, "Original installation changed"
    print(json.dumps({"status": "passed", "processes": results,
                      "verified_original_files": len(before)}, indent=2))


if __name__ == "__main__": main()
