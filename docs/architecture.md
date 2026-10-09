# Architecture and port boundaries

The product consists of `fta2-native`, its own platform layer and the imported SAGA2 gameplay algorithms. The ScummVM development reference is built separately and is not linked into the product.

| Component | Responsibility |
|---|---|
| `src/main.cpp` | CLI, user paths, data validation and direct game startup |
| `src/platform/startup.*` | Bundled-data discovery, remembered installations and the Windows folder picker |
| `src/platform/resources.*` | SHA-256, HRES bounds checks, the supported DOS profile and deterministic extraction |
| `src/platform/runtime.*` | Standard containers and streams, files, configuration, timers, SDL window/input/palette and PCM mixing |
| `src/platform/saves.*` | Original chunk-format validation and optional native metadata |
| `src/saga2/` | Simulation, SAGA bytecode, original UI, inventory, dialogue, magic, combat, maps and software rendering |
| `src/audio/` | Portable XMIDI/Miles algorithms, the Nuked OPL3 chip and a small native MIDI interface |
| `src/platform/probe.*` | Development scenario enabled explicitly by `--smoke-test` |

The platform provides an indexed framebuffer: 640 × 480 by default, or 854 × 480 in the optional widescreen mode. The original renderer writes 8-bit pixels; SDL receives an RGBA texture after palette and cursor application. Nearest-neighbour filtering and letterboxing preserve the active framebuffer aspect ratio. Mouse coordinates use the same SDL logical presentation, including after window resizing and changes in pixel density. Normal simulation uses the original 72 Hz timer. Test mode uses controlled virtual time and seed 42.

The mixer owns the audio callback and converts original PCM data to floating-point stereo at 48 kHz. The XMIDI parser and Miles driver write to Nuked OPL3; music timing advances with generated audio samples. The driver uses the original instrument banks. The callback and game thread synchronize mixer/music state through mutexes. Audio does not require a system MIDI synthesizer or DOS drivers.

Saves are written outside the installation, first to a temporary file and then replaced through SDL. The header and all 30 original chunks are preserved, followed by an optional `NRNG` chunk containing a 4-byte little-endian seed. Original saves without this chunk can be loaded. Before destructive restoration, the loader checks the header, chunk bounds, duplicate tags and required chunks. This validation does not make the format a general-purpose secure parser for arbitrary untrusted input.

The infrastructure port replaces `Common::` names with `Native::` and removes inheritance from `Engine`, `OSystem`, the detector, metaengine, plugins and GUI debugger. Native streams and utilities are new implementations based on the standard library. Only selected portable gameplay and audio algorithms remain from ScummVM. The preserved random sequence matches `common/random.cpp` from the pinned revision.

New `MotionTask` instances are value-initialized to make saves reproducible. Cancelled tasks are released after the game step finishes; cancelled pathfinding must not dereference a destroyed owner. The port also fixes resource ownership issues found by sanitizers and handles an empty window list.

The gameplay G shortcut searches only the selected actor's inventory through the original recursive container iterator. For the supported DOS profile, map prototype 320 uses ordinary Inventory class 13 and on-use script 1149; the shortcut schedules `MotionTask::useObject` just like inventory double-click. Original scripts continue to decide whether and where to show the map. Modal input, UI locks and held/dragged objects prevent shortcut activation.

## Display layout and live widescreen switching

`DisplayMode` and `DisplayLayout` define the classic and wide layouts independently of physical window dimensions. Widescreen adds 214 logical pixels to the map width, retains the 480-pixel screen height and moves the original sidebar right by 214 pixels. Frame artwork repeats a 32-pixel section over the new map space while retaining the original corners and sidebar ends. Compressed buttons share the inherited panel rectangle for hit testing, drawing, invalidation and cursor restoration; no button subclass retains a separate copy. Both sidebar control panels, indicators and hit regions move together; the status line expands with the map. The camera preserves its world-space center, and rendering, speech placement, cursor text and map input use the active dimensions. The original display-object limit is 100 in classic mode and 150 in wide mode; simulation activation distances remain unchanged.

The `widescreen` preference lives in the user configuration and defaults to false. Options changes the requested preference, then queues the transition after its deferred save/load and saved-background restoration. The game thread applies it after input dispatch has returned and no modal is open. Screen and map buffers and the SDL texture are allocated before committing; allocation or SDL failure keeps the working mode and restores the preference. The SDL window, fullscreen state, world and existing controls survive the transition. Floating windows retain their coordinates unless they must be clamped inside a smaller framebuffer. All window clips and cursor input are refreshed. Saved screen backgrounds carry dimensions and cannot be restored into a differently sized framebuffer.

Modal windows retain their artwork dimensions and vertical position. In wide mode they center horizontally on the whole screen when opened. Saves do not store the display preference; original saves and saves made in either mode load into the currently selected layout.

## Complete packages and startup

The SDL window starts in borderless desktop fullscreen, covering the monitor without changing its display resolution. The title bar and Windows taskbar are covered. Alt+Enter uses SDL fullscreen transitions to switch to a resizable window and back, preserving manually selected windowed sizes, maximization and position for the session. Automatic client dimensions follow the active framebuffer (640 × 480 or 854 × 480); changes apply immediately while windowed or after leaving fullscreen. Before a mode change or entering fullscreen, the platform compares the settled windowed size with the last accepted automatic size. Fullscreen monitor dimensions never count as a manual resize. SDL synchronization accounts for asynchronous window transitions, and the accepted automatic dimensions account for desktop constraints. Mouse conversion is refreshed after size and fullscreen changes. No additional configuration is stored. Both Enter keys and both Alt keys work. The platform consumes the shortcut before delivering keyboard events to the game UI, and key-repeat events do not toggle the window again. Rendering remains letterboxed at the selected aspect ratio with nearest-neighbour sampling; mouse conversion continues through the same SDL logical presentation after either transition.

Explicit `--data-dir` takes precedence. Otherwise the engine first checks `data/` beside the executable and then beside its containing directory, supporting both a flat layout and `bin/` plus sibling `data/`. An existing bundled path is authoritative even if incomplete or damaged: normal profile validation reports the failure rather than silently using a remembered installation. Only when no bundled path exists does discovery check the saved path and legacy locations, with the Windows folder picker as a final interactive fallback.

`tools/package_complete.py` combines a Windows engine ZIP with exactly eight allowlisted files from the supported DOS profile. It verifies their baseline SHA-256 hashes before writing, produces a deterministic ZIP for identical inputs and records data provenance in `share/fta2-native/complete-package.json`. It does not execute or modify the original files. Public CI and corresponding source packages remain independent of game data.

## Definitive Edition

The first version preserves the original mechanics and runs scripts from the original `SCRIPTS.HRS`. It does not recover the authors' story-script source files. A script disassembler, assembler and editor belong to a later stage.

Original resource loading, simulation and presentation have separate source areas and platform interfaces. SAGA2 still contains its original global state and coupling between gameplay UI and simulation. New UI, replacement graphics and further DE changes should follow completion of the regression scenarios and a full playthrough, with interfaces introduced gradually around these areas.

Exploration remains the original per-world `AMAP` visited-bit array, shared by all brothers. `updateMainDisplay` marks a radius of two metatiles around the tracked actor and skips the four corners of the clamped rectangle. Viewport dimensions do not enter this rule. The widescreen development probe checkpoints the world, tests separated brother positions and metatile-boundary crossings in both modes, checks a second world and restores all brother positions/worlds, visited bits and RNG. Ground-plane coverage samples normalize the camera to its settled actor-centered position; they do not claim coverage during camera panning, terrain occlusion or elevated viewpoints.
