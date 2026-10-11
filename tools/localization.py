#!/usr/bin/env python3
"""Export local translation evidence and compile data-free UTF-8 catalogs."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

from fta2_data import DataError, EXE_SHA256, hrs_inventory

ROOT = Path(__file__).resolve().parents[1]
TOKENS = re.compile(r"@im.{7}(?::.|.)|@pb|@|%(?:[-+0 #]*\d*(?:\.\d+)?[sdxnu%])")
INPUT_HASHES = {
    "FTA2.EXE": EXE_SHA256,
    "FTA.HRS": "1d4fc56861bdc3f0e1dda1a21f48991e350dd8213fb694484f0f7fa731c7055d",
    "SCRIPTS.HRS": "ddb3747ed47e1d6a420165d28e1d7f3e66abd64e1323690c11620b910dd05317",
}


def fingerprint(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def strings_at(data: bytes, start: int = 0, allow_final_sentinel: bool = False) -> list[bytes]:
    """Decode an offset table, rejecting guesses that point outside its resource."""
    if start < 0 or start + 2 > len(data):
        raise DataError("String table outside resource")
    data = data[start:]
    first = struct.unpack_from("<H", data)[0]
    if first < 2 or first % 2 or first > len(data):
        raise DataError("Invalid string table")
    offsets = struct.unpack_from(f"<{first // 2}H", data)
    result = []
    for index, offset in enumerate(offsets):
        if allow_final_sentinel and index == len(offsets) - 1 and offset == len(data):
            result.append(b"")
            continue
        end = data.find(b"\0", offset)
        if offset < first or offset >= len(data) or end < 0:
            raise DataError("String outside resource")
        result.append(data[offset:end])
    return result


def read_resources(path: Path) -> list[tuple[str, bytes]]:
    data = path.read_bytes()
    return [(entry["tag"], data[entry["offset"]:entry["offset"] + entry["size"]])
            for group in hrs_inventory(data)["groups"] for entry in group["entries"]
            if not entry["external"]]


def export(data_dir: Path) -> dict:
    for name, expected in INPUT_HASHES.items():
        if fingerprint((data_dir / name).read_bytes()) != expected:
            raise DataError(f"Unsupported DOS input profile: {name}")
    result = {}

    def add(key: str, raw: bytes, context: dict) -> None:
        if any(c < 32 and c not in (9, 10, 13) for c in raw):
            return
        result[key] = {"source_sha256": fingerprint(raw), "context": context,
                       "source": raw.decode("cp1252"), "tokens": TOKENS.findall(raw.decode("cp1252"))}

    resources = read_resources(data_dir / "FTA.HRS")
    names = next(data for tag, data in resources if tag == "NAME")
    for index, raw in enumerate(strings_at(names, allow_final_sentinel=True)):
        add(f"name:{index}", raw, {"archive": "FTA.HRS", "resource": "NAME", "index": index})
    segments = read_resources(data_dir / "SCRIPTS.HRS")
    visited = set()
    for tag, code in segments:
        if not tag.startswith(("SEG", "SEH")) or len(code) < 6:
            continue
        version, segment, offset = struct.unpack_from("<3H", code)
        if version != 1 or segment >= len(segments) or (segment, offset) in visited:
            continue
        try:
            strings = strings_at(segments[segment][1], offset)
        except DataError:
            continue
        visited.add((segment, offset))
        for index, raw in enumerate(strings):
            add(f"script:{segment}:{offset}:{index}", raw,
                {"archive": "SCRIPTS.HRS", "resource": segments[segment][0],
                 "segment": segment, "table_offset": offset, "index": index})
    return {"schema_version": 1, "profile": "english-dos-a7413ddf",
            "input_sha256": {name: fingerprint((data_dir / name).read_bytes())
                             for name in ("FTA2.EXE", "FTA.HRS", "SCRIPTS.HRS")},
            "entries": result}


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise DataError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def read_catalog(path: Path) -> dict:
    obj = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    if obj.get("schema_version") != 1 or obj.get("language") != "cs":
        raise DataError("Unsupported localization catalog")
    entries = obj["entries"]
    if not isinstance(entries, dict):
        raise DataError("Catalog entries must be an object")
    for key, value in entries.items():
        if not re.fullmatch(r"(?:ui:[A-Za-z0-9_.]+|name:\d+|script:\d+:\d+:\d+)", key):
            raise DataError(f"Invalid catalog key: {key}")
        if not re.fullmatch(r"[0-9a-f]{64}", value["source_sha256"]):
            raise DataError(f"Invalid source fingerprint: {key}")
        if value.get("status") not in ("reviewed", "draft", "preserve"):
            raise DataError(f"Invalid translation status: {key}")
        if value["status"] == "preserve" and value["text"]:
            raise DataError(f"Preserved originals must not be embedded in the catalog: {key}")
        if "\0" in value["text"]:
            raise DataError(f"Embedded NUL: {key}")
        value["text"].encode("cp1250")
        if "source" in value and TOKENS.findall(value["source"]) != TOKENS.findall(value["text"]):
            raise DataError(f"Changed formatting markers: {key}")
        if not key.startswith("ui:") and "source" in value:
            raise DataError(f"Original game text must stay outside source catalogs: {key}")
    return entries


def cpp_bytes(text: str) -> str:
    return '"' + "".join(f"\\{c:03o}" for c in text.encode("cp1250")) + '"'


def compile_catalog(paths: list[Path], output: Path) -> None:
    entries = {}
    for path in paths:
        for key, value in read_catalog(path).items():
            if key in entries:
                raise DataError(f"Duplicate localization key: {key}")
            entries[key] = value
    lines = ["// Generated from UTF-8 translation catalogs; do not edit.",
             "static const CatalogEntry catalog[] = {"]
    for key, value in sorted(entries.items()):
        text = "nullptr" if value["status"] == "preserve" else cpp_bytes(value["text"])
        lines.append(f'    {{"{key}", "{value["source_sha256"]}", {text}}},')
    lines.append("};\n")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="ascii")


def audit_review(translations: dict, journal: dict) -> dict:
    """Bind review decisions to the precise source and translation that were read."""
    if journal.get("schema_version") != 1 or not journal.get("reviewer"):
        raise DataError("Unsupported localization review journal")
    decisions = journal["entries"]
    missing, invalid = [], []
    for key, value in translations.items():
        if key.startswith("ui:"):
            continue
        decision = decisions.get(key)
        if decision is None:
            missing.append(key)
        elif (decision.get("source_sha256") != value["source_sha256"] or
              decision.get("translation_sha256") != fingerprint(value["text"].encode("utf-8")) or
              decision.get("status") != value["status"] or
              decision.get("status") not in ("reviewed", "preserve") or
              set(decision) != {"source_sha256", "translation_sha256", "status", "reason"} or
              not isinstance(decision.get("reason"), str) or not decision["reason"].strip()):
            invalid.append(key)
    invalid.extend(key for key in decisions if key not in translations)
    return {"review_missing": missing, "review_invalid": invalid}


def audit(paths: list[Path], evidence: dict, journal: dict | None = None) -> dict:
    translations = unique_object([(key, value) for path in paths
                                  for key, value in read_catalog(path).items()])
    missing, invalid, drafts = [], [], []
    for key, original in evidence["entries"].items():
        value = translations.get(key)
        if value is None:
            missing.append(key)
        elif (value["source_sha256"] != original["source_sha256"] or
              (value["status"] != "preserve" and TOKENS.findall(value["text"]) != original["tokens"])):
            invalid.append(key)
        elif value["status"] == "draft":
            drafts.append(key)
    result = {"entries": len(evidence["entries"]), "missing": missing,
            "invalid": invalid, "drafts": drafts,
            "complete": not (missing or invalid or drafts)}
    if journal is not None:
        result.update(audit_review(translations, journal))
        result["complete"] &= not (result["review_missing"] or result["review_invalid"])
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("export")
    p.add_argument("--data-dir", type=Path, default=ROOT / "original-game")
    p.add_argument("--output", type=Path, required=True)
    p = sub.add_parser("compile")
    p.add_argument("--catalog", type=Path, action="append", required=True)
    p.add_argument("--output", type=Path, required=True)
    p = sub.add_parser("audit")
    p.add_argument("--catalog", type=Path, action="append", required=True)
    p.add_argument("--evidence", type=Path, required=True)
    p.add_argument("--review", type=Path, default=ROOT / "localization/review.json",
                   help="Per-key linguistic review decisions (required by the completeness gate)")
    args = parser.parse_args()
    try:
        if args.command == "export":
            data_dir, output = args.data_dir.resolve(), args.output.resolve()
            if output.is_relative_to(data_dir) or not output.is_relative_to((ROOT / ".cache").resolve()):
                raise DataError("Original text exports must be written under .cache, outside game data")
            obj = export(data_dir)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            print(f"Exported {len(obj['entries'])} translation candidates")
        elif args.command == "compile":
            compile_catalog(args.catalog, args.output)
        else:
            journal = json.loads(args.review.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
            result = audit(args.catalog, json.loads(args.evidence.read_text(encoding="utf-8")), journal)
            print(json.dumps(result, indent=2))
            return 0 if result["complete"] else 1
    except (DataError, ValueError, KeyError, UnicodeError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
