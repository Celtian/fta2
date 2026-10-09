# Windows x64 build and package

The complete Windows x64 alpha ZIP contains `bin/fta2-native.exe`, `bin/SDL3.dll`, documentation, licence notices and the eight required game files under `data/`. Extract the entire ZIP and double-click `bin/fta2-native.exe`. The game starts directly, with no folder selection or separate original installation. Keep `bin/`, `data/` and `share/` together when moving the game. The data remains in its original format; `FTA2.EXE` is read for resources and never executed.

Public CI produces engine-only packages without game data. Their Windows folder picker remains available when no installation is found. Cancelling it exits normally; startup errors appear in a visible dialog.

Alternatively, run from PowerShell with an explicit path:

```powershell
.\bin\fta2-native.exe --data-dir "C:\Games\FTA2"
```

Use `--save-dir "C:\path\to\saves"` to override the default SDL user directory. Keep saves outside the original installation. Windows command-line paths and SDL filesystem calls use UTF-8, with native wide paths for C++ file streams.

Version 0.1.0 expected `original-game` relative to the working directory when no path was supplied. Double-clicking the packaged executable therefore closed its console immediately after a missing-directory error. Version 0.1.1 adds discovery, the folder picker, a remembered path and visible errors. Explicit command-line paths and automated probes remain noninteractive.

Version 0.1.2 adds complete local packages and prioritizes executable-relative bundled data over remembered installations. An explicit `--data-dir` still wins. Incomplete or damaged bundled data reports an error instead of falling back to a different installation.

The local build was cross-compiled on Linux using Zig 0.16.0 (Clang 21.1.0 and its bundled Windows/C++ runtimes). It links C++ runtime code statically and includes the official SDL 3.4.18 x64 DLL. Apart from SDL, the executable imports Windows system libraries, including the Universal C Runtime. Target Windows 10/11 x64; older Windows releases have not been validated.

## Reproduce the cross-build

Install Zig 0.16.0, CMake 3.24+ and a compatible build tool on the POSIX host. Download the official [SDL MinGW development archive](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18):

```sh
mkdir -p .cache/windows-sdl
curl -fL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-mingw.tar.gz -o .cache/windows-sdl.tar.gz
```

Verify the archive's SHA-256 before extracting:

```text
049b9dd711e3fb3f8d84feb998dfec54a75e17cf539c4a80ff4d058d839771cf
```

```sh
tar -xf .cache/windows-sdl.tar.gz -C .cache/windows-sdl
export ZIG_GLOBAL_CACHE_DIR="$PWD/.cache/zig-global"
export ZIG_LOCAL_CACHE_DIR="$PWD/.cache/zig-local"
cmake -S . -B build-windows-x64 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/windows-zig.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/.cache/windows-sdl/SDL3-3.4.18/x86_64-w64-mingw32" -DFTA2_FETCH_SDL=OFF
cmake --build build-windows-x64 --parallel 4
cpack --config build-windows-x64/CPackConfig.cmake -C Release -B dist
```

The native Windows CI build uses MSVC and the regular instructions in the main README; it does not require Zig. Runtime licence notices for the cross-build are in `third_party/windows-runtime/` and are included in packages.

## Build the complete package locally

After generating the Windows engine ZIP:

```sh
python3 tools/package_complete.py --engine-package dist/fta2-native-0.1.2-Windows-x86_64.zip --data-dir original-game --output dist/fta2-native-0.1.2-Windows-x86_64-complete.zip
cpack --config build-windows-x64/CPackSourceConfig.cmake -B dist
```

The complete-package tool accepts only the supported eight files, checked against the baseline SHA-256 hashes in `research/original-files.json`. It excludes original saves, the unusable voice archive and other DOS components. It writes provenance to `share/fta2-native/complete-package.json` and refuses to modify the original installation or overwrite an existing output ZIP. Source packages remain free of original game data. The engine's GPL does not relicense the bundled original data.

On an actual Windows desktop, validate a complete ZIP with isolated saves and a fresh temporary output directory:

```powershell
.\tools\test_complete_windows.ps1 -Package .\dist\fta2-native-0.1.2-Windows-x86_64-complete.zip -OutputDirectory "$env:TEMP\fta2-complete-check" -OriginalSave .\original-game\999.SAV
```

The optional `-OriginalSave` is copied before loading. The script opens and closes only its own test windows, moves the extracted game into a path containing spaces and non-ASCII characters, and verifies startup, data precedence, visible errors and gameplay/save restoration. It writes `validation.json`, screenshots, saves and logs under the test output directory.

## Validation scope

The Windows executable passed all four public CTest checks under Wine 10, including save operations in a directory containing non-ASCII characters. It also passed the original-data integration scenario: resource extraction, movement, brother selection, saving/restoring RNG state, original-save import and rejection of invalid profiles or output inside the original installation.

Both screen captures, four simulation checkpoints and all 30 original world chunks in each compared save match the separate ScummVM reference exactly. The original cross-build/Wine evidence remains in `research/windows-build.json`.

The executable was also launched on the actual Windows host through Windows PowerShell, using a test copy in Windows TEMP. These tests run as Windows processes with native graphics/audio backends, outside WSL and Wine. They cover the new folder-selection flow, cancellation, visible errors, remembered paths and the original-data movement/save/load scenario. See `research/windows-native-validation.json` for the recorded scope. MSVC CI, subjective audio comparison and a full playthrough remain pending.

For 0.1.2, the complete ZIP passed native Windows startup without a data path, relocation into a Unicode path, precedence over remembered installations, explicit overrides, visible archive-error handling and refusal to fall back from an incomplete bundle. Movement, brother selection, save restoration, original-save import, both reference images and all 30 original chunks in each compared save also passed. The eight bundled data files and all 54 original input files remain unchanged. Results are recorded in `research/windows-complete-validation.json`.
