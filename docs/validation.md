# Native alpha 0.1.5 validation status

The result is a standalone C++20 engine using SDL 3.4.18. It was built and run on Linux and the actual Windows host with the original DOS data, without DOSBox or the ScummVM framework. **This is a development alpha; a complete playthrough of the port has not yet been demonstrated.** Version 0.1.5 fixes numeric keypad input, including brother selection with `1`/`2`/`3`. It retains the borderless fullscreen and Alt+Enter window switching introduced in 0.1.4; the extended stability and sanitizer results below were recorded on 0.1.0.

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
| Missing media | All local runs use the profile without voice data or videos. Dialogue text cannot be disabled in the options menu. |
| Windows startup | Version 0.1.4 runs on Windows NT 10.0.26200.0 with native video/audio backends. A complete ZIP opens the game without a data path or folder picker, from an unrelated working directory and after relocation into a path containing spaces and Unicode characters. Bundled data wins over remembered paths; an explicit path overrides it. Damaged archives and missing resource sources report errors, with a visible dialog for interactive startup. |
| Packages | Complete and engine-only Windows x64 packages bundle SDL and licences. Complete packages include exactly eight verified game inputs, unchanged after testing. Source packages exclude original game data. Movement/save/load, original-save import and full reference parity pass on the actual Windows host. Native Windows/macOS CI has not been run remotely. |

Machine-readable results are in [`research/validation.json`](../research/validation.json). Baseline installation checksums are in [`research/original-files.json`](../research/original-files.json). Game images, saves and extracted graphics remain in development output directories and are excluded from the source package.

Windows cross-build details and the distinction between Wine checks and native Windows testing are documented in [Windows build and validation](windows-build.md). Earlier Wine results are in [`research/windows-build.json`](../research/windows-build.json); actual Windows results for 0.1.1 are in [`research/windows-native-validation.json`](../research/windows-native-validation.json).

Complete-package results for 0.1.2 are in [`research/windows-complete-validation.json`](../research/windows-complete-validation.json). The reproducible Windows test script uses isolated save directories, without `--data-dir` for automatic-startup scenarios. It intentionally damages and restores only a temporary extracted copy when exercising error handling.

Native Windows window-state results for 0.1.3 are in [`research/windows-window-validation.json`](../research/windows-window-validation.json). The desktop test uses keyboard messages scoped to its own game HWND, with raw keyboard input disabled for that test process. The separate C++ runtime test runs with native Windows video/audio backends.

Native Windows fullscreen results for 0.1.4 are in [`research/windows-fullscreen-validation.json`](../research/windows-fullscreen-validation.json). The test verifies caption removal, physical monitor bounds and that the game owns points in the taskbar area while active. It activates only its own test window and removes its temporary activation override before fullscreen assertions. The complete-package regression scenario and reference comparisons also passed again.

Numeric keypad regression results are in [`research/keypad-validation.json`](../research/keypad-validation.json). SDL keypad digits now share the ordinary number controls. Public runtime checks cover top-row `1`–`3` and keypad `0`–`9`; the integration scenario sends keypad `1`, `2` and `3` through SDL and verifies all three brother selections. Linux and native Windows runs passed with dummy video/audio backends, including reference image, checkpoint and save-chunk parity. These are automated input checks, not a manual hardware-keyboard test.

Checksums of the compared PPM screens:

```text
initial.ppm     d4d6934ddd00ae559deac10d7f2fbc0472a59ac4e8c25f490c053daa0147a28c
controlled.ppm  89363eb8d556ceda731f7feabb4754d48301114312d8c6eccaf4b318d098991e
```

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
