#!/usr/bin/env bash

# Shared helpers for AFL scripts.

afl_cpu_count() {
  getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4
}

afl_default_ram_root() {
  local root="/dev/shm"
  if [ ! -d "${root}" ] || [ ! -w "${root}" ] || [ ! -x "${root}" ]; then
    root="/tmp"
  fi
  printf '%s\n' "${root}"
}

afl_pick_work_dir() {
  local requested="$1"
  local leaf="$2"
  local fallback_root="${3:-/tmp}"
  local parent
  parent="$(dirname "${requested}")"
  if [ -d "${parent}" ] && [ -w "${parent}" ] && [ -x "${parent}" ]; then
    printf '%s\n' "${requested}"
  else
    printf '%s/%s\n' "${fallback_root}" "${leaf}"
  fi
}

afl_require_command() {
  local cmd="$1"
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    echo "${cmd} not found in PATH" >&2
    return 1
  fi
}

afl_ensure_target_executable() {
  local target="$1"
  local root_dir="$2"
  local build_dir="$3"
  local cc_bin="$4"
  local build_jobs="$5"
  local cmake_build_type="$6"
  local target_name
  local built_target

  if [ -x "${target}" ]; then
    printf '%s\n' "${target}"
    return 0
  fi

  target_name="$(basename "${target}")"
  case "${target_name}" in
    fuzz_pipeline_afl|fuzz_file_slice_afl)
      ;;
    *)
      echo "Target not executable: ${target}" >&2
      echo "Cannot auto-build unknown AFL target basename '${target_name}'." >&2
      return 1
      ;;
  esac

  if ! command -v "${cc_bin}" >/dev/null 2>&1; then
    echo "Compiler not found: ${cc_bin}" >&2
    return 1
  fi

  echo "Missing AFL target '${target}', building '${target_name}'..."
  CC="${cc_bin}" cmake \
    -S "${root_dir}" \
    -B "${root_dir}/${build_dir}" \
    -DCMAKE_BUILD_TYPE="${cmake_build_type}" \
    -DHILBERTVIZ_FUZZ=ON \
    -DHILBERTVIZ_FUZZ_ENGINE=afl \
    -DBUILD_TESTING=OFF \
    -DHILBERTVIZ_SANITIZERS=OFF
  cmake --build "${root_dir}/${build_dir}" -j"${build_jobs}" --target "${target_name}"

  if [ -x "${target}" ]; then
    printf '%s\n' "${target}"
    return 0
  fi

  built_target="${root_dir}/${build_dir}/fuzz/${target_name}"
  if [ -x "${built_target}" ]; then
    printf '%s\n' "${built_target}"
    return 0
  fi

  echo "Failed to build AFL target: ${target_name}" >&2
  return 1
}

afl_copy_seed_dir() {
  local src_dir="$1"
  local dst_dir="$2"
  local label="$3"
  local copied_any=0
  local file

  if [ ! -d "${src_dir}" ]; then
    return 1
  fi

  while IFS= read -r -d '' file; do
    cp -n "${file}" "${dst_dir}/" 2>/dev/null || true
    copied_any=1
  done < <(find "${src_dir}" -maxdepth 1 -type f -print0 2>/dev/null || true)

  if [ "${copied_any}" -eq 1 ]; then
    echo "Imported seed corpus from ${label}: ${src_dir}"
    return 0
  fi
  return 1
}

afl_seed_inputs() {
  local in_dir="$1"
  local root_dir="$2"
  local libfuzzer_corpus_dirs="${3:-}"
  local have_libfuzzer_corpus=0
  local dir
  local -a libfuzzer_dirs=()

  if [ -n "${libfuzzer_corpus_dirs}" ]; then
    IFS=':' read -r -a libfuzzer_dirs <<< "${libfuzzer_corpus_dirs}"
    for dir in "${libfuzzer_dirs[@]}"; do
      if [ -z "${dir}" ]; then
        continue
      fi
      if afl_copy_seed_dir "${dir}" "${in_dir}" "libFuzzer"; then
        have_libfuzzer_corpus=1
      fi
    done
    if [ "${have_libfuzzer_corpus}" -eq 0 ]; then
      echo "No libFuzzer corpus found in LIBFUZZER_CORPUS_DIRS='${libfuzzer_corpus_dirs}'."
    fi
  fi

  afl_copy_seed_dir "${root_dir}/fuzz/corpus" "${in_dir}" "local fuzz/corpus" || true

  if [ -z "$(find "${in_dir}" -maxdepth 1 -type f -print -quit 2>/dev/null)" ]; then
    printf '\x00' > "${in_dir}/seed_auto.bin"
  fi
}
