# Windows x64 build and package

The Windows x64 alpha package contains `bin/fta2-native.exe`, `bin/SDL3.dll`, documentation and licence notices. Game data is supplied separately. Extract the entire ZIP so the DLL stays beside the executable, then double-click `bin/fta2-native.exe`. Select the original game folder containing `FTA2.EXE` when prompted. The valid selection is stored in `config.ini`, so subsequent launches start directly. Cancelling the picker exits normally; startup errors appear in a visible dialog.

Alternatively, run from PowerShell with an explicit path:

```powershell
.\bin\fta2-native.exe --data-dir "C:\Games\FTA2"
```

Use `--save-dir "C:\path\to\saves"` to override the default SDL user directory. Keep saves outside the original installation. Windows command-line paths and SDL filesystem calls use UTF-8, with native wide paths for C++ file streams.

Version 0.1.0 expected `original-game` relative to the working directory when no path was supplied. Double-clicking the packaged executable therefore closed its console immediately after a missing-directory error. Version 0.1.1 adds discovery, the folder picker, a remembered path and visible errors. Explicit command-line paths and automated probes remain noninteractive.

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

## Validation scope

The Windows executable passed all four public CTest checks under Wine 10, including save operations in a directory containing non-ASCII characters. It also passed the original-data integration scenario: resource extraction, movement, brother selection, saving/restoring RNG state, original-save import and rejection of invalid profiles or output inside the original installation.

Both screen captures, four simulation checkpoints and all 30 original world chunks in each compared save match the separate ScummVM reference exactly. The original cross-build/Wine evidence remains in `research/windows-build.json`.

The executable was also launched on the actual Windows host through Windows PowerShell, using a test copy in Windows TEMP. These tests run as Windows processes with native graphics/audio backends, outside WSL and Wine. They cover the new folder-selection flow, cancellation, visible errors, remembered paths and the original-data movement/save/load scenario. See `research/windows-native-validation.json` for the recorded scope. MSVC CI, subjective audio comparison and a full playthrough remain pending.
