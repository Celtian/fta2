# Separate development reference

ScummVM/SAGA2 at revision `1a3e816afd5ddc41cc2a5c51a5da292e6d61758c` is used for comparison only. The product does not install, launch or link its framework.

```sh
python3 tools/reference.py build
python3 tools/reference.py run
python3 tools/integration_test.py --binary build/fta2-native --output .cache/integration
python3 tools/compare_reference.py --reference-output .cache/reference/output --reference-saves .cache/reference/saves --native-output .cache/integration/output --native-saves .cache/integration/saves
```

Building the reference requires Git, Python 3, a POSIX shell, make and a C++ compiler. The first step downloads the public source code into `.cache/reference/source` without changing the main Git checkout. `--work-dir` can point outside the repository.

`dos-data.patch` adds this installation's data profile, the validated F2R1 pack and support for the missing voice archive. `capture-probe.patch` gives the null backend a framebuffer and timer, controlled time, image capture and the same input sequence. `deterministic-motion.patch` initializes unused archived motion-task fields; without it, the original SAGA2 wrote uninitialized bytes to saves.

The reference and native engine both use seed 42. The test moves Julian, selects Phillip and saves the world. It compares two complete screens, four state checkpoints (actor, coordinates and RNG), and all 30 original chunks in each new save. ScummVM metadata (`SCVM`) and native metadata (`NRNG`) are excluded from the original-chunk comparison. Test images and original game data are excluded from the source package.
