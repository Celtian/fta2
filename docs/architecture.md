# Architecture and port boundaries

The product consists of `fta2-native`, its own platform layer and the imported SAGA2 gameplay algorithms. The ScummVM development reference is built separately and is not linked into the product.

| Component | Responsibility |
|---|---|
| `src/main.cpp` | CLI, user paths, data validation and direct game startup |
| `src/platform/startup.*` | Game-directory discovery and the Windows folder picker |
| `src/platform/resources.*` | SHA-256, HRES bounds checks, the supported DOS profile and deterministic extraction |
| `src/platform/runtime.*` | Standard containers and streams, files, configuration, timers, SDL window/input/palette and PCM mixing |
| `src/platform/saves.*` | Original chunk-format validation and optional native metadata |
| `src/saga2/` | Simulation, SAGA bytecode, original UI, inventory, dialogue, magic, combat, maps and software rendering |
| `src/audio/` | Portable XMIDI/Miles algorithms, the Nuked OPL3 chip and a small native MIDI interface |
| `src/platform/probe.*` | Development scenario enabled explicitly by `--smoke-test` |

The platform provides a 640 × 480 indexed framebuffer. The original renderer writes 8-bit pixels; SDL receives an RGBA texture after palette and cursor application. Nearest-neighbour filtering and letterboxing preserve the 4:3 aspect ratio. Mouse coordinates use the same SDL logical presentation, including after window resizing and changes in pixel density. Normal simulation uses the original 72 Hz timer. Test mode uses controlled virtual time and seed 42.

The mixer owns the audio callback and converts original PCM data to floating-point stereo at 48 kHz. The XMIDI parser and Miles driver write to Nuked OPL3; music timing advances with generated audio samples. The driver uses the original instrument banks. The callback and game thread synchronize mixer/music state through mutexes. Audio does not require a system MIDI synthesizer or DOS drivers.

Saves are written outside the installation, first to a temporary file and then replaced through SDL. The header and all 30 original chunks are preserved, followed by an optional `NRNG` chunk containing a 4-byte little-endian seed. Original saves without this chunk can be loaded. Before destructive restoration, the loader checks the header, chunk bounds, duplicate tags and required chunks. This validation does not make the format a general-purpose secure parser for arbitrary untrusted input.

The infrastructure port replaces `Common::` names with `Native::` and removes inheritance from `Engine`, `OSystem`, the detector, metaengine, plugins and GUI debugger. Native streams and utilities are new implementations based on the standard library. Only selected portable gameplay and audio algorithms remain from ScummVM. The preserved random sequence matches `common/random.cpp` from the pinned revision.

New `MotionTask` instances are value-initialized to make saves reproducible. Cancelled tasks are released after the game step finishes; cancelled pathfinding must not dereference a destroyed owner. The port also fixes resource ownership issues found by sanitizers and handles an empty window list.

## Definitive Edition

The first version preserves the original mechanics and runs scripts from the original `SCRIPTS.HRS`. It does not recover the authors' story-script source files. A script disassembler, assembler and editor belong to a later stage.

Original resource loading, simulation and presentation have separate source areas and platform interfaces. SAGA2 still contains its original global state and coupling between gameplay UI and simulation. New UI, replacement graphics and further DE changes should follow completion of the regression scenarios and a full playthrough, with interfaces introduced gradually around these areas.
