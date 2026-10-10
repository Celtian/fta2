# Verified DOS data profile

`original-game/` is immutable input. `research/original-files.json` records the size and SHA-256 of every original file. `tools/fta2_data.py` provides HRES inventory, installation verification and independent extraction. The native C++ application also validates the supported profile and extracts its resources at startup, without requiring Python.

The inventory summary is in [`research/archives.json`](../research/archives.json): the five active archives contain 1,550 resource records in 25 groups. All internal resources are within file bounds; no record uses the external-resource flag. The sixth archive, `FTAVOICE.HRS`, repeats the 293 records from `FTADATA.HRS`.

The supported `FTA2.EXE` is 2,223,710 bytes long and has this SHA-256:

```text
a7413ddf57df2ec649e3cb401cfc6b3d0b0141cc840161dbd6b3029e381837c2
```

The five main HRS archives match the English profile in the [SAGA2 detector](https://github.com/scummvm/scummvm/blob/1a3e816afd5ddc41cc2a5c51a5da292e6d61758c/engines/saga2/detection.cpp). This installation's `FTAVOICE.HRS` is byte-for-byte identical to `FTADATA.HRS`, and videos are absent. The native profile therefore has no voice archive or video decoder.

## Resource recovery

The addresses below are **file offsets in this DOS executable**, not Windows executable offsets. Their assignments are checked through rendering and comparison of the same scene against the reference.

| Font | Offset | Height | Baseline | Bytes per row |
|---|---:|---:|---:|---:|
| Onyx10Font | `0x213C98` | 10 | 7 | 192 |
| Plate18Font | `0x211C48` | 18 | 13 | 388 |
| Helv11Font | `0x214920` | 11 | 8 | 190 |
| Amber13Font | `0x215654` | 13 | 10 | 328 |
| ThinFix8Font | `0x217AEC` | 8 | 6 | 192 |
| Script10Font | `0x216C78` | 10 | 8 | 188 |

A font has a 1,286-byte header: three little-endian uint16 values, 256 little-endian uint16 offsets, and three 256-byte tables for width, kerning and spacing. Its bitmap contains `height × stride` bytes. A character offset is a byte offset within a row; bits are read most significant first. The Python extractor generates a preview of all 256 characters in each font.

The loading-screen palette is at `0x1C66D0` (1,024 RGBX bytes), its image at `0x1C6AD0` (307,200 indices), and `ColorMapRanges` at `0x1C3B1C` (1,584 bytes, 198 banks of eight colors). Cursors and the gauge were identified from the initializer at `0x13CD44`; the DOS data-object base is `0x1A90A4`. Exact addresses and dimensions are recorded in `tools/fta2_data.py` and the compiled `src/platform/dos-profile.inc`.

The DOS color-table builder starts at file offset `0xB89A4`. At `0xB89DB` it multiplies the selected bank by eight, then at `0xB89E2` adds the data-object address `0x1AA78`, giving file offset `0x1C3B1C`. Bank zero contains eight zeros and must be retained. Bank 197 is the last valid bank; the following bytes belong to another structure. Releases through 0.1.38 incorrectly started eight bytes later at `0x1C3B24`, shifting every selected bank and including eight unrelated trailing bytes. This made Bellows and the brown Book variant cream-colored. Comparing native rendering with an extractor or reference using that same offset could not detect the mistake. Version 0.1.39 corrects the extraction, with a synthetic regression retaining both boundary banks.

The F2R1 pack contains the `F2R1` signature, a little-endian uint32 count of 23, 40-byte records (`name[32]`, little-endian offset, little-endian length), and contiguous resource data. It is 340,358 bytes long and has this SHA-256:

```text
c54493e0188039e696570b0b2b4f5b2f4f9c586c7710938a3b1271ea6c315c1e
```

The manifest records the source executable, each resource's offset, length and SHA-256, and the pack checksum. The C++ output and independent Python extractor output are compared byte-for-byte, including their manifests. An unknown executable, incomplete HRES record or out-of-bounds resource is rejected with an explanation.

Technical sources: [SAGA2](https://github.com/scummvm/scummvm/tree/1a3e816afd5ddc41cc2a5c51a5da292e6d61758c/engines/saga2), the [2021 development report](https://blogs.scummvm.org/ayyg/2021/08/18/saga2-gsoc-summary/) and [SDL3 documentation](https://wiki.libsdl.org/SDL3/FrontPage). SAGA2 made complete decompilation of the DOS executable unnecessary; targeted analysis was still needed to recover resources otherwise loaded from the missing Windows executable.
