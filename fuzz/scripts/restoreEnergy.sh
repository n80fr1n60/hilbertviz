#!/usr/bin/env bash
set -euo pipefail

CPU_SYS="/sys/devices/system/cpu"
TARGET_GOV="powersave"

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

echo "Restoring all CPU governors to '${TARGET_GOV}' (requires sudo)..."
echo "${TARGET_GOV}" | sudo tee "${gov_files[@]}" >/dev/null

echo "CPU governors after restore:"
cat "${gov_files[@]}" | sort -u
