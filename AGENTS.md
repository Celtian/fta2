# Repository instructions

## Purpose and current scope

Build a standalone native Faery Tale Adventure II engine for Windows x64, Linux x64 and macOS arm64/x64, using the original DOS game data. The implementation uses C++20, CMake and SDL 3.4.18, with gameplay algorithms imported from SAGA2 at ScummVM revision `1a3e816afd5ddc41cc2a5c51a5da292e6d61758c`.

The product must run directly without DOSBox, a ScummVM installation or the ScummVM framework. Selected portable gameplay and audio code may remain. Preserve original behaviour first; Definitive Edition features and script authoring tools are later work. Read `README.md`, `docs/architecture.md` and `docs/validation.md` before substantial changes.

## Language

Use English for all project documentation, code comments, identifiers, tool output, user-facing messages and commit/PR descriptions. Preserve upstream author names, copyright notices and original game text. Conversation with the user may follow their preferred language.

## Original data and generated outputs

- Treat `original-game/` as immutable input. Never modify, rename, delete or write files there, including the original `999.SAV`.
- Put configuration, new saves, extracted resources and test output outside the original installation. Use `.cache/` for local development output or an external temporary directory; normal gameplay uses the SDL user directory unless `--save-dir` is supplied.
- Copy original saves before testing them. Verify the original installation with `python3 tools/fta2_data.py verify` after changes involving data access or extraction.
- Keep original game data, extracted graphics, screenshots and saves out of source and engine-only runtime packages. The user explicitly requests complete GitHub Releases for all desktop platforms, including only the eight verified input files selected by `tools/package_complete.py` under `data/`, the separately verified English CD voice archive under `media/`, and the matching data-free source ZIP under `source/`. Do not include original saves or other DOS components. Release assembly obtains the separately pinned input from the repository's maintainer draft; branch/pull-request CI and source archives remain data-free. Do not download replacement game data as part of development unless the user explicitly requests that download. Original game data and audio retain their original copyright.
- The explicitly requested README preview at `assets/gameplay.png` is the sole gameplay screenshot permitted in source packages. Preserve its original-artwork copyright notice; do not install it as a loose runtime asset or add other private captures.
- Reject unknown executable profiles with a clear error. Never apply Windows executable offsets to the DOS executable. Record recovered offsets, source hashes and extraction provenance.
- This DOS profile has no usable voice archive or videos. Optional English CD voices must be validated separately without changing the DOS executable profile. Keep unvoiced or silent dialogue readable and ensure missing media cannot block progress.

## Source layout and implementation

- `src/saga2/`: imported simulation, script interpreter, original UI and software renderer. Keep gameplay changes focused and compare behaviour against the development reference.
- `src/platform/`: native resources, streams, configuration, saves, timing, SDL presentation/input and mixing. Prefer the C++ standard library and small explicit interfaces; do not reintroduce the ScummVM framework.
- `src/audio/`: selected portable XMIDI/Miles algorithms and Nuked OPL3 synthesis. Preserve sample-driven timing and thread-safe ownership between the game and audio callback.
- `tools/`: data inspection, extraction, reference builds and integration comparison. Python tools use the standard library.
- `reference/`: patches and instructions for the separate ScummVM reference. Never link or package that development build as the product.
- `research/`: original-file hashes, archive summaries and recorded validation results. Update evidence only after running the corresponding checks.
- `third_party/`: licences and source provenance for imported code.
- `tests/` and `.github/workflows/`: synthetic public tests and desktop build/package automation.

Preserve 640 × 480 rendering and 4:3 presentation as the default compatible mode. The optional Widescreen (16:9) setting uses 854 × 480 rendering, expands only the main map horizontally and retains the original sidebar size. Preserve nearest-neighbour scaling, correct mouse mapping, original simulation timing and deterministic RNG behaviour in both modes. Keep native RNG save metadata optional so original saves remain loadable. Keep resource loading, simulation and presentation boundaries suitable for later DE work.

Use `.clang-format` for new native C++ code. Follow the surrounding style in imported files and avoid unrelated reformatting. Keep pinned dependency versions and checksums unless an upgrade is explicitly part of the task. Preserve imported licence headers and record provenance changes in `third_party/`.

## Build and validation

Standard build and public checks:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

Use `CMAKE_PREFIX_PATH` or `SDL3_DIR` for an installed SDL 3.4.18. Set `-DFTA2_FETCH_SDL=OFF` when an offline build is required. Windows builds with a multi-configuration generator place executables under `build/Release/`.

For changes affecting gameplay, resources, saves, rendering or audio, run the relevant original-data integration checks. Choose a fresh output directory for every integration run:

```sh
python3 tools/fta2_data.py verify
python3 tools/integration_test.py --binary build/fta2-ce --output .cache/integration
```

For changes that can affect parity, build/run the separate reference and compare images, simulation checkpoints and all original save chunks:

```sh
python3 tools/reference.py build
python3 tools/reference.py run
python3 tools/compare_reference.py --reference-output .cache/reference/output --reference-saves .cache/reference/saves --engine-output .cache/integration/output --engine-saves .cache/integration/saves --allow-about-icon
```

See `docs/validation.md` for stability and sanitizer commands. Use AddressSanitizer/LeakSanitizer for changes to task lifetimes, pathfinding, save restoration or resource ownership. Public CI must not require proprietary game data. Documentation-only changes need link/content checks rather than a new gameplay test run.

## Licensing, packaging and reporting

The engine is GPL-3.0-or-later; Nuked OPL3 retains its LGPL-2.1-or-later notices and SDL its zlib licence. Original game data is not relicensed. Keep all required notices, licences and source provenance available in distributions.

Engine-only runtime packages are generated with `cpack --config build/CPackConfig.cmake -C Release`; corresponding source packages use `build/CPackSourceConfig.cmake`. Complete desktop packages are produced by `tools/package_complete.py` from engine ZIPs and verified data; `tools/release.py complete` assembles all three platforms during publication. Desktop CI continues to build and test without proprietary data. Publish one complete runtime ZIP per platform, with the matching source ZIP embedded under `source/`; keep CPack intermediates in `.cache/`. Check that complete packages contain exactly the eight allowlisted files under `data/` and only the verified voice archive under `media/`, and that source and engine-only packages contain no original data or development outputs. Preserve older release packages and archive replaced engine-only assets before correcting an existing release.

Report what was changed, which checks actually ran and any remaining limitations. Keep development-alpha status until the documented acceptance checks are complete. A configured CI workflow is not evidence that Windows/macOS packages passed; an automated startup scenario is not evidence of a full playthrough.
