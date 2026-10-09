# Native alpha 0.1.0 validation status

The result is a standalone C++20 engine using SDL 3.4.18. It was built and run on the local Linux system with the original DOS data, without DOSBox or the ScummVM framework. **This is a development alpha; a complete playthrough of the port has not yet been demonstrated.**

The reference is a separate SAGA2 build at revision `1a3e816afd5ddc41cc2a5c51a5da292e6d61758c`, modified only by the supplied reference patches. Successful startup with DOS resources was verified before separating the engine. The missing `FTA2WIN.EXE` dependency was resolved by extracting the actual DOS resources.

## Completed checks

| Area | Result and scope |
|---|---|
| Original installation | All 54 files match their baseline SHA-256 hashes, including after integration tests. |
| HRES | All six local archives inventoried; table and internal-resource bounds checked. Synthetic malformed archives are rejected. |
| DOS executable | Exact supported SHA-256 checked; unknown executables are rejected with expected and actual hashes. |
| Extraction | 23 resources, including six fonts. C++ and independent Python implementations produce identical 340,358-byte packs and identical manifests. |
| Public tests | 4/4 CTest tests: runtime, the data tool's six cases, CLI help and version. Runtime checks cover streams, SHA-256, saves, RNG, mouse mapping after resizing and finite PCM looping. |
| Startup and controls | New game, moving Julian, selecting Phillip, saving, switching characters and restoring the saved world. Automated input reaches the original game handlers. |
| New saves | Loading restores the actor, coordinates and exact RNG state. All 30 original chunks in both compared saves are byte-for-byte identical to the reference. |
| Original save | A copy of this installation's `999.SAV` loads from slot 2. The original file is not overwritten. |
| Image and simulation | Two complete RGB screens are byte-for-byte identical to the reference; four state checkpoints have matching actors, coordinates and RNG. |
| Audio | XMIDI/Miles/OPL music and the `CLK1` effect produce nonzero samples. Integration run: 276,278 music samples and 8,322 effect samples. This does not replace listening on an actual audio device. |
| Window | Startup in a real X11 window; automatic mouse checks at 640 × 480 and in a 1280 × 720 letterboxed window. |
| Stability | A deterministic run of 10,000 game steps, including saving and loading. This is a limited scenario in the starting area, not extended gameplay through the entire game. |
| Memory | AddressSanitizer and LeakSanitizer checks; leaks fixed in motion-task cancellation, pathfinding, world loading and resource cleanup. |
| Missing media | All local runs use the profile without voice data or videos. Dialogue text cannot be disabled in the options menu. |
| Packages | Linux x64 package with bundled SDL and licences; the source ZIP excludes original game data. CI is configured for Windows x64 and macOS arm64/x64, but has not been run remotely. |

Machine-readable results are in [`research/validation.json`](../research/validation.json). Baseline installation checksums are in [`research/original-files.json`](../research/original-files.json). Game images, saves and extracted graphics remain in development output directories and are excluded from the source package.

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
