# hilbertviz

`hilbertviz` maps binary file bytes onto a 2D Hilbert curve and emits a colorized image (`.ppm` or `.png`).

## Features

- Linux-focused C11 implementation.
- Auto-sized Hilbert grid (`2^order x 2^order`) or manual `--order`.
- Optional `--offset` / `--length` slicing for large files.
- Optional pagination (`--paginate`) for very large inputs.
- Optional legend sidecar file with page and total byte-range stats.
- Output format dispatch by extension:
  - `.ppm` always supported.
  - `.png` supported when libpng is found at build time.
- Stairwell-inspired palette:
  - `0x00`: black.
  - `0x01-0x1F`: green (dark to bright).
  - `0x20-0x7E`: blue (dark to bright).
  - `0x7F-0xFF`: red (dark to bright).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Disable PNG support:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHILBERTVIZ_WITH_PNG=OFF
cmake --build build -j
```

With sanitizers:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DHILBERTVIZ_SANITIZERS=ON
cmake --build build -j
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

## Usage

```bash
./build/src/hilbertviz input.bin -o output.ppm
```

Manual order:

```bash
./build/src/hilbertviz input.bin -o output.ppm --order 8
```

Slice input:

```bash
./build/src/hilbertviz input.bin -o output.ppm --offset 4096 --length 65536
```

PNG output:

```bash
./build/src/hilbertviz input.bin -o output.png
```

Paginated rendering:

```bash
./build/src/hilbertviz input.bin -o output.ppm --order 10 --paginate
```

Legend sidecar:

```bash
./build/src/hilbertviz input.bin -o output.ppm --legend
./build/src/hilbertviz input.bin -o output.ppm --legend-path output.stats.txt
```

Help:

```bash
./build/src/hilbertviz --help
```

## Security and robustness notes

- File sizes, offsets, and products are range-checked before allocation/indexing.
- Reads are bounds-checked, streamed in fixed-size chunks, and detect short reads.
- Pixel index math is overflow-checked before writing.
- Invalid Hilbert orders and out-of-range indices fail with explicit errors.
- Destructive path aliasing is rejected across input/output/legend/page paths.
- Image allocation is capped by default (`256 MiB`) and fails fast when exceeded.
- Override cap with `HILBERTVIZ_MAX_IMAGE_BYTES=<bytes>` (`0` disables cap).
- Numeric CLI values are strict unsigned decimal (`+`/`-` forms are rejected).
- Slice validation uses `fstat` on the opened file descriptor to avoid path-race validation gaps.

## Output format

- PPM output is binary `P6`.
- PNG output uses libpng when available.
- For multi-page output, files are named with `_pageNNNN` suffixes.

## Fuzzing

Fuzz targets are included for AFL++ and libFuzzer:

- `fuzz_pipeline`: parser + render entry path.
- `fuzz_file_slice`: slice/stream file parsing path.

Build with libFuzzer:

```bash
CC=clang cmake -S . -B build-libfuzzer \
  -DCMAKE_BUILD_TYPE=Debug \
  -DHILBERTVIZ_FUZZ=ON \
  -DHILBERTVIZ_FUZZ_ENGINE=libfuzzer \
  -DHILBERTVIZ_FUZZ_SANITIZERS=ON \
  -DBUILD_TESTING=OFF
cmake --build build-libfuzzer -j
```

Build with AFL++:

```bash
CC=afl-clang-fast cmake -S . -B build-fuzz-afl \
  -DCMAKE_BUILD_TYPE=Release \
  -DHILBERTVIZ_FUZZ=ON \
  -DHILBERTVIZ_FUZZ_ENGINE=afl \
  -DBUILD_TESTING=OFF \
  -DHILBERTVIZ_SANITIZERS=OFF
cmake --build build-fuzz-afl -j
```

Start AFL++ instances in tmux (default: 6; configurable):

```bash
cmake --build build-fuzz-afl --target afl_tmux
tmux attach -t hilbert-afl
```

`afl_tmux` launches `start_afl_tmux.sh --no-attach` with `AUTO_SET_GOVERNOR=0`.

Use fewer panes for smaller screens (example: 4 instances):

```bash
JOBS=4 ./fuzz/scripts/start_afl_tmux.sh
```

One-command setup and launch:

```bash
./fuzz/scripts/start_afl_tmux.sh
```

`start_afl_tmux.sh` seeds AFL from libFuzzer corpus dirs when present
(`LIBFUZZER_CORPUS_DIRS`, default:
`/dev/shm/hilbert-libfuzzer-corpus:/tmp/hilbert-libfuzzer-corpus`) and from
`fuzz/corpus`.

If AFL startup breaks, see recovery commands in `fuzz/README.md`.

CPU governor helpers:

```bash
./fuzz/scripts/setPerformance.sh
./fuzz/scripts/restoreEnergy.sh
```

Stop AFL tmux fuzzing, cleanup work dirs, and restore energy governor:

```bash
./fuzz/scripts/cleanup_afl.sh
```

`cleanup_afl.sh` now asks whether to also remove libFuzzer shm dirs
(`hilbert-libfuzzer-corpus`, `hilbert-libfuzzer-artifacts`).
Non-interactive override:

```bash
CLEAN_LIBFUZZER_SHM=1 ./fuzz/scripts/cleanup_afl.sh   # remove them
CLEAN_LIBFUZZER_SHM=0 ./fuzz/scripts/cleanup_afl.sh   # keep them
```

Cleanup safety guardrails:

- `cleanup_afl.sh` rejects dangerous targets by default (`/`, `/tmp`, `/dev/shm`, home, non-canonical paths).
- It only permits expected project cleanup leaves under `/dev/shm` or `/tmp`.
- Unsafe override (only if intentional):

```bash
ALLOW_UNSAFE_CLEAN_PATHS=1 ./fuzz/scripts/cleanup_afl.sh
```

AFL run knobs (`start_afl_tmux.sh`):

- `JOBS`: number of AFL instances (default `6`, minimum `1`).
  - `JOBS=1` => `1` master, `0` slaves.
  - `JOBS=4` => `1` master, `3` slaves.
- `BUILD_JOBS`: build parallelism used when (re)building the AFL binary.
- `BUILD_DIR`: AFL build directory (default `build-fuzz-afl`).
- `TARGET`: AFL target binary path (default `build-fuzz-afl/fuzz/fuzz_pipeline_afl`).
- `CC_BIN`: AFL compiler wrapper used for auto-builds (default `afl-clang-fast`).
- `CMAKE_BUILD_TYPE`: build type for auto-builds (default `Release`).
- `SESSION`: tmux session name (default `hilbert-afl`).
- `LIBFUZZER_CORPUS_DIRS`: `:`-separated libFuzzer corpus import dirs (default `/dev/shm/hilbert-libfuzzer-corpus:/tmp/hilbert-libfuzzer-corpus`).
- `AFL_IN_DIR`: AFL input dir override (default `/dev/shm/hilbert-afl-in`, fallback `/tmp/hilbert-afl-in`).
- `AFL_OUT_DIR`: AFL output dir override (default `/dev/shm/hilbert-afl-out`, fallback `/tmp/hilbert-afl-out`).
- `AFL_TMPDIR`: AFL temp dir override (default `/dev/shm/hilbert-afl-tmp`, fallback `/tmp/hilbert-afl-tmp`).
- `AUTO_SET_GOVERNOR`: call `setPerformance.sh` before launch (`1` by default for direct script runs).
- `DRY_RUN`: print planned actions/commands without executing side effects.

Useful launcher forms:

```bash
./fuzz/scripts/start_afl_tmux.sh            # launch + attach
./fuzz/scripts/start_afl_tmux.sh --no-attach
TARGET=./build-fuzz-afl/fuzz/fuzz_file_slice_afl ./fuzz/scripts/start_afl_tmux.sh
DRY_RUN=1 ./fuzz/scripts/start_afl_tmux.sh --no-attach
```

RAM-disk runners (libFuzzer):

```bash
./fuzz/scripts/run_libfuzzer_ram.sh
./fuzz/scripts/run_libfuzzer_ram.sh ./build-libfuzzer/fuzz/fuzz_file_slice_libfuzzer
```

`run_libfuzzer_ram.sh` auto-builds the requested libFuzzer binary if it is missing.
`start_afl_tmux.sh` auto-builds known AFL targets if missing.

Monitor combined AFL++ progress summary (all fuzzers):

```bash
watch -n 2 "afl-whatsup -s /dev/shm/hilbert-afl-out || afl-whatsup -s /tmp/hilbert-afl-out"
```

See `fuzz/README.md` for details.

## Coverage Builds

Enable source coverage instrumentation:

```bash
CC=gcc cmake -S . -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DHILBERTVIZ_COVERAGE=ON
cmake --build build-coverage -j
ctest --test-dir build-coverage --output-on-failure
```

One-command coverage runner (separate build dir, safe with active AFL builds):

```bash
./fuzz/scripts/run_coverage.sh
```

`run_coverage.sh` only accepts `BUILD_DIR` names that start with `build-coverage`
and stay under this repository root (no absolute paths or `..` traversal).

Replay active AFL corpus into coverage (queue/crashes/hangs):

```bash
USE_AFL_CORPUS=1 ./fuzz/scripts/run_coverage.sh
```

Optional: override corpus directories or harness:

```bash
AFL_OUT_DIRS="/dev/shm/hilbert-afl-out:/tmp/hilbert-afl-out" \
COVERAGE_FUZZ_TARGET=fuzz_pipeline_afl \
USE_AFL_CORPUS=1 ./fuzz/scripts/run_coverage.sh
```

Generate HTML report (requires `lcov` + `genhtml`):

```bash
cmake --build build-coverage --target coverage_html
```

Coverage report path:

```bash
build-coverage/coverage/html/index.html
```

Notes:

- Coverage builds are intended for normal test binaries, not AFL compiler wrappers.
- If you use clang, ensure `libclang_rt.profile.a` is available.
