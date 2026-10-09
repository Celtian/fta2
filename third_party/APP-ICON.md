# Application icon provenance

`assets/fta2.png` is the unmodified 512 x 512 transparent game icon from
[ScummVM Game Icons](https://github.com/scummvm/scummvm-icons), contributed by
GandalfTheWhite80 in commit
[`554453286389a9609bef999f894cf408817a8373`](https://github.com/scummvm/scummvm-icons/commit/554453286389a9609bef999f894cf408817a8373).
Walter Agazzi renamed it to `icons/saga2-fta2.png` in commit
[`ff8d0b0cba0baaab3c31ef6b7f2fb2e92fe86bca`](https://github.com/scummvm/scummvm-icons/commit/ff8d0b0cba0baaab3c31ef6b7f2fb2e92fe86bca).
The [pinned PNG source](https://raw.githubusercontent.com/scummvm/scummvm-icons/ff8d0b0cba0baaab3c31ef6b7f2fb2e92fe86bca/icons/saga2-fta2.png)
has SHA-256 `c9d42686a034c8d766a0f527943cbdfc3ce3ec6a4f945465ca49cd905089d0ab`.

This is a community-prepared icon using the game's logo and cover illustration.
It has not been established as the icon shipped with the original Windows
release. It was selected explicitly for this native application on 2026-10-09.
No graphics were copied from the local original installation into these assets.

The upstream repository supplies GNU GPL version 2; its licence text is retained
in `SCUMMVM-ICONS-GPL-2.0.txt`. This records the repository's supplied licence;
it does not relicense the original game, its artwork or trademarks under the
engine's GPL-3.0-or-later licence.

`assets/fta2.ico` was generated on 2026-10-09 with SDL 3.4.18 using
`tools/make_app_icon.cpp`. It contains PNG images at 16, 24, 32, 48, 64, 128 and
256 pixels, with alpha transparency and SDL linear scaling. Its SHA-256 is
`d954a5ae13c0577c9ac4aee3501a04c5f9b977f99130ebe64d7aa1c13a058b66`.
To reproduce with an installed SDL 3.4.18:

```sh
mkdir -p .cache/app-icon
c++ -std=c++20 tools/make_app_icon.cpp $(pkg-config --cflags --libs sdl3) -o .cache/app-icon/make-app-icon
.cache/app-icon/make-app-icon assets/fta2.png .cache/app-icon/generated
cmp assets/fta2.ico .cache/app-icon/generated/fta2.ico
```

CMake embeds the PNG in the engine for the SDL window icon and the ICO in the
Windows executable for Explorer and shortcuts. Both files and this notice are
included in runtime and source packages. Linux installations also receive a
desktop entry and the PNG in the hicolor icon theme. Existing macOS packages
remain command-line executables rather than Finder `.app` bundles.
