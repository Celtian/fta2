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

Version 0.1.3 starts maximized. Alt+Enter toggles between maximized and restored windows and preserves the restored dimensions. Either Alt key and either Enter key work; holding the shortcut does not repeatedly toggle. The game keeps its 4:3 image and nearest-neighbour scaling.

Version 0.1.4 replaces maximization with borderless fullscreen by default, without the title bar or Windows taskbar. Alt+Enter switches between fullscreen and a resizable window, preserving its previous size and position. The monitor resolution remains unchanged.

Version 0.1.5 fixes numeric keypad input. Keypad `1`/`2`/`3` now select Julian, Philip and Kevin, matching the top-row number controls. Keypad digits also work in text fields.

Version 0.1.6 adds Hot Keys below Credits in Options, with an English shortcut overview and the original button artwork. H opens it from Options; Close or Escape returns to the same menu. M now toggles music during gameplay without changing the volume slider or the track's timing.

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
python3 tools/package_complete.py --engine-package dist/fta2-native-0.1.27-Windows-x86_64.zip --data-dir original-game --output dist/fta2-native-0.1.27-Windows-x86_64-complete.zip
cpack --config build-windows-x64/CPackSourceConfig.cmake -B dist
```

The complete-package tool accepts only the supported eight files, checked against the baseline SHA-256 hashes in `research/original-files.json`. It excludes original saves, the unusable voice archive and other DOS components. It writes provenance to `share/fta2-native/complete-package.json` and refuses to modify the original installation or overwrite an existing output ZIP. Source packages remain free of original game data. The engine's GPL does not relicense the bundled original data.

On an actual Windows desktop, validate a complete ZIP with isolated saves and a fresh temporary output directory:

```powershell
.\tools\test_complete_windows.ps1 -Package .\dist\fta2-native-0.1.27-Windows-x86_64-complete.zip -OutputDirectory "$env:TEMP\fta2-complete-check" -OriginalSave .\original-game\999.SAV
```

The optional `-OriginalSave` is copied before loading. The script opens and closes only its own test windows, moves the extracted game into a path containing spaces and non-ASCII characters, and verifies startup, data precedence, visible errors and gameplay/save restoration. It writes `validation.json`, screenshots, saves and logs under the test output directory.

To validate fullscreen and Alt+Enter against a complete ZIP on the actual Windows desktop:

```powershell
.\tools\test_window_windows.ps1 -Package .\dist\fta2-native-0.1.27-Windows-x86_64-complete.zip -OutputDirectory "$env:TEMP\fta2-window-check"
```

This test sends native keyboard messages only to its own game window, with SDL raw keyboard input disabled for that test process. It checks fullscreen startup by comparing physical window and monitor bounds and checking that the title bar is absent, including coverage beyond the desktop work area occupied by the taskbar. It activates only its own game window using a verified click, restores normal stacking and the cursor position before assertions, and checks both toggle directions, repeat suppression, right Alt with keypad Enter and preservation of a resized window.

## Validation scope

The Windows executable passed all four public CTest checks under Wine 10, including save operations in a directory containing non-ASCII characters. It also passed the original-data integration scenario: resource extraction, movement, brother selection, saving/restoring RNG state, original-save import and rejection of invalid profiles or output inside the original installation.

Both screen captures, four simulation checkpoints and all 30 original world chunks in each compared save match the separate ScummVM reference exactly. The original cross-build/Wine evidence remains in `research/windows-build.json`.

The executable was also launched on the actual Windows host through Windows PowerShell, using a test copy in Windows TEMP. These tests run as Windows processes with native graphics/audio backends, outside WSL and Wine. They cover the new folder-selection flow, cancellation, visible errors, remembered paths and the original-data movement/save/load scenario. See `research/windows-native-validation.json` for the recorded scope. MSVC CI, subjective audio comparison and a full playthrough remain pending.

For 0.1.2, the complete ZIP passed native Windows startup without a data path, relocation into a Unicode path, precedence over remembered installations, explicit overrides, visible archive-error handling and refusal to fall back from an incomplete bundle. Movement, brother selection, save restoration, original-save import, both reference images and all 30 original chunks in each compared save also passed. The eight bundled data files and all 54 original input files remain unchanged. Results are recorded in `research/windows-complete-validation.json`.

Version 0.1.3 passed the same complete-package scenario and reference comparisons. Native Windows checks also passed for maximized startup, both Alt+Enter directions, repeat suppression, manual restoration, right Alt with keypad Enter and restoration to the previous 900 × 650 outer window size. The C++ runtime checks passed with native Windows graphics/audio, including mouse mapping after maximize/restore and resizing. Results are in `research/windows-window-validation.json`.

Version 0.1.4 passed fullscreen startup without a title bar, complete monitor coverage (1920 × 1080, including the taskbar area), Alt+Enter in both directions, repeat suppression and the previous 900 × 650 window size. Native Windows runtime, complete-package regression tests and full reference comparisons passed again. Results are in `research/windows-fullscreen-validation.json`.

Version 0.1.7 fixes Options load handling, the visible mandatory Speech Text checkbox and original Credits display. Existing 0.1.6 archives remain available; current package commands above use 0.1.23.

Version 0.1.8 restores the interactive Speech Text preference and remembers it across restarts and sound-setting updates. Missing or muted voice requests fail explicitly so the original unvoiced dialogue fallback remains readable.

Version 0.1.9 refreshes cursor pixels even when a rebuilt bitmap reuses its address and dimensions. Credits chooses the page cursor when opening and during pointer movement, dragging and release, and restores the normal pointer when closed. Scroll and parchment retain their vertical page cursors.

Version 0.1.10 adds mouse-wheel page navigation to vertical scrolls and parchment. Wheel input is routed only to the active document under the pointer, with one page per notch and fractional movement accumulated per document.

Version 0.1.11 makes Escape close the frontmost open container before opening Options. The packaged native Windows regression opens and closes an original-data physical container and Ideas/Skills/Memories repeatedly, checks O and modal Escape, and retains all earlier scenarios. Two Windows processes and the public runtime test passed with SDL dummy backends; see `research/container-escape-validation.json`.

Version 0.1.12 adds G/Shift+G for the selected brother's carried map, including closed or nested bags. The public runtime test and two packaged native Windows processes passed with SDL dummy video/audio backends, covering ownership, missing maps, modifiers, modal input and parent-container restoration. Map/help captures and reference images, checkpoints and save chunks match Linux; see `research/map-shortcut-validation.json`. These automated checks do not establish a complete playthrough or physical-device input quality.

Version 0.1.13 adds **Widescreen (16:9)** in Options. The map expands to 654 × 420 inside an 854 × 480 framebuffer while the sidebar retains its size. The setting applies after Options closes and persists across restarts. The packaged native Windows runtime, two widescreen runs and the classic regression scenario passed with SDL dummy backends. Wide captures match Linux; classic images, checkpoints and all original save chunks match the reference. Manual widescreen desktop/input checks remain pending; see `research/widescreen-validation.json`.

Version 0.1.14 places Widescreen below Night, restores the original 18 × 17 checkbox redraw/hit bounds, and returns Options to its 263-pixel height. Native Windows tests cover repeated checked/unchecked pixel parity and clicks at all previously clipped edges in both Options variants and display modes. The Windows C++ CTest cases, classic scenario and reference comparison also pass; all 26 wide/Options captures match Linux. These checks use SDL dummy backends. See `research/options-layout-validation.json`.

Version 0.1.15 makes the automatic window size follow the active display mode, including changes made in fullscreen before the next Alt+Enter. Manual sizes, maximization and position survive subsequent transitions. The actual Windows video backend passed the extended C++ runtime checks for both startup modes, automatic sizing under desktop constraints, maximization, manual 900 × 650 restoration, input mapping and repeat suppression. Two packaged widescreen processes, the classic regression and reference comparisons passed with dummy backends. All 54 corresponding wide/Options/exploration captures match Linux, including the new per-brother and second-world map checks. See [`research/window-map-validation.json`](../research/window-map-validation.json).

Version 0.1.16 fixes compressed sidebar button invalidation after widescreen translation, including Center On, aggression, banding, brother selectors and portraits. Both Windows and Linux CTest suites pass (5/5 each). Two packaged native Windows widescreen processes pass 200 immediate-versus-full-redraw checks each; their 51 corresponding PPM captures per process match Linux, including 24 new button captures. The classic scenario matches the existing reference screens, checkpoints and all 30 original chunks in each compared save. The separate Windows runtime suite passes with native video; gameplay scenarios use dummy backends. Complete and source ZIPs retain development-alpha status. See [`research/button-redraw-validation.json`](../research/button-redraw-validation.json), including the separate existing ready-inventory displacement after loading in widescreen.

Version 0.1.17 adds **New layout** in Options for compact mental controls beside HP and a per-brother carried-gold counter below the inventory. Both display modes and Options variants pass, including nested closed bags, transfers, restoration and remembered startup preference. Ready-inventory slots now retain the active widescreen offset after loading. Both Release builds and CTest suites pass (5/5 each), as do two packaged native Windows widescreen processes and the classic scenario. All 65 corresponding captures per widescreen process match Linux; classic screens, checkpoints and all original chunks match the rebuilt reference. The final Windows package run uses local Windows TEMP after a prior WSL-share run hit Access denied during temporary-save replacement. The separate runtime suite also passes with native Windows video; gameplay uses dummy backends. See [`research/sidebar-layout-validation.json`](../research/sidebar-layout-validation.json).

Version 0.1.18 extends **New layout** with a compact mental control for each brother in group view and original coin-pile artwork above the individual gold total. Both Release builds, both CTest suites (5/5 each), two packaged native Windows widescreen processes and the classic scenario pass. Each process checks 102 category clicks, 90 pressed states and 90 rendered tooltips; all 87 corresponding captures match Linux. Default 4:3 screens, checkpoints and all original save chunks match the rebuilt reference. Separate runtime checks also pass with native Windows video. Gameplay uses dummy backends, and manual hardware input remains pending. See [`research/group-sidebar-validation.json`](../research/group-sidebar-validation.json).

Version 0.1.19 aligns all six optional sidebar controls to 28 × 27 cells with five-pixel gaps and composites HP into its compact cell. Individual gold now uses original gold decoration, a warm pale background and dark brown fitted digits. Both Release builds, both CTest suites (5/5 each), two packaged native Windows widescreen processes, classic reference comparisons and ASan/LSan pass. Each process checks 72 cells, 288 hit-region corners and tooltips, 60 HP changes and 144 button press/release redraws, alongside the existing mental and gold fixtures. All 87 corresponding captures per process match Linux. Default screenshots, checkpoints and all 30 original chunks in each compared save remain identical to the rebuilt reference. Native Windows video runtime checks also pass; gameplay uses dummy backends. Engine, complete and matching source ZIPs preserve earlier packages and development-alpha status. See [`research/sidebar-grid-validation.json`](../research/sidebar-grid-validation.json).

Version 0.1.20 gives the individual coin counter the full original circular gold bezel, bronze shading and light gold digits. Hover shows an exact live Gold tooltip, including inventory changes without pointer movement. Both Release builds, both CTest suites (5/5 each), two packaged native Windows widescreen processes, classic reference comparisons and ASan/LSan pass. Each process checks 86 gold-tooltip states, ten inactive-panel cases and two passive clicks alongside the existing grid and inventory fixtures. All 93 corresponding captures per process match Linux, including six with the actual tooltip cursor bitmap. Default screenshots, checkpoints and all 30 original chunks in each compared save remain identical to the rebuilt reference. Native Windows video runtime checks also pass; gameplay uses dummy backends. New engine, complete and source ZIPs preserve previous packages and development-alpha status. See [`research/coin-medallion-validation.json`](../research/coin-medallion-validation.json).

Version 0.1.21 anchors default Ideas/Spells/Skills windows over the active sidebar, retaining their original 158 × 215 size and vertical position. Both saved default rectangles are recognized; custom positions survive reopening and category reuse. Both Release builds and CTest suites (5/5 each), two packaged native Windows widescreen processes, classic reference comparisons and ASan/LSan pass. Each widescreen process verifies 84 default positions, 18 remembered-position cases and 12 node-serialization round trips. All 105 corresponding captures per process match Linux, including twelve new default-window captures; all six classic/wide window pairs match after translation. Default 4:3 screenshots, checkpoints and all 30 original chunks in each compared save match the rebuilt reference, and all 54 original files remain unchanged. Separate native Windows video runtime checks pass; gameplay uses dummy backends. Engine, complete and matching source ZIPs preserve previous packages and development-alpha status. See [`research/mental-window-validation.json`](../research/mental-window-validation.json).

Version 0.1.22 joins the optional gold, weight and volume medallions into one continuous 133 × 43 strip, repeating the original internal joint with a 45-pixel pitch. Original weight/volume pies and hit regions stay in place, and disabling New layout restores the original panel exactly. Both Release builds, both CTest suites (5/5 each), two packaged native Windows widescreen processes, classic reference comparisons and ASan/LSan pass. Each process checks fourteen whole-footer redraws and joint pixel comparisons, gold hover edges and unchanged weight/volume hit regions. All 105 corresponding captures per process match Linux. Default 4:3 screenshots, checkpoints and all 30 original chunks in each compared save match the rebuilt reference; all 54 original files remain unchanged. Native Windows video runtime checks also pass; gameplay uses dummy backends. Engine, complete and matching source ZIPs preserve older packages and development-alpha status. See [`research/carry-footer-validation.json`](../research/carry-footer-validation.json).

Version 0.1.23 clears hover text and delayed inventory tooltip state when closing a window, including with Escape. The hovered control receives pointer leave before its window and focus references are removed. A public regression fails against the old close implementation and passes on Linux and Windows. Both Release builds, both CTest suites (5/5 each), two packaged native Windows widescreen processes, classic reference comparisons and classic/widescreen ASan/LSan scenarios pass. Each widescreen process checks 84 mental-window hover closures, including 54 real item labels; the classic scenario checks twelve physical-inventory hover closures. All 129 corresponding captures per widescreen process match Linux, including 24 new cursor-composited before/after captures. Original 4:3 screenshots, checkpoints and all 30 original chunks in each compared save match the rebuilt reference, and all 54 original files remain unchanged. Separate runtime checks pass with native Windows video; gameplay uses dummy backends. Engine, complete and matching source ZIPs preserve previous packages and development-alpha status. See [`research/window-tooltip-validation.json`](../research/window-tooltip-validation.json).

Version 0.1.24 adds live carrier gemstones to physical container windows in both display modes and with New layout enabled or disabled. Red identifies Julian, blue Phillip and yellow Kevin; hovering shows the full name. Nested bags follow their actual carrier and neutral containers remain unmarked. The packaged native Windows scenarios pass 176 redraw comparisons, 552 tooltip checks, 256 edge checks, 280 transfers and four save/load checks per owner fixture. Both Release builds and CTest suites (5/5 each), classic/widescreen ASan/LSan and reference comparisons pass. All 177 corresponding widescreen captures per process match Linux. Original data remains unchanged; gameplay uses dummy backends and separate runtime checks pass with native Windows video. See [`research/container-owner-validation.json`](../research/container-owner-validation.json) for completed checks and limitations; development-alpha status is retained.

Version 0.1.25 replaces the small carrier gemstone with a wider 33 × 15 gold-rimmed colored plaque and original-font J/P/K initials. All 48 owner captures verify visible, distinct letters across display modes, layouts and decorations; immediate redraw, tooltip, transfer, neutral-carrier, Escape and save/load checks pass again. Linux and Windows Release builds and CTest suites (5/5 each), classic/widescreen integrations and Linux ASan/LSan pass. All 177 corresponding widescreen captures per process match across platforms. Default 4:3 screenshots, checkpoints and all 30 original chunks in each of two saves match the rebuilt reference. All 54 original files and eight bundled inputs remain unchanged. Packaged gameplay uses dummy backends and injected events; separate runtime checks pass with native Windows video. Manual hardware input, macOS and a complete playthrough remain unverified. See [`research/container-owner-plaque-validation.json`](../research/container-owner-plaque-validation.json) for completed checks and limitations. Development-alpha status is retained.

Version 0.1.26 restores a faceted diamond carrier marker, enlarged to 25 × 21 with J/P/K inside. All 48 owner captures check complete original-font initials across modes, layouts and decorations; redraws, tooltips, transfers, neutral carriers, Escape and save/load pass again. Both Release builds and CTest suites (5/5 each), classic/widescreen integrations and Linux ASan/LSan pass. All 177 corresponding widescreen captures per process match Linux and Windows. Default screenshots, checkpoints and all 30 original chunks in each of two saves match the rebuilt reference. All 54 original files and eight bundled inputs remain unchanged. Packaged gameplay uses dummy backends and injected events; separate runtime checks pass with native Windows video. Manual hardware input, macOS and a complete playthrough remain unverified. See [`research/container-owner-diamond-validation.json`](../research/container-owner-diamond-validation.json). Development-alpha status is retained.

Version 0.1.27 adds the shared J/P/K diamond to Ideas/Spells/Skills windows, with a passive actor-name tooltip. All brothers and categories pass in both panels, layouts and display modes. Each fixture checks 288 immediate/full diamond redraws, 744 tooltip states, 576 edges, 72 passive clicks, 24 save/load cases and 24 Escape closures. Original automatic hiding/reopening and header category actions are retained. Full indexed-stamp presentation also fixes missing colors on normal physical-marker opening. Both Release builds and CTest suites (5/5 each), classic/widescreen integrations and Linux ASan/LSan pass. Linux public tests use isolated TMPDIR. All 249 corresponding widescreen captures per process match Linux and Windows. Default screenshots, checkpoints and all 30 original chunks in each of two saves match the rebuilt reference. All 54 original files and eight bundled inputs remain unchanged. Packaged gameplay uses dummy backends and injected events; separate runtime checks pass with native Windows video. Manual hardware input, macOS and a complete playthrough remain unverified. See [`research/mental-owner-validation.json`](../research/mental-owner-validation.json). Development-alpha status is retained.

To run the desktop window checks from a native Windows build:

```powershell
$env:SDL_VIDEODRIVER = "windows"
$env:SDL_AUDIODRIVER = "dummy"
.\build\Release\fta2-runtime-test.exe
```

This test opens and closes its own windows and injects input. It does not establish hardware keyboard or audio-device behavior.
