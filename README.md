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

The game starts in borderless fullscreen, covering the entire monitor without a title bar or the Windows taskbar. **Alt+Enter** toggles between fullscreen and a resizable window, using a default client size of 640 × 480 or 854 × 480 for the active display mode. Until you resize or maximize the window, changing the mode updates this default immediately in windowed mode or at the next Alt+Enter from fullscreen. A manually resized or maximized window keeps its size and position during subsequent mode changes and fullscreen transitions for the rest of that session; either Enter key and either Alt key work. The shortcut is handled by the platform and does not activate game dialogs. The default 640 × 480 image uses nearest-neighbour scaling and retains its 4:3 aspect ratio. In Options, enable **Widescreen (16:9)** to widen the game view without stretching the graphics. The change takes effect after Options closes and is remembered across restarts. The wide framebuffer is 854 × 480 (16:9 rounded to whole pixels): the map grows from 440 × 420 to 654 × 420, while the 180-pixel sidebar moves right at its original size. Dialogs and documents keep their size and center horizontally on the wider screen. Open inventory windows remain open; returning to 4:3 moves any window that would otherwise fall outside the screen back inside.

Exploration is shared by all three brothers within each world. The original reveal radius remains two metatiles around the selected brother, excluding the four corners, in both display modes. A wider viewport does not increase the exploration or simulation activation radius.

Enable **New layout** in Options for a regular two-column, three-row grid beside each portrait, in both individual and group view. All six controls, including HP with its original frame, fit 28 × 27 pixels with five-pixel gaps. Clicking a group's Ideas/Spells/Skills control selects that brother and opens the chosen category while keeping group view. In the individual panel, their former bottom-row space shows the original coin artwork and that brother's exact Gold Coin total in a round gold medallion with a bronze interior and light gold digits. Gold, weight and volume share a continuous strip of original gold decoration, with evenly spaced medallions. This includes coins in closed and nested carried bags. The coin icon remains visible at zero. Hover over the medallion to see the exact amount in its Gold tooltip; it updates even while the pointer stays still. The counter updates after inventory changes and switching brothers. The option defaults to off, applies after Options closes and is remembered across restarts in both display modes. Jewellery and other gold-named items are not counted as currency.

Ideas/Spells/Skills windows open over the right sidebar in both display modes, keeping their original size and vertical position. Remembered custom positions are retained when reopening. A red J, blue P or yellow K diamond in the head illustration identifies whose abilities are displayed. Hover shows the name, such as "Julian's abilities". This marker is always shown with either sidebar layout. Switching brothers retains the original automatic hiding and reopening of their windows.

Physical inventory windows identify their current carrier with a large gold-rimmed diamond below the container icon, marked J, P or K: red for Julian, blue for Phillip and yellow for Kevin. Hover over it for the full name, such as "Carried by Julian". This works with either sidebar layout and both display modes, including bags inside other bags. The marker follows transfers between brothers and disappears for ground containers and containers carried by other characters. It identifies the carrier, rather than story ownership of a chest, and has no click action.

The original controls are preserved: use the mouse, `1`/`2`/`3` (top row or numeric keypad) to switch brothers, `O` to open options including save/load, Escape to close the frontmost container and clear its hover text (or open Options when none is open), and Tab to switch between group and individual control.

Choose **Hot Keys** in Options (or press **H** while Options is open) for the main keyboard shortcuts. Close the overview with **Close** or Escape to return to Options. **G** uses the selected brother's carried map, including maps in closed or nested bags; Shift+G also works. Escape or the original map button closes it. If that brother has no map, the status line says "No map carried by the selected brother." Gameplay shortcuts are ignored in modal dialogs, and G is ignored while holding or dragging an item. **M** toggles music during gameplay, preserving its configured volume; the mute setting is remembered.

Load shows a harmless message when no player saves exist. Choose Save in Options to create a named game, then select it in Load. Escape or Cancel returns to Options without restarting. Speech Text is an interactive preference remembered across restarts. Unchecking it suppresses text only when usable voice audio is available; unvoiced dialogue and its choices remain readable. Credits displays the original game credits; use the page arrows or click either page to browse, and Escape or the red bookmark to return. The cursor points left or right over the corresponding half of the book and returns to the normal pointer when Credits closes.

Mouse-wheel up/down turns pages in scrolls and parchment while the pointer is over the document, excluding its close button. One notch turns one page; fractional wheel or trackpad input accumulates within the open document.

## Game data

The supported English DOS profile requires `FTA2.EXE`, `FTA.HRS`, `FTADATA.HRS`, `FTAIMAGE.HRS`, `FTASOUND.HRS`, `SCRIPTS.HRS`, and the instrument banks `DRIVERS/SAMPLE.AD` and `DRIVERS/SAMPLE.OPL`. The executable, main archives and banks are checked against SHA-256 hashes. Unknown executables are rejected; Windows executable offsets are never applied to the DOS executable.

This installation's `FTAVOICE.HRS` is a copy of `FTADATA.HRS`. The engine ignores it, preserves the original unvoiced-text fallback and skips missing videos. Original and bundled game data are opened read-only. Source and engine-only packages exclude game data; the explicitly requested local complete package includes only the eight required files. The original `FTA2.EXE` supplies embedded resources and is never executed. Game data retains its original copyright and is not relicensed under the engine's GPL.

You can **copy** the original `999.SAV` to the user save directory, for example as `fta2.002`, and load it through the game menu. New saves preserve the original chunks and add the random generator state; see [architecture](docs/architecture.md).

## Validation and packaging

```sh
ctest --test-dir build -C Release --output-on-failure
python3 tools/fta2_data.py verify
python3 tools/fta2_data.py inventory
python3 tools/fta2_data.py extract --output .cache/extracted
python3 tools/integration_test.py --binary build/fta2-native --output .cache/integration
python3 tools/integration_widescreen.py --binary build/fta2-native --output .cache/integration-widescreen
cpack --config build/CPackConfig.cmake -C Release
```

To create the complete Windows ZIP locally after packaging the Windows engine:

```sh
python3 tools/package_complete.py --engine-package dist/fta2-native-0.1.27-Windows-x86_64.zip --data-dir original-game --output dist/fta2-native-0.1.27-Windows-x86_64-complete.zip
```

The tool checks all eight data files against their known SHA-256 hashes, excludes original saves and unnecessary DOS files, and records a package manifest. It refuses unknown data, output inside the original installation, and overwriting an existing output ZIP.

The integration test requires original game data and a fresh output directory. Public CI tests use synthetic data only. Runtime packages include the engine, SDL and licences. For more information, see [DOS resources](docs/dos-resources.md), the [development reference](reference/README.md), [code provenance](third_party/README.md) and the [remaining acceptance checks](docs/validation.md).

Repository conventions for coding agents are documented in [AGENTS.md](AGENTS.md). All project documentation, code comments and tool messages should be written in English.
