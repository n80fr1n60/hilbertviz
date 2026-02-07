#!/usr/bin/env bash
set -euo pipefail

CPU_SYS="/sys/devices/system/cpu"
TARGET_GOV="performance"

if [ ! -d "${CPU_SYS}" ]; then
  echo "CPU sysfs path not found: ${CPU_SYS}" >&2
  exit 1
fi

shopt -s nullglob
gov_files=("${CPU_SYS}"/cpu*/cpufreq/scaling_governor)

if [ "${#gov_files[@]}" -eq 0 ]; then
  echo "No scaling_governor files found under ${CPU_SYS}. Skipping."
  exit 0
fi

echo "Current CPU governors:"
cat "${gov_files[@]}" | sort -u

all_target=1
for f in "${gov_files[@]}"; do
  cur="$(cat "${f}")"
  if [ "${cur}" != "${TARGET_GOV}" ]; then
    all_target=0
    break
  fi
done

if [ "${all_target}" -eq 1 ]; then
  echo "All cores already use '${TARGET_GOV}'."
  exit 0
fi

echo "Switching all CPU governors to '${TARGET_GOV}' (requires sudo)..."
echo "${TARGET_GOV}" | sudo tee "${gov_files[@]}" >/dev/null

echo "CPU governors after update:"
cat "${gov_files[@]}" | sort -u
