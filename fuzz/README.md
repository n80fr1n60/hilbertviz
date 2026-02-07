# Fuzzing

This directory contains fuzz targets for:

- `fuzz_pipeline`: file parsing + slice handling + render pipeline entry.
- `fuzz_file_slice`: file slice validation + stream read APIs.

## Build (libFuzzer)

```bash
CC=clang cmake -S . -B build-libfuzzer \
  -DCMAKE_BUILD_TYPE=Debug \
  -DHILBERTVIZ_FUZZ=ON \
  -DHILBERTVIZ_FUZZ_ENGINE=libfuzzer \
  -DHILBERTVIZ_FUZZ_SANITIZERS=ON \
  -DBUILD_TESTING=OFF
cmake --build build-libfuzzer -j
```

If your clang toolchain lacks ASan/UBSan runtime libraries, switch to:

```bash
-DHILBERTVIZ_FUZZ_SANITIZERS=OFF
```

Targets:

- `build-libfuzzer/fuzz/fuzz_pipeline_libfuzzer`
- `build-libfuzzer/fuzz/fuzz_file_slice_libfuzzer`

Run with RAM-disk corpus/artifacts:

```bash
./fuzz/scripts/run_libfuzzer_ram.sh
./fuzz/scripts/run_libfuzzer_ram.sh ./build-libfuzzer/fuzz/fuzz_file_slice_libfuzzer
```

`run_libfuzzer_ram.sh` auto-builds a missing target (`fuzz_pipeline_libfuzzer` or
`fuzz_file_slice_libfuzzer`) using:

- `BUILD_DIR` (default: `build-libfuzzer`)
- `CC_BIN` (default: `clang`)
- `JOBS` (default: CPU count)
- `CMAKE_BUILD_TYPE` (default: `Debug`)
- `HILBERTVIZ_FUZZ_SANITIZERS` (default: `ON`)

## Build (AFL++)

```bash
CC=afl-clang-fast cmake -S . -B build-fuzz-afl \
  -DCMAKE_BUILD_TYPE=Release \
  -DHILBERTVIZ_FUZZ=ON \
  -DHILBERTVIZ_FUZZ_ENGINE=afl \
  -DBUILD_TESTING=OFF \
  -DHILBERTVIZ_SANITIZERS=OFF
cmake --build build-fuzz-afl -j
```

Targets:

- `build-fuzz-afl/fuzz/fuzz_pipeline_afl`
- `build-fuzz-afl/fuzz/fuzz_file_slice_afl`

Launch AFL++ in tmux (default: 6 instances, 1 master + 5 slaves):

```bash
cmake --build build-fuzz-afl --target afl_tmux
tmux attach -t hilbert-afl
```

`afl_tmux` launches `start_afl_tmux.sh --no-attach` with `AUTO_SET_GOVERNOR=0`.

Use fewer panes for smaller screens (example: 4 instances):

```bash
JOBS=4 ./fuzz/scripts/start_afl_tmux.sh
```

One-command startup (configure + build + launch + attach):

```bash
./fuzz/scripts/start_afl_tmux.sh
```

`start_afl_tmux.sh` auto-builds a missing known AFL target
(`fuzz_pipeline_afl` or `fuzz_file_slice_afl`).

By default, AFL input seeds are merged from:

- `/dev/shm/hilbert-libfuzzer-corpus`
- `/tmp/hilbert-libfuzzer-corpus`
- `fuzz/corpus`

Override libFuzzer seed locations with:

```bash
LIBFUZZER_CORPUS_DIRS="/path/a:/path/b" ./fuzz/scripts/start_afl_tmux.sh
```

Troubleshooting startup issues:

If you see errors like `Unable to open .../_resume`, do a clean restart:

```bash
tmux kill-session -t hilbert-afl 2>/dev/null || true
rm -rf /dev/shm/hilbert-afl-out /dev/shm/hilbert-afl-in /dev/shm/hilbert-afl-tmp
./fuzz/scripts/start_afl_tmux.sh
```

If `/dev/shm` is restricted on your system, force `/tmp`:

```bash
AFL_IN_DIR=/tmp/hilbert-afl-in AFL_OUT_DIR=/tmp/hilbert-afl-out AFL_TMPDIR=/tmp/hilbert-afl-tmp ./fuzz/scripts/start_afl_tmux.sh
```

Power governor helpers:

```bash
./fuzz/scripts/setPerformance.sh
./fuzz/scripts/restoreEnergy.sh
# typo-compatible alias:
./fuzz/scripts/restorEnergy.sh
```

Stop fuzzing + cleanup RAM/tmp AFL dirs + restore energy governor:

```bash
./fuzz/scripts/cleanup_afl.sh
```

`cleanup_afl.sh` prompts whether to also remove libFuzzer shm dirs:

- `/dev/shm/hilbert-libfuzzer-corpus`
- `/dev/shm/hilbert-libfuzzer-artifacts`

If `/dev/shm` is not writable on your system, the script falls back to `/tmp`.

Override prompt behavior:

```bash
CLEAN_LIBFUZZER_SHM=1 ./fuzz/scripts/cleanup_afl.sh   # remove libFuzzer dirs
CLEAN_LIBFUZZER_SHM=0 ./fuzz/scripts/cleanup_afl.sh   # keep libFuzzer dirs
```

Cleanup safety notes:

- `cleanup_afl.sh` refuses dangerous cleanup targets (`/`, `/tmp`, `/dev/shm`, home, non-canonical paths).
- By default it only allows cleanup under `/dev/shm/*` or `/tmp/*` with expected project leaf names.
- Override strict path policy only if intentional:

```bash
ALLOW_UNSAFE_CLEAN_PATHS=1 ./fuzz/scripts/cleanup_afl.sh
```

`start_afl_tmux.sh` calls `setPerformance.sh` by default. Disable with:

```bash
AUTO_SET_GOVERNOR=0 ./fuzz/scripts/start_afl_tmux.sh
```

Instance/build concurrency knobs:

- `JOBS`: number of AFL instances (default `6`, minimum `1`).
- `BUILD_JOBS`: build parallelism for `cmake --build` (default: CPU count).
- `BUILD_DIR`: AFL build directory (default `build-fuzz-afl`).
- `TARGET`: AFL target path (default `build-fuzz-afl/fuzz/fuzz_pipeline_afl`).
- `CC_BIN`: AFL compiler wrapper for auto-build (default `afl-clang-fast`).
- `CMAKE_BUILD_TYPE`: build type used for auto-build (default `Release`).
- `SESSION`: tmux session name (default `hilbert-afl`).
- `LIBFUZZER_CORPUS_DIRS`: `:`-separated import dirs for libFuzzer corpus.
- `AFL_IN_DIR`: input directory override.
- `AFL_OUT_DIR`: output directory override.
- `AFL_TMPDIR`: temp directory override.
- `AUTO_SET_GOVERNOR`: call `setPerformance.sh` before launch (`1` by default for direct script runs).
- `DRY_RUN`: print planned actions/commands without executing side effects.

Useful launcher forms:

```bash
./fuzz/scripts/start_afl_tmux.sh            # launch + attach
./fuzz/scripts/start_afl_tmux.sh --no-attach
TARGET=./build-fuzz-afl/fuzz/fuzz_file_slice_afl ./fuzz/scripts/start_afl_tmux.sh
DRY_RUN=1 ./fuzz/scripts/start_afl_tmux.sh --no-attach
```

Equivalent full commands launched inside tmux panes:

```bash
AFL_TMPDIR=<tmpdir> AFL_SKIP_CPUFREQ=1 AFL_AUTORESUME=1 AFL_NO_AFFINITY=1 afl-fuzz -M m0 -i <in> -o <out> -- <target>
AFL_TMPDIR=<tmpdir> AFL_SKIP_CPUFREQ=1 AFL_AUTORESUME=1 AFL_NO_AFFINITY=1 afl-fuzz -S s1 -i <in> -o <out> -- <target>
AFL_TMPDIR=<tmpdir> AFL_SKIP_CPUFREQ=1 AFL_AUTORESUME=1 AFL_NO_AFFINITY=1 afl-fuzz -S s2 -i <in> -o <out> -- <target>
AFL_TMPDIR=<tmpdir> AFL_SKIP_CPUFREQ=1 AFL_AUTORESUME=1 AFL_NO_AFFINITY=1 afl-fuzz -S s3 -i <in> -o <out> -- <target>
AFL_TMPDIR=<tmpdir> AFL_SKIP_CPUFREQ=1 AFL_AUTORESUME=1 AFL_NO_AFFINITY=1 afl-fuzz -S s4 -i <in> -o <out> -- <target>
AFL_TMPDIR=<tmpdir> AFL_SKIP_CPUFREQ=1 AFL_AUTORESUME=1 AFL_NO_AFFINITY=1 afl-fuzz -S s5 -i <in> -o <out> -- <target>
```

## Notes

- Inputs are capped to 1 MiB per test case to keep runs stable.
- Harness temp input files prefer `/dev/shm` and fall back to `/tmp`.
- Runner scripts also fall back to `/tmp` if `/dev/shm` is not writable.
- `fuzz/corpus/` provides starter seeds for both engines.
- AFL harnesses use persistent mode (`__AFL_LOOP`) and do not write PNG output files.
- If you need instrumentation in fuzz builds, set `-DHILBERTVIZ_COVERAGE=ON` at configure time.
- Coverage flags are skipped automatically when compiling with AFL compiler wrappers.

Coverage helper script (separate non-fuzz build directory):

```bash
./fuzz/scripts/run_coverage.sh
```

`run_coverage.sh` only accepts `BUILD_DIR` names beginning with `build-coverage`
and contained under this repository root (no absolute or `..` paths).

Use AFL corpus for coverage replay:

```bash
USE_AFL_CORPUS=1 ./fuzz/scripts/run_coverage.sh
```

Optional overrides:

```bash
AFL_OUT_DIRS="/dev/shm/hilbert-afl-out:/tmp/hilbert-afl-out" \
COVERAGE_FUZZ_TARGET=fuzz_pipeline_afl \
USE_AFL_CORPUS=1 ./fuzz/scripts/run_coverage.sh
```
