#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
# shellcheck source=afl_common.sh
source "${SCRIPT_DIR}/afl_common.sh"

BUILD_DIR="${BUILD_DIR:-build-fuzz-afl}"
SESSION="${SESSION:-hilbert-afl}"
CC_BIN="${CC_BIN:-afl-clang-fast}"
BUILD_JOBS="${BUILD_JOBS:-$(afl_cpu_count)}"
ATTACH_MODE="${1:---attach}"
AUTO_SET_GOVERNOR="${AUTO_SET_GOVERNOR:-1}"
JOBS="${JOBS:-6}"
LIBFUZZER_CORPUS_DIRS="${LIBFUZZER_CORPUS_DIRS:-/dev/shm/hilbert-libfuzzer-corpus:/tmp/hilbert-libfuzzer-corpus}"
TARGET="${TARGET:-${ROOT_DIR}/${BUILD_DIR}/fuzz/fuzz_pipeline_afl}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
DRY_RUN="${DRY_RUN:-0}"

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

dry_run=0
if is_truthy "${DRY_RUN}"; then
  dry_run=1
fi

if ! [[ "${JOBS}" =~ ^[0-9]+$ ]]; then
  echo "JOBS must be a positive integer (got '${JOBS}')" >&2
  exit 1
fi
if [ "${JOBS}" -lt 1 ]; then
  echo "JOBS must be >= 1 (got '${JOBS}')" >&2
  exit 1
fi
case "${ATTACH_MODE}" in
  --attach|--no-attach)
    ;;
  *)
    echo "ATTACH_MODE must be --attach or --no-attach (got '${ATTACH_MODE}')" >&2
    exit 1
    ;;
esac
if ! [[ "${SESSION}" =~ ^[A-Za-z0-9._-]+$ ]]; then
  echo "SESSION contains unsupported characters: '${SESSION}'" >&2
  echo "Use only letters, numbers, dot, underscore, and dash." >&2
  exit 1
fi

prompt_yes_no() {
  local prompt="$1"
  local default_answer="${2:-y}"
  local reply=""

  if [ -t 0 ]; then
    if [ "${default_answer}" = "y" ]; then
      read -r -p "${prompt} [Y/n] " reply
      reply="${reply:-y}"
    else
      read -r -p "${prompt} [y/N] " reply
      reply="${reply:-n}"
    fi
  else
    reply="${default_answer}"
    echo "${prompt} -> ${reply} (non-interactive default)"
  fi

  case "${reply}" in
    y|Y|yes|YES)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

resolve_afl_target_name() {
  local candidate="$1"
  local name
  name="$(basename "${candidate}")"
  case "${name}" in
    fuzz_pipeline_afl|fuzz_file_slice_afl)
      printf '%s\n' "${name}"
      return 0
      ;;
    *)
      echo "Unsupported AFL target basename '${name}'." >&2
      echo "Expected fuzz_pipeline_afl or fuzz_file_slice_afl." >&2
      return 1
      ;;
  esac
}

build_afl_target() {
  local target_name="$1"

  if [ "${dry_run}" -eq 1 ]; then
    echo "DRY_RUN: would build AFL target '${target_name}' in '${BUILD_DIR}'."
    return 0
  fi

  if ! command -v "${CC_BIN}" >/dev/null 2>&1; then
    echo "Compiler not found: ${CC_BIN}" >&2
    return 1
  fi

  echo "Building AFL target '${target_name}' in '${BUILD_DIR}'..."
  CC="${CC_BIN}" cmake \
    -S "${ROOT_DIR}" \
    -B "${ROOT_DIR}/${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
    -DHILBERTVIZ_FUZZ=ON \
    -DHILBERTVIZ_FUZZ_ENGINE=afl \
    -DBUILD_TESTING=OFF \
    -DHILBERTVIZ_SANITIZERS=OFF
  cmake --build "${ROOT_DIR}/${BUILD_DIR}" -j"${BUILD_JOBS}" --target "${target_name}"
}

build_afl_cmdline() {
  local mode="$1"
  local worker_id="$2"
  local in_dir="$3"
  local out_dir="$4"
  local tmp_dir="$5"
  local target_path="$6"
  local -a cmd=(
    env
    AFL_TMPDIR="${tmp_dir}"
    AFL_SKIP_CPUFREQ=1
    AFL_AUTORESUME=1
    AFL_NO_AFFINITY=1
    afl-fuzz
  )

  if [ "${mode}" = "master" ]; then
    cmd+=(-M m0)
  else
    cmd+=(-S "s${worker_id}")
  fi

  cmd+=(
    -i "${in_dir}"
    -o "${out_dir}"
    --
    "${target_path}"
  )

  printf '%q ' "${cmd[@]}"
}

require_non_powersave_governor() {
  local cpu_sys="/sys/devices/system/cpu"
  local -a gov_files=()
  local gov=""
  local file

  if [ ! -d "${cpu_sys}" ]; then
    return 0
  fi

  shopt -s nullglob
  gov_files=("${cpu_sys}"/cpu*/cpufreq/scaling_governor)
  shopt -u nullglob

  if [ "${#gov_files[@]}" -eq 0 ]; then
    return 0
  fi

  for file in "${gov_files[@]}"; do
    gov="$(cat "${file}" 2>/dev/null || true)"
    if [ "${gov}" = "powersave" ]; then
      echo "CPU governor is 'powersave' on at least one core." >&2
      echo "Run ./fuzz/scripts/setPerformance.sh, then run this script again." >&2
      return 1
    fi
  done

  return 0
}

target_name="$(resolve_afl_target_name "${TARGET}")"
if [ -x "${TARGET}" ]; then
  echo "Found existing AFL binary: ${TARGET}"
  if prompt_yes_no "Use this binary without rebuild?" "y"; then
    echo "Using existing binary."
  else
    build_afl_target "${target_name}"
    TARGET="${ROOT_DIR}/${BUILD_DIR}/fuzz/${target_name}"
  fi
else
  echo "AFL binary not found: ${TARGET}"
  build_afl_target "${target_name}"
  TARGET="${ROOT_DIR}/${BUILD_DIR}/fuzz/${target_name}"
fi

if [ "${dry_run}" -eq 0 ] && [ ! -x "${TARGET}" ]; then
  echo "AFL target is still missing after build: ${TARGET}" >&2
  exit 1
fi

if [ "${dry_run}" -eq 0 ]; then
  afl_require_command tmux
  afl_require_command afl-fuzz
else
  if ! command -v tmux >/dev/null 2>&1; then
    echo "DRY_RUN: note - tmux not found in PATH."
  fi
  if ! command -v afl-fuzz >/dev/null 2>&1; then
    echo "DRY_RUN: note - afl-fuzz not found in PATH."
  fi
fi

if [ "${AUTO_SET_GOVERNOR}" = "1" ]; then
  if [ "${dry_run}" -eq 1 ]; then
    echo "DRY_RUN: would run ${ROOT_DIR}/fuzz/scripts/setPerformance.sh"
  else
    "${ROOT_DIR}/fuzz/scripts/setPerformance.sh"
  fi
fi

if [ "${dry_run}" -eq 0 ]; then
  require_non_powersave_governor
else
  echo "DRY_RUN: skipping CPU governor enforcement check."
fi

default_root="$(afl_default_ram_root)"
IN_DIR="$(afl_pick_work_dir "${AFL_IN_DIR:-${default_root}/hilbert-afl-in}" "hilbert-afl-in" "/tmp")"
OUT_DIR="$(afl_pick_work_dir "${AFL_OUT_DIR:-${default_root}/hilbert-afl-out}" "hilbert-afl-out" "/tmp")"
TMP_DIR="$(afl_pick_work_dir "${AFL_TMPDIR:-${default_root}/hilbert-afl-tmp}" "hilbert-afl-tmp" "/tmp")"

if [ "${dry_run}" -eq 0 ]; then
  mkdir -p "${IN_DIR}" "${OUT_DIR}" "${TMP_DIR}"
  afl_seed_inputs "${IN_DIR}" "${ROOT_DIR}" "${LIBFUZZER_CORPUS_DIRS}"
else
  echo "DRY_RUN: would prepare directories:"
  echo "  IN_DIR=${IN_DIR}"
  echo "  OUT_DIR=${OUT_DIR}"
  echo "  TMP_DIR=${TMP_DIR}"
fi

echo "Using JOBS=${JOBS} AFL instance(s): 1 master + $((JOBS - 1)) slave(s)"

cmds=()
cmds+=("$(build_afl_cmdline "master" "0" "${IN_DIR}" "${OUT_DIR}" "${TMP_DIR}" "${TARGET}")")
if [ "${JOBS}" -gt 1 ]; then
  for ((i=1; i<"${JOBS}"; ++i)); do
    cmds+=("$(build_afl_cmdline "slave" "${i}" "${IN_DIR}" "${OUT_DIR}" "${TMP_DIR}" "${TARGET}")")
  done
fi

if [ "${dry_run}" -eq 1 ]; then
  echo "DRY_RUN: would start tmux session '${SESSION}' with commands:"
  for cmd in "${cmds[@]}"; do
    echo "  ${cmd}"
  done
  if [ "${ATTACH_MODE}" = "--attach" ]; then
    echo "DRY_RUN: would attach to tmux session '${SESSION}'."
  fi
  exit 0
fi

tmux kill-session -t "${SESSION}" 2>/dev/null || true
tmux new-session -d -x 240 -y 80 -s "${SESSION}" -n fuzz

if [ "${JOBS}" -gt 1 ]; then
  for ((i=1; i<"${JOBS}"; ++i)); do
    tmux split-window -t "${SESSION}:0"
  done
  tmux select-layout -t "${SESSION}:0" tiled
fi

mapfile -t panes < <(tmux list-panes -t "${SESSION}:0" -F "#{pane_id}")
if [ "${#panes[@]}" -lt "${#cmds[@]}" ]; then
  echo "Not enough tmux panes (${#panes[@]}) for commands (${#cmds[@]})." >&2
  tmux kill-session -t "${SESSION}" 2>/dev/null || true
  exit 1
fi

for i in "${!cmds[@]}"; do
  tmux send-keys -t "${panes[$i]}" "${cmds[$i]}" C-m
done

echo "Session started. Attach with:"
echo "  tmux attach -t ${SESSION}"

if [ "${ATTACH_MODE}" = "--attach" ]; then
  exec tmux attach -t "${SESSION}"
fi
