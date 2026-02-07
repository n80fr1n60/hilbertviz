#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=afl_common.sh
source "${SCRIPT_DIR}/afl_common.sh"

SESSION="${SESSION:-hilbert-afl}"

default_root="$(afl_default_ram_root)"
IN_DIR="$(afl_pick_work_dir "${AFL_IN_DIR:-${default_root}/hilbert-afl-in}" "hilbert-afl-in" "/tmp")"
OUT_DIR="$(afl_pick_work_dir "${AFL_OUT_DIR:-${default_root}/hilbert-afl-out}" "hilbert-afl-out" "/tmp")"
TMP_DIR="$(afl_pick_work_dir "${AFL_TMPDIR:-${default_root}/hilbert-afl-tmp}" "hilbert-afl-tmp" "/tmp")"
LIBFUZZER_CORPUS_DIR="${LIBFUZZER_CORPUS_DIR:-${default_root}/hilbert-libfuzzer-corpus}"
LIBFUZZER_ARTIFACT_DIR="${LIBFUZZER_ARTIFACT_DIR:-${default_root}/hilbert-libfuzzer-artifacts}"
CLEAN_LIBFUZZER_SHM="${CLEAN_LIBFUZZER_SHM:-ask}"
DRY_RUN="${DRY_RUN:-0}"
ALLOW_UNSAFE_CLEAN_PATHS="${ALLOW_UNSAFE_CLEAN_PATHS:-0}"

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
allow_unsafe_clean_paths=0
if is_truthy "${DRY_RUN}"; then
  dry_run=1
fi
if is_truthy "${ALLOW_UNSAFE_CLEAN_PATHS}"; then
  allow_unsafe_clean_paths=1
fi

prompt_yes_no() {
  local prompt="$1"
  local default_answer="${2:-n}"
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

validate_cleanup_target() {
  local path="$1"
  local expected_leaf="$2"
  local label="$3"
  local leaf

  if [ -z "${path}" ]; then
    echo "Refusing to clean empty path for ${label}." >&2
    return 1
  fi

  case "${path}" in
    /|/tmp|/dev/shm|/home|"${HOME}"|.|..)
      echo "Refusing dangerous cleanup path for ${label}: ${path}" >&2
      return 1
      ;;
  esac

  case "${path}" in
    */..|*/../*|*/.|*/./*)
      echo "Refusing non-canonical cleanup path for ${label}: ${path}" >&2
      return 1
      ;;
  esac

  if [ "${allow_unsafe_clean_paths}" -eq 1 ]; then
    return 0
  fi

  leaf="$(basename "${path}")"
  if [ "${leaf}" != "${expected_leaf}" ]; then
    echo "Refusing unexpected cleanup basename for ${label}: ${path}" >&2
    echo "Expected leaf '${expected_leaf}'. Override with ALLOW_UNSAFE_CLEAN_PATHS=1 if intentional." >&2
    return 1
  fi

  case "${path}" in
    /dev/shm/*|/tmp/*)
      ;;
    *)
      echo "Refusing cleanup path outside /dev/shm or /tmp for ${label}: ${path}" >&2
      echo "Override with ALLOW_UNSAFE_CLEAN_PATHS=1 if intentional." >&2
      return 1
      ;;
  esac

  return 0
}

validate_cleanup_target "${IN_DIR}" "hilbert-afl-in" "AFL input dir"
validate_cleanup_target "${OUT_DIR}" "hilbert-afl-out" "AFL output dir"
validate_cleanup_target "${TMP_DIR}" "hilbert-afl-tmp" "AFL tmp dir"

echo "Stopping tmux session '${SESSION}' (if running)..."
if [ "${dry_run}" -eq 1 ]; then
  echo "DRY_RUN: would stop tmux session '${SESSION}'."
else
  tmux kill-session -t "${SESSION}" 2>/dev/null || true
fi

echo "Removing AFL work directories:"
echo "  ${IN_DIR}"
echo "  ${OUT_DIR}"
echo "  ${TMP_DIR}"
if [ "${dry_run}" -eq 1 ]; then
  echo "DRY_RUN: would remove AFL work directories."
else
  rm -rf "${IN_DIR}" "${OUT_DIR}" "${TMP_DIR}"
fi

case "${CLEAN_LIBFUZZER_SHM}" in
  1|true|TRUE|yes|YES|y|Y)
    remove_libfuzzer=1
    ;;
  0|false|FALSE|no|NO|n|N)
    remove_libfuzzer=0
    ;;
  ask|ASK|"")
    if prompt_yes_no "Also remove libFuzzer corpus/artifacts from shm?" "n"; then
      remove_libfuzzer=1
    else
      remove_libfuzzer=0
    fi
    ;;
  *)
    echo "Invalid CLEAN_LIBFUZZER_SHM='${CLEAN_LIBFUZZER_SHM}' (use ask|1|0)." >&2
    exit 1
    ;;
esac

if [ "${remove_libfuzzer}" -eq 1 ]; then
  validate_cleanup_target "${LIBFUZZER_CORPUS_DIR}" "hilbert-libfuzzer-corpus" "libFuzzer corpus dir"
  validate_cleanup_target "${LIBFUZZER_ARTIFACT_DIR}" "hilbert-libfuzzer-artifacts" "libFuzzer artifact dir"
  echo "Removing libFuzzer directories:"
  echo "  ${LIBFUZZER_CORPUS_DIR}"
  echo "  ${LIBFUZZER_ARTIFACT_DIR}"
  if [ "${dry_run}" -eq 1 ]; then
    echo "DRY_RUN: would remove libFuzzer directories."
  else
    rm -rf "${LIBFUZZER_CORPUS_DIR}" "${LIBFUZZER_ARTIFACT_DIR}"
  fi
else
  echo "Keeping libFuzzer directories:"
  echo "  ${LIBFUZZER_CORPUS_DIR}"
  echo "  ${LIBFUZZER_ARTIFACT_DIR}"
fi

echo "Restoring CPU governor via restoreEnergy.sh..."
if [ "${dry_run}" -eq 1 ]; then
  echo "DRY_RUN: would run ${SCRIPT_DIR}/restoreEnergy.sh"
else
  "${SCRIPT_DIR}/restoreEnergy.sh"
fi

echo "Cleanup complete."
