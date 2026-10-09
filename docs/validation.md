# Native alpha 0.1.12 validation status

The result is a standalone C++20 engine using SDL 3.4.18. It was built and run on Linux and the actual Windows host with the original DOS data, without DOSBox or the ScummVM framework. **This is a development alpha; a complete playthrough of the port has not yet been demonstrated.** Version 0.1.12 adds G/Shift+G to use the selected brother's carried map through its original script, including maps in closed or nested bags. Version 0.1.11 makes Escape close the frontmost container before opening Options. Version 0.1.10 adds mouse-wheel page navigation to vertical scrolls and parchment. Version 0.1.9 fixes Credits page-arrow updates and cursor restoration on close. Version 0.1.8 restores the Speech Text toggle and remembered preference while retaining readable unvoiced dialogue. Version 0.1.7 fixes the Options load empty state, restores the visible mandatory Speech Text checkbox and recovers the original Credits script. Version 0.1.6 adds a Hot Keys overview to Options and implements the M music toggle. It retains the numeric keypad support introduced in 0.1.5. It retains the borderless fullscreen and Alt+Enter window switching introduced in 0.1.4; the extended stability and sanitizer results below were recorded on 0.1.0.

The reference is a separate SAGA2 build at revision `1a3e816afd5ddc41cc2a5c51a5da292e6d61758c`, modified only by the supplied reference patches. Successful startup with DOS resources was verified before separating the engine. The missing `FTA2WIN.EXE` dependency was resolved by extracting the actual DOS resources.

## Completed checks

| Area | Result and scope |
|---|---|
| Original installation | All 54 files match their baseline SHA-256 hashes, including after integration tests. |
| HRES | All six local archives inventoried; table and internal-resource bounds checked. Synthetic malformed archives are rejected. |
| DOS executable | Exact supported SHA-256 checked; unknown executables are rejected with expected and actual hashes. |
| Extraction | 23 resources, including six fonts. C++ and independent Python implementations produce identical 340,358-byte packs and identical manifests. |
| Public tests | 5/5 CTest tests: runtime, the data tool's six cases, the complete-package tool's eight cases, CLI help and version. Runtime checks cover streams, SHA-256, saves, RNG, mouse mapping after resizing, finite PCM looping and bundled-data discovery, including SDL's trailing path separator. The updated runtime test also passes as a native Windows process. |
| Startup and controls | New game, moving Julian, selecting Phillip, saving, switching characters and restoring the saved world. Automated input reaches the original game handlers. |
| New saves | Loading restores the actor, coordinates and exact RNG state. All 30 original chunks in both compared saves are byte-for-byte identical to the reference. |
| Original save | A copy of this installation's `999.SAV` loads from slot 2. The original file is not overwritten. |
| Image and simulation | Two complete RGB screens are byte-for-byte identical to the reference; four state checkpoints have matching actors, coordinates and RNG. |
| Audio | XMIDI/Miles/OPL music and the `CLK1` effect produce nonzero samples. Integration run: 276,278 music samples and 8,322 effect samples. This does not replace listening on an actual audio device. |
| Window | Native Windows checks pass for fullscreen startup without a title bar, full 1920 × 1080 monitor coverage including the taskbar area, Alt+Enter in both directions, held-key repeat suppression, right Alt plus keypad Enter and preservation of a 900 × 650 window. C++ mouse-coordinate checks pass with native Windows graphics at 640 × 480 and in a 1280 × 720 letterboxed window after fullscreen/windowed transitions. The shortcut is consumed before game input; ordinary Enter remains available. Earlier startup also passed in a real X11 window. |
| Stability | A deterministic run of 10,000 game steps, including saving and loading. This is a limited scenario in the starting area, not extended gameplay through the entire game. |
| Memory | AddressSanitizer and LeakSanitizer checks; leaks fixed in motion-task cancellation, pathfinding, world loading and resource cleanup. |
| Missing media | All local runs use the profile without voice data or videos. The Speech Text preference can be toggled; unvoiced dialogue and its choices remain visible. |
| Windows startup | Version 0.1.4 runs on Windows NT 10.0.26200.0 with native video/audio backends. A complete ZIP opens the game without a data path or folder picker, from an unrelated working directory and after relocation into a path containing spaces and Unicode characters. Bundled data wins over remembered paths; an explicit path overrides it. Damaged archives and missing resource sources report errors, with a visible dialog for interactive startup. |
| Packages | Complete and engine-only Windows x64 packages bundle SDL and licences. Complete packages include exactly eight verified game inputs, unchanged after testing. Source packages exclude original game data. Movement/save/load, original-save import and full reference parity pass on the actual Windows host. Native Windows/macOS CI has not been run remotely. |

Machine-readable results are in [`research/validation.json`](../research/validation.json). Baseline installation checksums are in [`research/original-files.json`](../research/original-files.json). Game images, saves and extracted graphics remain in development output directories and are excluded from the source package.

Windows cross-build details and the distinction between Wine checks and native Windows testing are documented in [Windows build and validation](windows-build.md). Earlier Wine results are in [`research/windows-build.json`](../research/windows-build.json); actual Windows results for 0.1.1 are in [`research/windows-native-validation.json`](../research/windows-native-validation.json).

Complete-package results for 0.1.2 are in [`research/windows-complete-validation.json`](../research/windows-complete-validation.json). The reproducible Windows test script uses isolated save directories, without `--data-dir` for automatic-startup scenarios. It intentionally damages and restores only a temporary extracted copy when exercising error handling.

Native Windows window-state results for 0.1.3 are in [`research/windows-window-validation.json`](../research/windows-window-validation.json). The desktop test uses keyboard messages scoped to its own game HWND, with raw keyboard input disabled for that test process. The separate C++ runtime test runs with native Windows video/audio backends.

Native Windows fullscreen results for 0.1.4 are in [`research/windows-fullscreen-validation.json`](../research/windows-fullscreen-validation.json). The test verifies caption removal, physical monitor bounds and that the game owns points in the taskbar area while active. It activates only its own test window and removes its temporary activation override before fullscreen assertions. The complete-package regression scenario and reference comparisons also passed again.

Numeric keypad regression results are in [`research/keypad-validation.json`](../research/keypad-validation.json). SDL keypad digits now share the ordinary number controls. Public runtime checks cover top-row `1`–`3` and keypad `0`–`9`; the integration scenario sends keypad `1`, `2` and `3` through SDL and verifies all three brother selections. Linux and native Windows runs passed with dummy video/audio backends, including reference image, checkpoint and save-chunk parity. These are automated input checks, not a manual hardware-keyboard test.

Hot Keys and music-toggle results for 0.1.6 are in [`research/hotkeys-validation.json`](../research/hotkeys-validation.json). Both Options layouts passed six repeated help openings each, alternating H/Esc with mouse/Close, while checking modal focus, settings, actor state and RNG. Gameplay shortcuts are blocked inside the overview. The M scenario verifies muted music, audible effect samples, preserved volume and successful unmuting. All five public tests, Linux integration, AddressSanitizer/LeakSanitizer and the packaged native Windows scenario passed. Windows used dummy SDL backends; the pre-game layout was exercised from the integration harness. Reference screens, four checkpoints and all 30 original chunks in each compared save remain unchanged. Six new UI captures stay under `.cache/integration-hotkeys-016-final/output/`.

Options regression results for 0.1.7 are in [`research/options-validation.json`](../research/options-validation.json). Empty Load closes safely without restarting; malformed saves are omitted. The scenario creates a named save through the actual Save menu, cancels one Load opening, then loads it after changing the selected brother and verifies restoration. Speech Text stays visibly checked and read-only. The original IMPO field identifies credits script 110; the credits book opens repeatedly, changes pages and closes through the bookmark or Escape. Nested dialogs restore Options modal focus. All five public tests, Linux integration, AddressSanitizer/LeakSanitizer and packaged native Windows execution passed. The six new UI screens match between Linux and Windows; existing reference images, checkpoints and original save chunks remain identical. New captures are under `.cache/integration-options-017-final/output/`. SDL dummy backends were used.

Speech Text regression results for 0.1.8 are in [`research/speech-validation.json`](../research/speech-validation.json). Mouse input toggles the original checkbox in both Options layouts, repeated reopening preserves it, and a second process loads the unchecked preference. Volume synchronization and M do not reset it. Dialogue with no voice samples or missing sample references renders identical text and choices with the checkbox checked or unchecked. Space dismisses plain dialogue; choices wait for input beyond the reading deadline and mouse selection continues the scenario. The new dialogue checks exposed a completed-speech allocation leak, fixed by deleting tasks after unlinking. All five public tests, two-process Linux integration, AddressSanitizer/LeakSanitizer and two-process packaged native Windows execution passed. Reference images, four checkpoints and all 30 original chunks in both compared saves remain identical. Six new UI captures match between Linux and Windows and stay under `.cache/integration-speech-018-final/output/`. These automated checks use SDL dummy video/audio backends; they do not establish a complete playthrough or device-audio quality.

Document cursor regression results for 0.1.9 are in [`research/cursor-validation.json`](../research/cursor-validation.json). A synthetic public test reproduces the old same-address bitmap refresh failure and passes with the fix. Each integration process exercises eight credits-book openings, four scroll openings and four parchment openings across both Options layouts. Exact displayed pixels and hotspots are checked during hover, dragging, release, stationary-pointer opening and closing without further mouse movement. Mouse navigation preserves the first and last spread limits, and closing restores Options focus and settings. All five public tests, two-process Linux integration, AddressSanitizer/LeakSanitizer and two-process packaged native Windows execution passed. Ten cursor-inclusive captures match between Linux and Windows and remain under `.cache/integration-cursor-019-v1/output/`; original reference captures, checkpoints and all compared save chunks remain identical. The existing page-break terminator fix and final-credits-text regression are preserved. SDL dummy video/audio backends were used.

Mouse-wheel regression results for 0.1.10 are in [`research/wheel-validation.json`](../research/wheel-validation.json). Public checks cover normal/flipped direction, fractional deltas and fullscreen/window/letterbox coordinate mapping. SDL wheel input in both vertical document appearances produces pages identical to arrow-key and mouse-click navigation. Integration covers multiple notches, first/last limits, outside and close-control input, horizontal-only input and reopening with a partial notch pending. Books, Credits, Options and Hot Keys ignore wheel input. The longer document tests permit the existing timed ambient sounds to advance RNG; their original behavior remains intact. All five public tests, two-process Linux integration, AddressSanitizer/LeakSanitizer and two-process packaged native Windows execution passed. Eight new page captures match Linux and Windows and remain under `.cache/integration-wheel-010-v3/output/`. Reference images, checkpoints and original save chunks remain identical. SDL dummy backends and injected input were used; no physical wheel or trackpad test is claimed.

Container Escape regression results for 0.1.11 are in [`research/container-escape-validation.json`](../research/container-escape-validation.json). Each integration process repeatedly opens and closes an original-data physical Sack and a mental container twelve times, cycling through Ideas, Skills and Memories. Escape closes the frontmost container before the underlying one, even with the pointer outside or after clicking the game background. O opens Options above the physical container; modal Escape leaves that container open. Escape opens Options once all containers are closed. Original object open flags, cleanup, actor coordinates, party flags and settings are checked. All five public tests, two-process Linux integration, ASan/LSan and two-process packaged native Windows execution passed. Both new captures match across Linux and Windows and remain under `.cache/integration-container-011-v6/output/`. Reference images, checkpoints and original save chunks remain identical. These automated checks use SDL dummy backends; no new manual hardware test or full playthrough is claimed.

Checksums of the compared PPM screens:

```text
initial.ppm     d4d6934ddd00ae559deac10d7f2fbc0472a59ac4e8c25f490c053daa0147a28c
controlled.ppm  89363eb8d556ceda731f7feabb4754d48301114312d8c6eccaf4b318d098991e
```

Map shortcut results for 0.1.12 are in [`research/map-shortcut-validation.json`](../research/map-shortcut-validation.json). Each integration process opens twelve maps, checking all three brothers against inventory double-click, direct inventory, closed and nested bags, transfer between brothers and first-map iterator order. The original script arguments, selected position/world, explored-area mode, inventory and modal settings are checked. Escape and the original close button restore the parent; Escape over an open physical Sack closes only the map. Missing, ground and other-brother maps, Ctrl/Alt, UI locks, held objects, dragging, Options and choice dialogue are covered. All five public tests, two-process Linux integration, ASan/LSan and two-process packaged native Windows execution passed. Nine map/help captures match across those backends in each corresponding process run and remain under `.cache/integration-map-012-final/`. Reference images, four checkpoints and all compared original save chunks remain identical. SDL dummy backends and injected input were used; all three original-save actors are in the same world, and a full playthrough remains pending.

## Reproduction

```sh
ctest --test-dir build -C Release --output-on-failure
python3 tools/fta2_data.py verify
python3 tools/integration_test.py --binary build/fta2-native --output .cache/integration
python3 tools/reference.py build
python3 tools/reference.py run
python3 tools/compare_reference.py --reference-output .cache/reference/output --reference-saves .cache/reference/saves --native-output .cache/integration/output --native-saves .cache/integration/saves
```

The integration output directory must be fresh. To run another limited stability scenario:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/fta2-native --data-dir original-game --save-dir .cache/soak-saves --smoke-test .cache/soak-output --smoke-frames 10000
```

For memory checks with GCC/Clang, build Debug with `-DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"` and run the scenario with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`. LeakSanitizer requires an environment that permits inspection of process threads.

## Required before declaring the port fully playable

- Document a complete playthrough. Verify doors, conversations, item manipulation, combat, spells, the map and area transitions separately; add reproducible regression scenarios.
- Manually verify the save/load dialog, all cursors and fonts in their respective screens, and input at different DPI settings.
- Listen to music and effects on actual devices and compare them with the reference. During a longer playthrough, check locations that trigger missing videos and speech.
- Run the configured CI, address any compiler differences and manually test Windows and macOS packages on both macOS architectures.
- Expand the regression suite using reproduced SAGA2 bugs. The limited stability run reports original `Sector::getSector` warnings about invalid coordinates; their impact outside this scenario has not been verified.

The local Linux package was built on Ubuntu 26.04 with GCC 15.2 and CMake 3.31.8. It bundles SDL; the target system supplies standard C/C++ libraries and graphics/audio services. Compatibility of this local package with older distributions has not been established. Linux CI builds on Ubuntu 22.04 to target an older system baseline.

The DE interface, new graphics and tools for story-script source files belong to a later stage. The original gameplay algorithms have been ported; complete functional parity requires the acceptance checks above.
