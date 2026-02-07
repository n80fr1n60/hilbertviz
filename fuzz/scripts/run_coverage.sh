#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
ROOT_REAL="$(cd "${ROOT_DIR}" && pwd -P)"

BUILD_DIR="${BUILD_DIR:-build-coverage}"
CC_BIN="${CC_BIN:-gcc}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
GENERATE_HTML="${GENERATE_HTML:-1}"
USE_AFL_CORPUS="${USE_AFL_CORPUS:-0}"
COVERAGE_FUZZ_TARGET="${COVERAGE_FUZZ_TARGET:-fuzz_pipeline_afl}"
AFL_OUT_DIRS="${AFL_OUT_DIRS:-${AFL_OUT_DIR:-/dev/shm/hilbert-afl-out:/tmp/hilbert-afl-out}}"

die() {
  echo "$*" >&2
  exit 1
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
  if [[ "${value}" != build-coverage* ]]; then
    die "Refusing unsafe BUILD_DIR='${value}': must start with 'build-coverage'."
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

BUILD_PATH="$(validate_build_dir "${BUILD_DIR}")"

if ! command -v "${CC_BIN}" >/dev/null 2>&1; then
  echo "Compiler not found: ${CC_BIN}" >&2
  exit 1
fi

echo "Using coverage build directory: ${BUILD_PATH}"
echo "Configuring with compiler: ${CC_BIN}"

cmake_cmd=(
  cmake
  -S "${ROOT_DIR}"
  -B "${BUILD_PATH}"
  -DCMAKE_BUILD_TYPE=Debug
  -DHILBERTVIZ_COVERAGE=ON
  -DBUILD_TESTING=ON
)

if [ "${USE_AFL_CORPUS}" = "1" ]; then
  cmake_cmd+=(
    -DHILBERTVIZ_FUZZ=ON
    -DHILBERTVIZ_FUZZ_ENGINE=afl
    -DHILBERTVIZ_SANITIZERS=OFF
  )
fi

CC="${CC_BIN}" "${cmake_cmd[@]}"
cmake --build "${BUILD_PATH}" -j"${JOBS}"

# Ensure clean coverage counters for this run even when lcov is unavailable.
find "${BUILD_PATH}" -type f -name '*.gcda' -delete

if [ "${GENERATE_HTML}" = "1" ] && command -v lcov >/dev/null 2>&1 && command -v genhtml >/dev/null 2>&1; then
  lcov --directory "${BUILD_PATH}" --zerocounters
fi

ctest --test-dir "${BUILD_PATH}" --output-on-failure

if [ "${USE_AFL_CORPUS}" = "1" ]; then
  HARNESS="${BUILD_PATH}/fuzz/${COVERAGE_FUZZ_TARGET}"
  COVERAGE_DIR="${BUILD_PATH}/coverage"
  AFL_LIST_FILE="${COVERAGE_DIR}/afl_inputs.list0"
  corpus_count=0
  out_dirs=()

  if [ ! -x "${HARNESS}" ]; then
    echo "AFL coverage harness not found: ${HARNESS}" >&2
    exit 1
  fi

  mkdir -p "${COVERAGE_DIR}"
  : > "${AFL_LIST_FILE}"

  IFS=':' read -r -a out_dirs <<< "${AFL_OUT_DIRS}"
  for d in "${out_dirs[@]}"; do
    if [ -d "${d}" ]; then
      find "${d}" -type f \( -path '*/queue/id:*' -o -path '*/crashes/id:*' -o -path '*/hangs/id:*' \) -print0 >> "${AFL_LIST_FILE}"
    fi
  done

  corpus_count="$(tr -cd '\0' < "${AFL_LIST_FILE}" | wc -c | tr -d ' ')"
  if [ "${corpus_count}" -eq 0 ]; then
    echo "No AFL corpus inputs found in: ${AFL_OUT_DIRS}"
  else
    echo "Replaying ${corpus_count} AFL inputs through ${COVERAGE_FUZZ_TARGET}..."
    xargs -0 -n1 -P"${JOBS}" "${HARNESS}" < "${AFL_LIST_FILE}"
  fi
fi

if [ "${GENERATE_HTML}" != "1" ]; then
  echo "Coverage run completed (HTML generation disabled)."
  exit 0
fi

if command -v lcov >/dev/null 2>&1 && command -v genhtml >/dev/null 2>&1; then
  COVERAGE_DIR="${BUILD_PATH}/coverage"
  COVERAGE_INFO="${COVERAGE_DIR}/coverage.info"
  COVERAGE_FILTERED="${COVERAGE_DIR}/coverage.filtered.info"
  COVERAGE_HTML="${COVERAGE_DIR}/html"

  mkdir -p "${COVERAGE_DIR}"
  lcov --capture --directory "${BUILD_PATH}" --output-file "${COVERAGE_INFO}"
  lcov --remove "${COVERAGE_INFO}" \
    "/usr/*" "*/tests/*" "*/fuzz/*" "*/build*/*" \
    --output-file "${COVERAGE_FILTERED}"
  genhtml "${COVERAGE_FILTERED}" --output-directory "${COVERAGE_HTML}"

  echo "Coverage report: ${BUILD_PATH}/coverage/html/index.html"
else
  echo "lcov/genhtml not found; skipping HTML report generation."
  echo "Install lcov to enable HTML output."
fi
