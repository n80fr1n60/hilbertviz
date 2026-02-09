#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
ROOT_REAL="$(cd "${ROOT_DIR}" && pwd -P)"

BUILD_DIR="${BUILD_DIR:-build-coverage-clang20}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
CLANG_BIN="${CLANG_BIN:-clang-20}"
CLANGXX_BIN="${CLANGXX_BIN:-clang++}"
LLVM_PROFDATA_BIN="${LLVM_PROFDATA_BIN:-llvm-profdata}"
LLVM_COV_BIN="${LLVM_COV_BIN:-llvm-cov}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
GENERATE_HTML="${GENERATE_HTML:-1}"
PROFILE_FILE_PATTERN="${PROFILE_FILE_PATTERN:-%m_%p.profraw}"
RESOURCE_DIR_OVERRIDE="${RESOURCE_DIR_OVERRIDE:-}"
FALLBACK_PROFILE_RUNTIME="${FALLBACK_PROFILE_RUNTIME:-}"

die() {
  echo "$*" >&2
  exit 1
}

resolve_tool_path() {
  local requested="$1"
  local candidate=""
  local chosen=""

  if [[ "${requested}" == */* ]]; then
    if [ ! -x "${requested}" ]; then
      die "Required tool not executable: ${requested}"
    fi
    printf '%s\n' "${requested}"
    return 0
  fi

  while IFS= read -r candidate; do
    [ -z "${candidate}" ] && continue
    if [ -z "${chosen}" ]; then
      chosen="${candidate}"
    fi
    if [[ "${candidate}" != *"/swiftly/"* ]]; then
      chosen="${candidate}"
      break
    fi
  done < <(type -aP "${requested}" 2>/dev/null || true)

  if [ -z "${chosen}" ]; then
    die "Required tool not found in PATH: ${requested}"
  fi

  printf '%s\n' "${chosen}"
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
  if [[ "${value}" != build-coverage-clang20* ]]; then
    die "Refusing unsafe BUILD_DIR='${value}': must start with 'build-coverage-clang20'."
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

resolve_resource_dir() {
  local clang_bin="$1"
  local resource_dir=""
  local fallback_resource=""

  if [ -n "${RESOURCE_DIR_OVERRIDE}" ]; then
    printf '%s\n' "${RESOURCE_DIR_OVERRIDE}"
    return 0
  fi

  resource_dir="$("${clang_bin}" -print-resource-dir)"
  if find "${resource_dir}/lib" -type f -name 'libclang_rt.profile*.a' -print -quit >/dev/null 2>&1; then
    printf '%s\n' "${resource_dir}"
    return 0
  fi

  if [ -n "${FALLBACK_PROFILE_RUNTIME}" ]; then
    if [ ! -f "${FALLBACK_PROFILE_RUNTIME}" ]; then
      die "FALLBACK_PROFILE_RUNTIME does not exist: ${FALLBACK_PROFILE_RUNTIME}"
    fi
    fallback_resource="$(dirname "$(dirname "$(dirname "${FALLBACK_PROFILE_RUNTIME}")")")"
    printf '%s\n' "${fallback_resource}"
    return 0
  fi

  printf '%s\n' "${resource_dir}"
}

BUILD_PATH="$(validate_build_dir "${BUILD_DIR}")"

CLANG_BIN="$(resolve_tool_path "${CLANG_BIN}")"
CLANGXX_BIN="$(resolve_tool_path "${CLANGXX_BIN}")"
LLVM_PROFDATA_BIN="$(resolve_tool_path "${LLVM_PROFDATA_BIN}")"
LLVM_COV_BIN="$(resolve_tool_path "${LLVM_COV_BIN}")"
CMK_BIN="$(resolve_tool_path "cmake")"
CTEST_BIN="$(resolve_tool_path "ctest")"

clang_version_line="$("${CLANG_BIN}" --version | head -n 1)"
if [[ "${clang_version_line}" != *"version 20."* ]]; then
  die "CLANG_BIN must be clang 20.x, got: ${clang_version_line}"
fi

RESOURCE_DIR="$(resolve_resource_dir "${CLANG_BIN}")"
if ! find "${RESOURCE_DIR}/lib" -type f -name 'libclang_rt.profile*.a' -print -quit >/dev/null 2>&1; then
  die "Could not find libclang_rt.profile in resource dir '${RESOURCE_DIR}'. Set RESOURCE_DIR_OVERRIDE or FALLBACK_PROFILE_RUNTIME."
fi

COV_FLAGS="-fprofile-instr-generate -fcoverage-mapping -resource-dir=${RESOURCE_DIR}"
PROFILE_DIR="${BUILD_PATH}/profiles"
PROFDATA_PATH="${BUILD_PATH}/coverage.profdata"
REPORT_TXT="${BUILD_PATH}/coverage-report.txt"
REPORT_SRC_TXT="${BUILD_PATH}/coverage-report-src-only.txt"
HTML_DIR="${BUILD_PATH}/coverage-html"

echo "Using clang: ${CLANG_BIN}"
echo "Using clang++: ${CLANGXX_BIN}"
echo "Using llvm-profdata: ${LLVM_PROFDATA_BIN}"
echo "Using llvm-cov: ${LLVM_COV_BIN}"
echo "Using resource dir: ${RESOURCE_DIR}"
echo "Using coverage build directory: ${BUILD_PATH}"

CC="${CLANG_BIN}" \
  CXX="${CLANGXX_BIN}" \
  CFLAGS="${COV_FLAGS}" \
  CXXFLAGS="${COV_FLAGS}" \
  LDFLAGS="${COV_FLAGS}" \
  "${CMK_BIN}" \
    -S "${ROOT_DIR}" \
    -B "${BUILD_PATH}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DBUILD_TESTING=ON \
    -DHILBERTVIZ_WITH_PNG=ON \
    -DHILBERTVIZ_WARNINGS_AS_ERRORS=ON \
    -DHILBERTVIZ_SANITIZERS=OFF \
    -DHILBERTVIZ_COVERAGE=OFF

"${CMK_BIN}" --build "${BUILD_PATH}" -j"${JOBS}"

mkdir -p "${PROFILE_DIR}"
rm -f "${PROFILE_DIR}"/*.profraw

LLVM_PROFILE_FILE="${PROFILE_DIR}/${PROFILE_FILE_PATTERN}" \
  "${CTEST_BIN}" --test-dir "${BUILD_PATH}" --output-on-failure

shopt -s nullglob
profile_files=( "${PROFILE_DIR}"/*.profraw )
shopt -u nullglob
if [ "${#profile_files[@]}" -eq 0 ]; then
  die "No raw profile files were produced in ${PROFILE_DIR}"
fi

"${LLVM_PROFDATA_BIN}" merge -sparse "${profile_files[@]}" -o "${PROFDATA_PATH}"

"${LLVM_COV_BIN}" report \
  "${BUILD_PATH}/tests/test_hv" \
  -object "${BUILD_PATH}/src/hilbertviz" \
  -instr-profile="${PROFDATA_PATH}" \
  > "${REPORT_TXT}"

"${LLVM_COV_BIN}" report \
  "${BUILD_PATH}/tests/test_hv" \
  -object "${BUILD_PATH}/src/hilbertviz" \
  -instr-profile="${PROFDATA_PATH}" \
  -ignore-filename-regex='tests/|/include/' \
  > "${REPORT_SRC_TXT}"

if [ "${GENERATE_HTML}" = "1" ]; then
  rm -rf "${HTML_DIR}"
  "${LLVM_COV_BIN}" show \
    "${BUILD_PATH}/tests/test_hv" \
    -object "${BUILD_PATH}/src/hilbertviz" \
    -instr-profile="${PROFDATA_PATH}" \
    -format=html \
    -output-dir="${HTML_DIR}" \
    -show-line-counts-or-regions
fi

echo "Coverage report (all files): ${REPORT_TXT}"
echo "Coverage report (src-only): ${REPORT_SRC_TXT}"
if [ "${GENERATE_HTML}" = "1" ]; then
  echo "Coverage HTML: ${HTML_DIR}/index.html"
fi
