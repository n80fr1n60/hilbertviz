#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_DIR="${BUILD_DIR:-build-libfuzzer}"
CC_BIN="${CC_BIN:-clang}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Debug}"
HILBERTVIZ_FUZZ_SANITIZERS="${HILBERTVIZ_FUZZ_SANITIZERS:-ON}"

DEFAULT_TARGET="${ROOT_DIR}/${BUILD_DIR}/fuzz/fuzz_pipeline_libfuzzer"
TARGET="${1:-${DEFAULT_TARGET}}"
if [ "$#" -gt 0 ]; then
  shift
fi

default_root="/dev/shm"
if [ ! -d "${default_root}" ] || [ ! -w "${default_root}" ] || [ ! -x "${default_root}" ]; then
  default_root="/tmp"
fi

CORPUS_DIR="${1:-${default_root}/hilbert-libfuzzer-corpus}"
if [ "$#" -gt 0 ]; then
  shift
fi
ARTIFACT_DIR="${1:-${default_root}/hilbert-libfuzzer-artifacts}"
if [ "$#" -gt 0 ]; then
  shift
fi

build_if_missing() {
  local target_name
  target_name="$(basename "${TARGET}")"

  case "${target_name}" in
    fuzz_pipeline_libfuzzer|fuzz_file_slice_libfuzzer)
      ;;
    *)
      echo "Target not found: ${TARGET}" >&2
      echo "Cannot auto-build unknown target basename '${target_name}'." >&2
      echo "Use one of: fuzz_pipeline_libfuzzer, fuzz_file_slice_libfuzzer" >&2
      return 1
      ;;
  esac

  if ! command -v "${CC_BIN}" >/dev/null 2>&1; then
    echo "Compiler not found: ${CC_BIN}" >&2
    return 1
  fi

  echo "Missing binary '${TARGET}', configuring and building '${target_name}'..."
  CC="${CC_BIN}" cmake \
    -S "${ROOT_DIR}" \
    -B "${ROOT_DIR}/${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
    -DHILBERTVIZ_FUZZ=ON \
    -DHILBERTVIZ_FUZZ_ENGINE=libfuzzer \
    -DHILBERTVIZ_FUZZ_SANITIZERS="${HILBERTVIZ_FUZZ_SANITIZERS}" \
    -DBUILD_TESTING=OFF

  cmake --build "${ROOT_DIR}/${BUILD_DIR}" -j"${JOBS}" --target "${target_name}"
}

if [ ! -x "${TARGET}" ]; then
  build_if_missing
fi

if [ ! -x "${TARGET}" ]; then
  echo "Failed to build libFuzzer target: ${TARGET}" >&2
  exit 1
fi

mkdir -p "${CORPUS_DIR}" "${ARTIFACT_DIR}"
if [ -d "${ROOT_DIR}/fuzz/corpus" ]; then
  cp -n "${ROOT_DIR}"/fuzz/corpus/* "${CORPUS_DIR}/" 2>/dev/null || true
fi

exec "${TARGET}" \
  -artifact_prefix="${ARTIFACT_DIR}/" \
  "${CORPUS_DIR}" \
  "$@"
