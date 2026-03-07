#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROOT_REAL="$(cd "${ROOT_DIR}" && pwd -P)"

BUILD_DIR="${BUILD_DIR:-build-tsan}"
CC_BIN="${CC_BIN:-gcc}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Debug}"
WITH_PNG="${WITH_PNG:-0}"
WITH_3D_VIEWER="${WITH_3D_VIEWER:-0}"
WARNINGS_AS_ERRORS="${WARNINGS_AS_ERRORS:-ON}"
DRY_RUN="${DRY_RUN:-0}"
CTEST_REGEX="${CTEST_REGEX:-}"
TSAN_OPTIONS_VALUE="${TSAN_OPTIONS:-halt_on_error=1 second_deadlock_stack=1 history_size=7}"
TSAN_C_FLAGS="${TSAN_C_FLAGS:--fsanitize=thread -fno-omit-frame-pointer -O1 -g}"
TSAN_LINK_FLAGS="${TSAN_LINK_FLAGS:--fsanitize=thread}"

die() {
  echo "$*" >&2
  exit 1
}

is_truthy() {
  case "$1" in
    1|true|TRUE|yes|YES|y|Y)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

validate_build_dir() {
  local value="$1"
  local build_parent=""
  local build_path=""
  local parent_real=""

  if [ -z "${value}" ]; then
    die "Refusing unsafe BUILD_DIR: value is empty."
  fi
  if [[ "${value}" = /* ]]; then
    die "Refusing unsafe BUILD_DIR='${value}': absolute paths are not allowed."
  fi
  if [[ "${value}" == *".."* ]]; then
    die "Refusing unsafe BUILD_DIR='${value}': parent traversal is not allowed."
  fi
  if [[ "${value}" == *"/"* ]]; then
    die "Refusing unsafe BUILD_DIR='${value}': nested paths are not allowed."
  fi
  if [[ "${value}" != build-tsan* ]]; then
    die "Refusing unsafe BUILD_DIR='${value}': must start with 'build-tsan'."
  fi

  build_path="${ROOT_DIR}/${value}"
  build_parent="$(dirname "${build_path}")"
  parent_real="$(cd "${build_parent}" && pwd -P)"
  build_path="${parent_real}/$(basename "${build_path}")"
  if [[ "${build_path}" != "${ROOT_REAL}/"* ]]; then
    die "Refusing unsafe BUILD_DIR='${value}': resolved path escapes repository root."
  fi

  printf '%s\n' "${build_path}"
}

warn_host_tsan_layout() {
  local mmap_rnd_bits=""
  local current_bits=""

  mmap_rnd_bits="/proc/sys/vm/mmap_rnd_bits"
  if [ ! -r "${mmap_rnd_bits}" ]; then
    return 0
  fi

  current_bits="$(cat "${mmap_rnd_bits}" 2>/dev/null || true)"
  case "${current_bits}" in
    ''|*[!0-9]*)
      return 0
      ;;
  esac

  if [ "${current_bits}" -gt 28 ]; then
    cat >&2 <<EOF
TSan requires vm.mmap_rnd_bits=28.
Set it before this run:
  sudo sysctl vm.mmap_rnd_bits=28
Restore it after:
  sudo sysctl vm.mmap_rnd_bits=${current_bits}
EOF
  fi
}

BUILD_PATH="$(validate_build_dir "${BUILD_DIR}")"

if ! command -v "${CC_BIN}" >/dev/null 2>&1; then
  die "Compiler not found: ${CC_BIN}"
fi

cmake_cmd=(
  cmake
  -S "${ROOT_DIR}"
  -B "${BUILD_PATH}"
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
  -DHILBERTVIZ_WITH_3D_VIEWER="$(if is_truthy "${WITH_3D_VIEWER}"; then printf ON; else printf OFF; fi)"
  -DHILBERTVIZ_WITH_PNG="$(if is_truthy "${WITH_PNG}"; then printf ON; else printf OFF; fi)"
  -DHILBERTVIZ_WARNINGS_AS_ERRORS="${WARNINGS_AS_ERRORS}"
  -DCMAKE_C_FLAGS="${TSAN_C_FLAGS}"
  -DCMAKE_EXE_LINKER_FLAGS="${TSAN_LINK_FLAGS}"
  -DCMAKE_SHARED_LINKER_FLAGS="${TSAN_LINK_FLAGS}"
)

build_cmd=(
  cmake
  --build "${BUILD_PATH}"
  -j"${JOBS}"
)

test_cmd=(
  ctest
  --test-dir "${BUILD_PATH}"
  --output-on-failure
)

if [ -n "${CTEST_REGEX}" ]; then
  test_cmd+=(-R "${CTEST_REGEX}")
fi

echo "Using TSan build directory: ${BUILD_PATH}"
echo "Configuring with compiler: ${CC_BIN}"

if is_truthy "${DRY_RUN}"; then
  echo "DRY_RUN=1; planned commands:"
  printf 'CC=%q' "${CC_BIN}"
  printf ' %q' "${cmake_cmd[@]}"
  printf '\n'
  printf '%q' "${build_cmd[@]}"
  printf '\n'
  printf 'TSAN_OPTIONS=%q' "${TSAN_OPTIONS_VALUE}"
  printf ' %q' "${test_cmd[@]}"
  printf '\n'
  exit 0
fi

warn_host_tsan_layout

CC="${CC_BIN}" "${cmake_cmd[@]}"
"${build_cmd[@]}"
TSAN_OPTIONS="${TSAN_OPTIONS_VALUE}" "${test_cmd[@]}"
