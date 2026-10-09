# Faery Tale Adventure II — native

A standalone C++20 engine based on SAGA2, serving as the foundation for a future Definitive Edition. It runs the original DOS game data directly, without DOSBox or a ScummVM installation. The code is licensed under GPL-3.0-or-later.

**Status: development alpha.** Startup, controls, audio generation, new saves and loading an original save have been verified on Linux and the actual Windows host. Two captured scenes and all 30 world-state chunks match the reference. A full playthrough and macOS validation are still pending; see [validation status](docs/validation.md).

## Build and run

You need CMake 3.24+, a C++20 compiler and your platform's graphics/audio development libraries. SDL **3.4.18** is downloaded automatically from a checksum-verified release archive. To use an installed copy of that exact version, set `CMAKE_PREFIX_PATH`; for an offline build, also pass `-DFTA2_FETCH_SDL=OFF`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
./build/fta2-native --data-dir ./original-game
```

On Windows, the default generator places the executable at `build/Release/fta2-native.exe`. On macOS, pass `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` to build a universal binary. [CI](.github/workflows/build.yml) is configured for Linux x64, Windows x64 and both macOS architectures.

The **complete Windows x64 ZIP** includes `bin/fta2-native.exe`, `bin/SDL3.dll` and the required files under `data/`. Extract the entire ZIP and double-click `bin/fta2-native.exe`. No folder selection or separate original installation is needed. Keep `bin/`, `data/` and `share/` together when moving the game. Startup errors appear in a visible dialog.

Engine-only packages built by public CI require separately supplied game data. They retain the Windows folder picker. The optional `--data-dir` argument selects an explicit installation for either package. See [Windows build and validation](docs/windows-build.md).

The application uses the [ScummVM community game icon](third_party/APP-ICON.md), with its logo and cover illustration. It is embedded in the Windows executable and used for the SDL window; PNG and ICO files are also included under `share/fta2-native/icons/`.

```sh
fta2-native --data-dir /path/to/game --save-dir /path/to/new/saves
fta2-native --help
fta2-native --version
```

Without `--save-dir`, the application uses the user directory returned by SDL. It contains `config.ini`, saves named `fta2.000`–`fta2.999`, and `resources/` with automatically extracted resources and their manifest. Output directories must be outside the game data directory. Slot 999 holds the restart state created by the engine.

The original controls are preserved: use the mouse, `1`/`2`/`3` to switch brothers, `O` or Escape to open options including save/load, and Tab to switch between group and individual control. The 640 × 480 image uses nearest-neighbour scaling and retains its 4:3 aspect ratio.

## Game data

The supported English DOS profile requires `FTA2.EXE`, `FTA.HRS`, `FTADATA.HRS`, `FTAIMAGE.HRS`, `FTASOUND.HRS`, `SCRIPTS.HRS`, and the instrument banks `DRIVERS/SAMPLE.AD` and `DRIVERS/SAMPLE.OPL`. The executable, main archives and banks are checked against SHA-256 hashes. Unknown executables are rejected; Windows executable offsets are never applied to the DOS executable.

This installation's `FTAVOICE.HRS` is a copy of `FTADATA.HRS`. The engine ignores it, keeps dialogue text enabled and skips missing videos. Original and bundled game data are opened read-only. Source and engine-only packages exclude game data; the explicitly requested local complete package includes only the eight required files. The original `FTA2.EXE` supplies embedded resources and is never executed. Game data retains its original copyright and is not relicensed under the engine's GPL.

You can **copy** the original `999.SAV` to the user save directory, for example as `fta2.002`, and load it through the game menu. New saves preserve the original chunks and add the random generator state; see [architecture](docs/architecture.md).

## Validation and packaging

```sh
ctest --test-dir build -C Release --output-on-failure
python3 tools/fta2_data.py verify
python3 tools/fta2_data.py inventory
python3 tools/fta2_data.py extract --output .cache/extracted
python3 tools/integration_test.py --binary build/fta2-native --output .cache/integration
cpack --config build/CPackConfig.cmake -C Release
```

To create the complete Windows ZIP locally after packaging the Windows engine:

```sh
python3 tools/package_complete.py --engine-package dist/fta2-native-0.1.2-Windows-x86_64.zip --data-dir original-game --output dist/fta2-native-0.1.2-Windows-x86_64-complete.zip
```

The tool checks all eight data files against their known SHA-256 hashes, excludes original saves and unnecessary DOS files, and records a package manifest. It refuses unknown data, output inside the original installation, and overwriting an existing output ZIP.

The integration test requires original game data and a fresh output directory. Public CI tests use synthetic data only. Runtime packages include the engine, SDL and licences. For more information, see [DOS resources](docs/dos-resources.md), the [development reference](reference/README.md), [code provenance](third_party/README.md) and the [remaining acceptance checks](docs/validation.md).

Repository conventions for coding agents are documented in [AGENTS.md](AGENTS.md). All project documentation, code comments and tool messages should be written in English.
