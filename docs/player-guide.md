# Player and developer guide

A standalone C++20 engine based on SAGA2, serving as the foundation for a future Definitive Edition. It runs the original DOS game data directly, without DOSBox or a ScummVM installation. The code is licensed under GPL-3.0-or-later.

**Status: development alpha.** Startup, controls, audio generation, new saves and loading an original save have been verified on Linux and the actual Windows host. Two captured scenes and all 30 world-state chunks match the reference. A full playthrough and macOS validation are still pending; see [validation status](validation.md).

## Build and run

You need CMake 3.24+, Python 3.10+, a C++20 compiler and your platform's graphics/audio development libraries. SDL **3.4.18** is downloaded automatically from a checksum-verified release archive. To use an installed copy of that exact version, set `CMAKE_PREFIX_PATH`; for an offline build, also pass `-DFTA2_FETCH_SDL=OFF`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
./build/fta2-ce --data-dir ./original-game
```

On Windows, the default generator places the executable at `build/Release/fta2-ce.exe`. On macOS, pass `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` to build a universal binary. [CI](../.github/workflows/build.yml) is configured for Linux x64, Windows x64 and both macOS architectures.

Player ZIPs share root launchers and `bin/`, `licenses/` and `source/`; Linux/macOS also use `lib/` for SDL. Linux starts with `Faery Tale Adventure II`, macOS with `Faery Tale Adventure II.command`, and Windows with `Faery Tale Adventure II.exe`. Developer files stay in the embedded source ZIP.

The single **complete Windows x64 ZIP** includes `bin/fta2-ce.exe`, `bin/SDL3.dll`, the required files under `data/`, original English voices under `media/`, and corresponding engine sources under `source/`. Extract the entire ZIP and double-click `Faery Tale Adventure II.exe` in its root (or run `bin/fta2-ce.exe` directly). No folder selection or separate original installation is needed. Keep the extracted directory together when moving the game. Startup errors appear in a visible dialog. Licence notices are under `licenses/`; developer documentation, research records and build instructions live inside the matching `source/` ZIP rather than the player folders.

Public CI produces one engine ZIP per platform with the corresponding source ZIP inside; those packages require separately supplied game data. They retain the Windows folder picker. The optional `--data-dir` argument selects an explicit installation for either package. See [Windows build and validation](windows-build.md).

The application uses the [ScummVM community game icon](../third_party/APP-ICON.md), with its logo and cover illustration. It is embedded in the Windows executable and used for the SDL window; loose icon files are retained in the corresponding sources, and is embedded in the Windows root launcher.

```sh
fta2-ce --data-dir /path/to/game --save-dir /path/to/new/saves
fta2-ce --help
fta2-ce --version
```

Without `--save-dir`, the application uses the user directory returned by SDL. It contains `config.ini`, saves named `fta2.000`–`fta2.999`, and `resources/` with automatically extracted resources and their manifest. Output directories must be outside the game data directory. Slot 999 holds the restart state created by the engine.

The current profile uses the `fta2-ce` identifier. On first launch, existing settings and numbered saves are copied from the previous profile without replacing newer files or removing the old profile. Explicit `--save-dir` paths keep their existing behavior.

Each run writes a timestamped, flushed diagnostic log under `logs/` in that same user/save directory. On Windows the default is `%APPDATA%\fta2-ce\fta2-ce\logs`; `--save-dir` moves logs along with the saves. Logs include startup paths, warnings, input actions, save/load operations, voice requests and periodic world position. A fatal record includes the last engine phase and location. Windows also attempts to write a same-name `.dmp` beside the log for an unhandled crash. To report a crash, provide the latest `.log` and its `.dmp`, if present; nothing is uploaded automatically. Startup errors display the actual log path. Routine logging stops at 8 MiB per session, while fatal records remain enabled. Startup retains the latest ten session logs and three older dumps. Crash capture is best effort; see [diagnostics validation](validation.md#persistent-diagnostics-and-villager-death-0134).

The game starts in borderless fullscreen, covering the entire monitor without a title bar or the Windows taskbar. **Alt+Enter** toggles between fullscreen and a resizable window, using a default client size of 640 × 480 or 854 × 480 for the active display mode. Until you resize or maximize the window, changing the mode updates this default immediately in windowed mode or at the next Alt+Enter from fullscreen. A manually resized or maximized window keeps its size and position during subsequent mode changes and fullscreen transitions for the rest of that session; either Enter key and either Alt key work. The shortcut is handled by the platform and does not activate game dialogs. The default 640 × 480 image uses nearest-neighbour scaling and retains its 4:3 aspect ratio. In Options, enable **Widescreen (16:9)** to widen the game view without stretching the graphics. The change takes effect immediately while Options stays open and is remembered across restarts. The wide framebuffer is 854 × 480 (16:9 rounded to whole pixels): the map grows from 440 × 420 to 654 × 420, while the 180-pixel sidebar moves right at its original size. Dialogs and documents keep their size and center horizontally on the wider screen. Open inventory windows remain open; returning to 4:3 moves any window that would otherwise fall outside the screen back inside.

Options applies every preference immediately: volumes, automatic aggression, automatic weapon use, subtitles, Night, aspect ratio, sidebar layout and the game clock. Language changes when Apply is confirmed in its dialog, while Options remains open. Preferences are saved when Options closes.

Exploration is shared by all three brothers within each world. The original reveal radius remains two metatiles around the selected brother, excluding the four corners, in both display modes. A wider viewport does not increase the exploration or simulation activation radius.

Enable **New layout** in Options for a regular two-column, three-row grid beside each portrait, in both individual and group view. All six controls, including HP with its original frame, fit 28 × 27 pixels with five-pixel gaps. Clicking a group's Ideas/Spells/Skills control selects that brother and opens the chosen category while keeping group view. In the individual panel, their former bottom-row space shows the original coin artwork and that brother's exact Gold Coin total in a round gold medallion with a bronze interior and light gold digits. Gold, weight and volume share a continuous strip of original gold decoration, with evenly spaced medallions. This includes coins in closed and nested carried bags. The coin icon remains visible at zero. Hover over the medallion to see the exact amount in its Gold tooltip; it updates even while the pointer stays still. The counter updates after inventory changes and switching brothers. The option defaults to off, applies immediately in open Options and is remembered across restarts in both display modes. Jewellery and other gold-named items are not counted as currency.

Ideas/Spells/Skills windows open over the right sidebar in both display modes, keeping their original size and vertical position. Remembered custom positions are retained when reopening. A red J, blue P or yellow K diamond in the head illustration identifies whose abilities are displayed. Hover shows the name, such as "Julian's abilities". This marker is always shown with either sidebar layout. Switching brothers retains the original automatic hiding and reopening of their windows.

Physical inventory windows identify their current carrier with a large gold-rimmed diamond below the container icon, marked J, P or K: red for Julian, blue for Phillip and yellow for Kevin. Hover over it for the full name, such as "Carried by Julian". This works with either sidebar layout and both display modes, including bags inside other bags. The marker follows transfers between brothers and disappears for ground containers and containers carried by other characters. It identifies the carrier, rather than story ownership of a chest, and has no click action.

The original controls are preserved: use the mouse, `1`/`2`/`3` (top row or numeric keypad) to switch brothers, `O` to open options including save/load, Escape to close the frontmost container and clear its hover text (or open Options when none is open), and Tab to switch between group and individual control.

Click the small **i** at the right end of the status bar for the Community Edition name, Dominik Hladík (2026), the build version and development-alpha status. Enter, Escape and Close dismiss it.

Choose **Hot Keys** in Options (or press **H** while Options is open) for the main keyboard shortcuts. **Print Screen** saves the last presented game image as a lossless PNG, including the cursor and tooltip, under `screenshots/` in the package root (beside the executable for development builds). If that location is not writable, it uses `screenshots/` in the user/save directory and reports the fallback. Screenshot names include local date/time and a collision suffix; existing files are never overwritten. Screenshots retain the original 640 × 480 or 854 × 480 pixels without window borders or letterboxing, and work in menus and documents too. Holding the key does not save repeated images.

The overview uses evenly spaced rows and a centered **Close** button inside its frame. Close it with **Close** or Escape to return to Options. **G** uses the selected brother's carried map, including maps in closed or nested bags; Shift+G also works. While the map is open, arrow keys pan its viewport in screen directions; holding a key repeats the movement. Scrolling stops at the original map bounds. Home, End, Page Up and Page Down retain their original diagonal movement. Escape or the original map button closes it. If that brother has no map, the status line says "No map carried by the selected brother." Gameplay shortcuts are ignored in modal dialogs, and G is ignored while holding or dragging an item. **M** toggles music during gameplay, preserving its configured volume; the mute setting is remembered.

In Options, **Night** is the last checkbox in the left column, below Speech Text. **Speech Volume** is remembered and applies immediately to active and newly started speech channels. The base DOS installation has no usable voices; the optional verified English CD voice supplement enables original dialogue playback. Missing, muted or zero-volume voices retain readable dialogue text, including choices. Music and sound effects have separate volume controls.

Load shows a harmless message when no player saves exist. Choose Save in Options to create a named game, then select it in Load. Escape or Cancel returns to Options without restarting. Speech Text is an interactive preference remembered across restarts. Unchecking it suppresses text only when usable voice audio is available; unvoiced dialogue and its choices remain readable. Credits displays the original game credits; use the page arrows or click either page to browse, and Escape or the red bookmark to return. The cursor points left or right over the corresponding half of the book and returns to the normal pointer when Credits closes.

Mouse-wheel up/down turns pages in scrolls and parchment while the pointer is over the document, excluding its close button. One notch turns one page; fractional wheel or trackpad input accumulates within the open document. Version 0.1.44 fixes overlapping page text in widescreen by restoring the document background before each page redraw.

Mouse-wheel up/down also scrolls physical containers such as chests, sacks and purses, and Ideas/Spells/Skills windows, while the pointer is over their inventory grid. One notch moves one row within the original capacity. Fractional input accumulates separately per window and resets when leaving the grid. Covered windows and containers behind modal dialogs do not receive the wheel. Scrolling is ignored during button presses, dragging and quantity selection; holding an item without pressing a button still allows scrolling. Empty slots in these windows show small, muted warm-gray absolute slot numbers with a thin dark outline in the original font, starting at 1. Mental windows retain their original 20 rows of four slots (1–80), with four rows visible. Changing category returns to the first row and clears fractional wheel input; selecting the same category retains the position. Closing or loading starts at the first row. Death inventories and fixed sidebar slots retain their existing behavior.

Giving a physical item to Julian, Phillip or Kevin, through his portrait or the map, first uses the original main-inventory merging, stacking and free slots. If none of the nine slots can accept it, the game tries his unlocked carried containers in visible slot order, first all direct containers and then each deeper level. Closed containers are eligible; locked containers and their contents are excluded. Each container tries compatible merging or stacking before a free slot. Existing capacity and script restrictions remain in force. If no container accepts the item, it is placed beside the brother as before. This automatic storage works with either display mode and sidebar layout, without another setting.

Physical containers also support rearrangement with the original two-click controls: pick up an item, then click another item in the same container to exchange their slots. Compatible piles still merge or stack, and dropping onto a nested bag still inserts into that bag, including the original refusal when insertion is not allowed. Cross-container transfers and the use cursor retain their original actions. A complete incompatible destination stack moves together; partial merged piles and a source slot that still contains other items are not exchanged. Wheel scrolling while holding an item lets you exchange slots on distant rows. Existing saves retain the resulting positions. This works in either display mode and sidebar layout, without adding held-button dragging to physical containers.


In **Spells**, hold the left mouse button and drag a spell to arrange favourites near the top. Dropping onto an occupied slot swaps the two spells. Wheel scrolling remains available over that same grid during a drag, including between the first and last rows. Dropping outside its grid cancels the move. Ordinary click selection, double-click use and clicking a target in the map retain their original behavior. Each brother's order is saved with the game, in either display mode and sidebar layout. Ideas and Skills retain their existing interaction.

Enable **Show game time** in Options to display **Day 1 05:30** at the upper-left of the main map. It uses the original saved game calendar, a 24-hour clock and a day count starting at 1. Pausing stops the clock, and loading restores the saved time. The display is passive, sits beneath open windows and works with either aspect ratio or sidebar layout. This option defaults to off, applies immediately in open Options and is remembered in `config.ini`.

## Language

Options now includes **Language...**, opening a separate English/Čeština/Español/Français radio dialog with Apply and Cancel. Apply changes the language immediately, including the open Options controls, without restarting, and is remembered as `language=en|cs|es|fr` in `config.ini`; English remains the default. Switching to any translated language enables subtitles. You can then turn subtitles off; usable voices remain English, and missing voices retain readable dialogue. Already assembled speech finishes in its captured language, including after save/load. Loading a save does not change the selected language. Czech uses Julián, Filip and Kevin with J/F/K inventory markers.

**Czech, Spanish and French story translations include drafts.** Interface text, terminology, fonts, switching and save support are implemented, but thousands of story/catalog entries still require linguistic review. Review status is recorded in the documentation and enforced by the completeness audit. The language dialog has compact radio choices without a draft notice. Common item labels start lowercase; proper names retain capitals. Credits roles and headings are translated while author names and licensing notices remain original. See the [catalog workflow and remaining work](../localization/README.md) and [validation evidence](validation.md#czech-localization-work-in-progress). Original game files remain unchanged.

Version 0.1.48 adds Spanish and French interface/game catalogs, Western bitmap diacritics, explicit counted-item forms and captured speech save support. Spanish/French story drafts were generated locally and still need linguistic review. The original English brothers' names and J/P/K markers remain in these languages.

Version 0.1.45 fixes all six mana hover labels to use their existing Czech translations while retaining the current/base values and original English text.

## Game data

Complete GitHub release ZIPs include the eight verified data files and English CD
voices. Extract the entire ZIP and open its root launcher: no folder selection or
separate original installation is needed. Engine-only CI artifacts and source
builds still require a supported data installation as described below.

The supported English DOS profile requires `FTA2.EXE`, `FTA.HRS`, `FTADATA.HRS`, `FTAIMAGE.HRS`, `FTASOUND.HRS`, `SCRIPTS.HRS`, and the instrument banks `DRIVERS/SAMPLE.AD` and `DRIVERS/SAMPLE.OPL`. The executable, main archives and banks are checked against SHA-256 hashes. Unknown executables are rejected; Windows executable offsets are never applied to the DOS executable.

This installation's `FTAVOICE.HRS` is a copy of `FTADATA.HRS` and is ignored. Version 0.1.32 can use the English full CD's verified `FTAVOICE.HRS` as an optional supplement: 268,574,344 bytes, SHA-256 `34bd5c7e3b5982df3f84b274c05a0f056a2932dfd03f02d880be44e5a81353c3`, containing 4,320 original recordings (about 4 hours 34 minutes). The CD's five main HRES archives match the supported DOS data byte for byte; its Windows executable is not used. Missing videos remain skipped.

From version 0.1.33, the local complete Windows ZIP includes the verified English voices. Extract this one ZIP and start the game normally; no voice add-on installation is needed. Alternatively, pass `--voice-file /path/to/FTAVOICE.HRS`, or supply the verified archive in a separate supported game-data copy. The existing `original-game/` installation stays untouched. The engine validates the supplement's size, HRES bounds and SHA-256 before enabling it. An invalid explicit `--voice-file` reports an error; an unusable automatically discovered optional archive is ignored with text fallback. Old releases' forced speech mute is cleared at startup. No voice data is downloaded automatically. Original and bundled game data are opened read-only. Source and engine-only packages exclude game data; the explicitly requested local complete package includes the eight required files under `data/` and the verified voice archive under `media/`. The original `FTA2.EXE` supplies embedded resources and is never executed. Game data retains its original copyright and is not relicensed under the engine's GPL.

You can **copy** the original `999.SAV` to the user save directory, for example as `fta2.002`, and load it through the game menu. New saves preserve the original chunks and add the random generator state; see [architecture](architecture.md).

## Validation and packaging

```sh
ctest --test-dir build -C Release --output-on-failure
python3 tools/fta2_data.py verify
python3 tools/fta2_data.py inventory
python3 tools/fta2_data.py extract --output .cache/extracted
python3 tools/integration_test.py --binary build/fta2-ce --output .cache/integration
python3 tools/integration_widescreen.py --binary build/fta2-ce --output .cache/integration-widescreen
cpack --config build/CPackConfig.cmake -C Release
```

For a local complete Windows release, create the internal engine and source archives under `.cache/`, then publish just one ZIP in `dist/`:

```sh
cpack --config build-windows-x64/CPackConfig.cmake -C Release -B .cache/release-0.0.1
cpack --config build-windows-x64/CPackSourceConfig.cmake -B .cache/release-0.0.1
python3 tools/package_complete.py --engine-package .cache/release-0.0.1/fta2-ce-0.0.1-Windows-x86_64.zip --source-package .cache/release-0.0.1/fta2-ce-0.0.1-source.zip --data-dir original-game --voice-file .cache/fta2-cd-voices/FTAVOICE.HRS --output dist/fta2-ce-0.0.1-Windows-x86_64.zip
```

The tool verifies all eight data files and the optional voice archive against known size and SHA-256 values, embeds the matching data-free source ZIP, and records their hashes in the package manifest. Original saves and unnecessary DOS files are excluded. It refuses unknown data, output inside the original installation and overwriting an existing ZIP. Earlier releases remain available. Original game data and voices retain their copyright; the engine's GPL does not relicense them.

Desktop CI uses `tools/package_release.py` to embed matching sources into each platform's engine ZIP, without proprietary data. After all builds pass, the release publication job downloads a separately pinned, verified input and uses `tools/release.py complete` to add the eight required data files and English voices to every platform ZIP. The complete packages are audited before publication. See [release setup](releases.md). CPack's engine/source archives are build intermediates, rather than separate user downloads. The legacy voice-add-on tool remains available for older releases.

The integration test requires original game data and a fresh output directory. Public CI tests use synthetic data only. Runtime packages include the engine, SDL and licences. For more information, see [DOS resources](dos-resources.md), the [development reference](../reference/README.md), [code provenance](../third_party/README.md) and the [remaining acceptance checks](validation.md).

Repository conventions for coding agents are documented in [AGENTS.md](../AGENTS.md). All project documentation, code comments and tool messages should be written in English.
