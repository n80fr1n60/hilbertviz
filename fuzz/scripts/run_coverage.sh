#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
ROOT_REAL="$(cd "${ROOT_DIR}" && pwd -P)"

BUILD_DIR="${BUILD_DIR:-build-coverage}"
CC_BIN="${CC_BIN:-gcc}"
GCOV_BIN="${GCOV_BIN:-gcov}"
JQ_BIN="${JQ_BIN:-jq}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
GENERATE_HTML="${GENERATE_HTML:-1}"
USE_AFL_CORPUS="${USE_AFL_CORPUS:-0}"
RUN_FUZZ_SMOKE="${RUN_FUZZ_SMOKE:-1}"
COVERAGE_FUZZ_TARGET="${COVERAGE_FUZZ_TARGET:-fuzz_pipeline_afl}"
AFL_OUT_DIRS="${AFL_OUT_DIRS:-${AFL_OUT_DIR:-/dev/shm/hilbert-afl-out:/tmp/hilbert-afl-out}}"

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

can_generate_gcov_summary() {
  command -v "${GCOV_BIN}" >/dev/null 2>&1 &&
  command -v "${JQ_BIN}" >/dev/null 2>&1 &&
  command -v gzip >/dev/null 2>&1
}

write_gcov_summary() {
  local source_root="$1"
  local summary_path="$2"
  local summary_label="$3"
  shift 3

  local gcno=""
  local source_path=""
  local work_dir=""
  local generated_any=0
  local -a json_files=()

  mkdir -p "${BUILD_PATH}/coverage"
  work_dir="$(mktemp -d "${BUILD_PATH}/coverage/${summary_label}.XXXXXX")"

  pushd "${work_dir}" >/dev/null
  for gcno in "$@"; do
    [ -f "${gcno}" ] || continue
    source_path="${source_root}/$(basename "${gcno}" .gcno)"
    if [ ! -f "${source_path}" ]; then
      popd >/dev/null
      rm -rf "${work_dir}"
      die "Missing source for gcov summary: ${source_path}"
    fi
    "${GCOV_BIN}" -j -a -b -c -f -o "${gcno}" "${source_path}" >/dev/null
    generated_any=1
  done

  if [ "${generated_any}" -eq 0 ]; then
    popd >/dev/null
    rm -rf "${work_dir}"
    return 1
  fi

  shopt -s nullglob
  json_files=( *.gcov.json.gz )
  shopt -u nullglob
  if [ "${#json_files[@]}" -eq 0 ]; then
    popd >/dev/null
    rm -rf "${work_dir}"
    die "gcov summary generation produced no JSON files for ${summary_label}"
  fi

  "${JQ_BIN}" -s '
    [ .[] | .files[] ] as $files |
    {
      line_executed: ([ $files[] | .lines[] | select(.count > 0) ] | length),
      line_total: ([ $files[] | .lines[] ] | length),
      function_executed: ([ $files[] | .functions[] | select(.execution_count > 0) ] | length),
      function_total: ([ $files[] | .functions[] ] | length),
      block_executed: ([ $files[] | .functions[] | .blocks_executed ] | add),
      block_total: ([ $files[] | .functions[] | .blocks ] | add)
    }
    | .line_pct = ((100.0 * .line_executed) / .line_total)
    | .function_pct = ((100.0 * .function_executed) / .function_total)
    | .block_pct = ((100.0 * .block_executed) / .block_total)
  ' <(for f in "${json_files[@]}"; do gzip -dc "${f}"; echo; done) > "${summary_path}"

  popd >/dev/null
  rm -rf "${work_dir}"
  return 0
}

print_gcov_summary() {
  local label="$1"
  local summary_path="$2"

  "${JQ_BIN}" -r --arg summary_name "${label}" '
    def r2: ((. * 100.0) | round) / 100.0;
    $summary_name + ": " +
    "lines=" + ((.line_pct | r2) | tostring) + "% (" + (.line_executed | tostring) + "/" + (.line_total | tostring) + "), " +
    "functions=" + ((.function_pct | r2) | tostring) + "% (" + (.function_executed | tostring) + "/" + (.function_total | tostring) + "), " +
    "basic-blocks=" + ((.block_pct | r2) | tostring) + "% (" + (.block_executed | tostring) + "/" + (.block_total | tostring) + ")"
  ' "${summary_path}"
}

emit_gcov_summaries() {
  local app_summary="${BUILD_PATH}/app-coverage-summary.json"
  local fuzz_summary="${BUILD_PATH}/fuzz-target-coverage-summary.json"
  local -a app_gcno=()
  local -a fuzz_gcno=()

  if ! can_generate_gcov_summary; then
    echo "Skipping gcov JSON summaries; need ${GCOV_BIN}, ${JQ_BIN}, and gzip."
    return 0
  fi

  shopt -s nullglob
  app_gcno=(
    "${BUILD_PATH}"/src/CMakeFiles/hvcore.dir/*.gcno
    "${BUILD_PATH}"/src/CMakeFiles/hilbertviz.dir/main.c.gcno
    "${BUILD_PATH}"/src/CMakeFiles/hilbertviz3d.dir/main_3d.c.gcno
  )
  fuzz_gcno=(
    "${BUILD_PATH}"/fuzz/CMakeFiles/hvfuzzcore.dir/fuzz_target.c.gcno
  )
  shopt -u nullglob

  if write_gcov_summary "${ROOT_DIR}/src" "${app_summary}" "gcov-app" "${app_gcno[@]}"; then
    print_gcov_summary "Primary app coverage" "${app_summary}"
    echo "App summary JSON: ${app_summary}"
  else
    echo "Skipping primary app gcov summary; no matching coverage objects found."
  fi

  if write_gcov_summary "${ROOT_DIR}/fuzz" "${fuzz_summary}" "gcov-fuzz-target" "${fuzz_gcno[@]}"; then
    print_gcov_summary "Fuzz target coverage" "${fuzz_summary}"
    echo "Fuzz target summary JSON: ${fuzz_summary}"
  fi
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
run_fuzz_smoke=0
if is_truthy "${RUN_FUZZ_SMOKE}"; then
  run_fuzz_smoke=1
fi

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
elif [ "${run_fuzz_smoke}" -eq 1 ]; then
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

emit_gcov_summaries

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
