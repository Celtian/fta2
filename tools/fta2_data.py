#!/usr/bin/env python3
"""Inspect original HRES archives and recover the supported DOS EXE's resources."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

EXE_SHA256 = "a7413ddf57df2ec649e3cb401cfc6b3d0b0141cc840161dbd6b3029e381837c2"
DOS_DATA_BASE = 0x1A90A4
FONTS = {
    "Onyx10Font": 0x213C98,
    "Plate18Font": 0x211C48,
    "Helv11Font": 0x214920,
    "Amber13Font": 0x215654,
    "ThinFix8Font": 0x217AEC,
    "Script10Font": 0x216C78,
}
# Object-relative addresses recovered from the initializer at EXE file offset
# 0x13CD44. Dimensions are independently documented in SAGA2 mouseimg.cpp.
CURSORS = {
    "closeBx1ImageData": (0x1B428, 12, 12),
    "closeBx2ImageData": (0x1B4B8, 12, 12),
    "usePtrImageData": (0x1B548, 15, 15),
    "xPointerImageData": (0x1B62C, 15, 15),
    "arrowImageData": (0x1B710, 11, 17),
    "grabPtrImageData": (0x1B7CC, 16, 13),
    "attakPtrImageData": (0x1B89C, 23, 23),
    "centerActorIndicatorImageData": (0x1BAB0, 9, 9),
    "pgUpImageData": (0x1BB0C, 16, 16),
    "pgDownImageData": (0x1BC0C, 16, 16),
    "pgLeftImageData": (0x1BD0C, 16, 16),
    "pgRightImageData": (0x1BE0C, 16, 16),
    "autoWalkImageData": (0x1BF0C, 15, 15),
    "gaugeImageData": (0x1A944, 48, 5),
}


class DataError(ValueError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def bounded(data: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise DataError(f"Resource outside file: offset={offset:#x}, size={size}")
    return data[offset : offset + size]


def tag_name(tag: bytes) -> str:
    return "".join(chr(c) if 32 <= c < 127 else f"\\x{c:02x}" for c in tag)


def hrs_inventory(data: bytes) -> dict:
    if len(data) < 12 or data[:4] != b"HRES":
        raise DataError("Not an HRES archive")
    root_offset, root_size = struct.unpack_from("<II", data, 4)
    if root_size % 12:
        raise DataError("HRES root table size is not a multiple of 12")
    root = bounded(data, root_offset, root_size)
    groups = []
    for pos in range(0, len(root), 12):
        tag, offset, size = struct.unpack_from("<4sII", root, pos)
        if size % 12:
            raise DataError(f"Invalid resource table size for {tag_name(tag)}")
        table = bounded(data, offset, size)
        entries = []
        for i in range(0, len(table), 12):
            ident, start, length = struct.unpack_from("<4sII", table, i)
            # HRES marks external resources with bit 31 of the offset.
            external = bool(start & 0x80000000)
            if not external:
                bounded(data, start, length)
            entries.append({"id": ident.hex(), "tag": tag_name(ident),
                            "offset": start, "size": length, "external": external})
        groups.append({"tag": tag_name(tag), "offset": offset, "entries": entries})
    return {"size": len(data), "sha256": sha256(data), "groups": groups}


def decode_font(data: bytes) -> tuple[int, int, int]:
    if len(data) < 1286:
        raise DataError("Truncated font header")
    height, baseline, row_mod = struct.unpack_from("<HHH", data)
    if not 0 < baseline <= height <= 64 or not 0 < row_mod <= 4096:
        raise DataError("Invalid font dimensions")
    if len(data) != 1286 + height * row_mod:
        raise DataError("Font bitmap size does not match dimensions")
    offsets = struct.unpack_from("<256H", data, 6)
    widths = struct.unpack_from("<256b", data, 518)
    for x, width in zip(offsets, widths):
        if width < 0 or x * 8 + width > row_mod * 8:
            raise DataError("Glyph extends outside font row")
    return height, baseline, row_mod


def png(path: Path, width: int, height: int, rgb: bytes) -> None:
    if len(rgb) != width * height * 3:
        raise DataError("Invalid PNG pixel count")
    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload)))
    scanlines = b"".join(b"\0" + rgb[y * width * 3 : (y + 1) * width * 3]
                         for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))


def font_preview(path: Path, data: bytes) -> None:
    height, _, stride = decode_font(data)
    offsets = struct.unpack_from("<256H", data, 6)
    widths = data[518:774]
    cell_w, cell_h = 32, height + 4
    w, h = 16 * cell_w, 16 * cell_h
    pixels = bytearray([24] * (w * h * 3))
    for ch in range(256):
        for y in range(height):
            for x in range(widths[ch]):
                # charXOffset is a byte offset, with glyph bits read MSB first.
                if data[1286 + y * stride + offsets[ch] + x // 8] & (0x80 >> (x % 8)):
                    dest = (((ch // 16) * cell_h + y + 2) * w
                            + (ch % 16) * cell_w + x + 2) * 3
                    pixels[dest : dest + 3] = b"\xff\xff\xff"
    png(path, w, h, pixels)


def recover(exe: bytes) -> list[tuple[str, int, bytes]]:
    if sha256(exe) != EXE_SHA256:
        raise DataError("Unsupported FTA2.EXE: expected SHA-256 " + EXE_SHA256
                        + "; original file was not modified")
    assets = []
    for name, offset in FONTS.items():
        height, _, stride = struct.unpack("<HHH", bounded(exe, offset, 6))
        data = bounded(exe, offset, 1286 + height * stride)
        decode_font(data)
        assets.append((name, offset, data))
    for name, offset, size in [("loadingWindowPalette", 0x1C66D0, 1024),
                               ("loadingWindowData", 0x1C6AD0, 640 * 480),
                               ("ColorMapRanges", 0x1C3B24, 1584)]:
        assets.append((name, offset, bounded(exe, offset, size)))
    for name, (address, w, h) in CURSORS.items():
        offset = DOS_DATA_BASE + address
        assets.append((name, offset, bounded(exe, offset, w * h)))
    return assets


def extract(game_dir: Path, output: Path) -> dict:
    game_dir, output = game_dir.resolve(), output.resolve()
    if output == game_dir or game_dir in output.parents:
        raise DataError("Extraction output must be outside the original game directory")
    assets = recover((game_dir / "FTA2.EXE").read_bytes())
    output.mkdir(parents=True, exist_ok=True)
    manifest = {"schema_version": 1, "profile": "english-dos-a7413ddf",
                "source": {"file": "FTA2.EXE", "sha256": EXE_SHA256}, "assets": {}}
    # F2R1: u32 entry count; records: name[32], u32 offset, u32 size; payloads.
    table, payload = bytearray(), bytearray()
    data_start = 8 + len(assets) * 40
    for name, source_offset, data in assets:
        table += struct.pack("<32sII", name.encode(), data_start + len(payload), len(data))
        payload += data
        manifest["assets"][name] = {"source_offset": source_offset, "size": len(data),
                                    "sha256": sha256(data)}
        if name in FONTS:
            font_preview(output / f"{name}.png", data)
    pack = b"F2R1" + struct.pack("<I", len(assets)) + table + payload
    (output / "fta2-dos-resources.bin").write_bytes(pack)
    manifest["pack_sha256"] = sha256(pack)
    palette = dict((name, data) for name, _, data in assets)["loadingWindowPalette"]
    image = dict((name, data) for name, _, data in assets)["loadingWindowData"]
    png(output / "loading.png", 640, 480,
        bytes(c for index in image for c in palette[index * 4 : index * 4 + 3]))
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    inventory = sub.add_parser("inventory", help="Validate and list HRES archives")
    inventory.add_argument("--data-dir", type=Path, default=Path("original-game"))
    extraction = sub.add_parser("extract", help="Recover DOS executable resources")
    extraction.add_argument("--data-dir", type=Path, default=Path("original-game"))
    extraction.add_argument("--output", type=Path, required=True)
    verify = sub.add_parser("verify", help="Verify every original file against the baseline")
    verify.add_argument("--data-dir", type=Path, default=Path("original-game"))
    verify.add_argument("--manifest", type=Path,
                        default=Path(__file__).resolve().parents[1] / "research/original-files.json")
    args = parser.parse_args()
    try:
        if args.command == "inventory":
            result = {p.name: hrs_inventory(p.read_bytes())
                      for p in sorted(args.data_dir.iterdir()) if p.suffix.lower() == ".hrs"}
            if not result:
                raise DataError("No HRES archives found")
        elif args.command == "extract":
            result = extract(args.data_dir, args.output)
        else:
            baseline = json.loads(args.manifest.read_text())["files"]
            for name, expected in baseline.items():
                p = args.data_dir / name
                if p.stat().st_size != expected["size"] or sha256(p.read_bytes()) != expected["sha256"]:
                    raise DataError(f"Original file changed: {name}")
            result = {"verified_files": len(baseline)}
        print(json.dumps(result, indent=2))
        return 0
    except (DataError, OSError, struct.error, ValueError) as exc:
        print(f"fta2-data: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
