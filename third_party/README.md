# Provenance and licences

The product is GPL-3.0-or-later. Original game data is neither relicensed nor included in packages.

* `src/saga2/`: SAGA2 from ScummVM revision `1a3e816afd5ddc41cc2a5c51a5da292e6d61758c`, GPL-3.0-or-later. Copyright notices remain in every imported file; see `SCUMMVM-COPYRIGHT` and the root `LICENSE`. [Source](https://github.com/scummvm/scummvm/tree/1a3e816afd5ddc41cc2a5c51a5da292e6d61758c/engines/saga2).
* `src/audio/midiparser.{h,cpp}`, `midiparser_xmidi.cpp`, `miles_adlib.cpp`: selected portable MIDI algorithms from that same ScummVM revision, GPL-3.0-or-later. `midi.h` provides a new one-source native interface, with MIDI constants from upstream `audio/mididrv.h` and Miles constants from `audio/miles.h`.
* `src/audio/nuked.{h,cpp}`, `nuked_wf_rom.h`: Nuked-OPL3-fast 1.8-fast.2 as distributed in that revision, LGPL-2.1-or-later, Nuke.YKT and Tony Gies. See `NUKED-LGPL-2.1`; original notices are retained. Only the chip algorithms remain; the ScummVM audio-chip adapter was removed.
* SDL 3.4.18: zlib licence, `SDL-LICENSE.txt`. Fetched as a checksum-pinned release archive, or provided by the builder. Bundled as a shared runtime library.
* The Windows cross-build uses Zig 0.16.0 and its statically linked C++/compiler runtimes. Notices for libc++, libc++abi and libunwind (Apache-2.0 with LLVM exceptions), MinGW-w64 (its supplied COPYING notice), and Zig compiler runtime code (MIT) are preserved in `windows-runtime/`. These build-runtime components are separate from the imported SAGA2 code.

## Changes to imported code

Framework includes and `Common::` utility types were replaced by `platform/runtime.h` and `Native::` types backed by the C++ standard library. The native engine has no ScummVM `Engine`, `OSystem`, plugin registration, launcher, GUI debugger, configuration, stream, save manager or event infrastructure. Detection/metaengine/console implementation files are excluded from this product entirely. Gameplay, SAGA bytecode interpretation and software rendering remain in SAGA2.

DOS executable resources use the independently recovered DOS addresses and checked F2R1 pack. Unusable voice data is excluded, subtitles are mandatory, and missing movies finish immediately. Save files retain the original chunks, adding optional native RNG state. Reverse list traversal uses standard reverse iterators; closing the last window handles an empty list. The XMIDI/Miles algorithms use the native mixer and Nuked chip; MIDI timer callbacks are driven by generated audio samples. See `docs/architecture.md` and `docs/validation.md` for scope and verified limits.

`upstream-files.json` records SHA-256 hashes of the pristine imported source files, before these changes, to make the source revision and subsequent modifications auditable. The separate reference patches apply to the unmodified ScummVM revision; they are not linked into the product.

Native ownership fixes release retired motion tasks after the game step, detach cancelled path requests, and free player wrappers, association streams, effect prototype arrays, old band lists and the framebuffer. Motion tasks are value-initialized so save files do not contain uninitialized fields. These changes were checked against the reference world chunks and with AddressSanitizer/LeakSanitizer.
