#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="${SCRIPT_DIR}/.runtime/smoke-host.pid"

if [[ ! -f "${PID_FILE}" ]]; then
  echo "no smoke host pid file found"
  exit 0
fi

pid="$(cat "${PID_FILE}")"
if [[ -z "${pid}" ]]; then
  rm -f "${PID_FILE}"
  echo "empty smoke host pid file removed"
  exit 0
fi

if kill -0 "${pid}" >/dev/null 2>&1; then
  kill "${pid}"
  echo "stopped smoke host pid ${pid}"
else
  echo "smoke host pid ${pid} is not running"
fi

rm -f "${PID_FILE}"

