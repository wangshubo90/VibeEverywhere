#!/usr/bin/env bash
set -euo pipefail

ports=(18085 18086 18087 3000 4200)
pids=()

for port in "${ports[@]}"; do
  while IFS= read -r pid; do
    [[ -n "${pid}" ]] && pids+=("${pid}")
  done < <(lsof -ti "tcp:${port}" 2>/dev/null || true)
done

if [[ "${#pids[@]}" -eq 0 ]]; then
  echo "No known dev processes are listening on the runtime/frontend ports."
  exit 0
fi

unique_pids=($(printf '%s\n' "${pids[@]}" | sort -u))
echo "Stopping dev processes: ${unique_pids[*]}"
kill "${unique_pids[@]}" 2>/dev/null || true

sleep 1

remaining=()
for pid in "${unique_pids[@]}"; do
  if kill -0 "${pid}" 2>/dev/null; then
    remaining+=("${pid}")
  fi
done

if [[ "${#remaining[@]}" -gt 0 ]]; then
  echo "Force stopping stubborn processes: ${remaining[*]}"
  kill -9 "${remaining[@]}" 2>/dev/null || true
fi
