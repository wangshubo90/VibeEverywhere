#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

SENTRITS_BIN="${SENTRITS_BIN:-${REPO_ROOT}/build/sentrits}"
SENTRITS_ADMIN_HOST="${SENTRITS_ADMIN_HOST:-127.0.0.1}"
SENTRITS_ADMIN_PORT="${SENTRITS_ADMIN_PORT:-19085}"
SENTRITS_REMOTE_HOST="${SENTRITS_REMOTE_HOST:-0.0.0.0}"
SENTRITS_REMOTE_PORT="${SENTRITS_REMOTE_PORT:-19086}"
SENTRITS_SMOKE_NAME="${SENTRITS_SMOKE_NAME:-Sentrits-Smoke-Mac}"
RUNTIME_DIR="${SCRIPT_DIR}/.runtime"
PID_FILE="${RUNTIME_DIR}/smoke-host.pid"
LOG_FILE="${RUNTIME_DIR}/smoke-host.log"
DATA_DIR="${RUNTIME_DIR}/data"

if [[ ! -x "${SENTRITS_BIN}" ]]; then
  echo "missing executable: ${SENTRITS_BIN}" >&2
  echo "run: cmake --build build" >&2
  exit 1
fi

echo "Starting smoke host:"
echo "  admin: ${SENTRITS_ADMIN_HOST}:${SENTRITS_ADMIN_PORT}"
echo "  remote: ${SENTRITS_REMOTE_HOST}:${SENTRITS_REMOTE_PORT}"
echo "  name: ${SENTRITS_SMOKE_NAME}"
echo "  data: ${DATA_DIR}"
echo "  log: ${LOG_FILE}"

mkdir -p "${RUNTIME_DIR}"

if [[ -f "${PID_FILE}" ]]; then
  old_pid="$(cat "${PID_FILE}")"
  if [[ -n "${old_pid}" ]] && kill -0 "${old_pid}" >/dev/null 2>&1; then
    echo "smoke host already appears to be running with pid ${old_pid}" >&2
    echo "stop it with: kill ${old_pid}" >&2
    exit 1
  fi
  rm -f "${PID_FILE}"
fi

nohup "${SENTRITS_BIN}" serve \
  --admin-host "${SENTRITS_ADMIN_HOST}" \
  --admin-port "${SENTRITS_ADMIN_PORT}" \
  --remote-host "${SENTRITS_REMOTE_HOST}" \
  --remote-port "${SENTRITS_REMOTE_PORT}" \
  --datadir "${DATA_DIR}" \
  </dev/null >"${LOG_FILE}" 2>&1 &

server_pid="$!"
disown "${server_pid}" 2>/dev/null || true
echo "${server_pid}" >"${PID_FILE}"

ready=false
for _ in {1..50}; do
  if "${SENTRITS_BIN}" host status \
    --host "${SENTRITS_ADMIN_HOST}" \
    --port "${SENTRITS_ADMIN_PORT}" \
    --json >/dev/null 2>&1; then
    ready=true
    break
  fi
  sleep 0.1
done

if [[ "${ready}" != "true" ]]; then
  echo "smoke host did not become reachable during startup; log follows:" >&2
  sed -n '1,160p' "${LOG_FILE}" >&2
  if kill -0 "${server_pid}" >/dev/null 2>&1; then
    kill "${server_pid}" >/dev/null 2>&1 || true
  fi
  rm -f "${PID_FILE}"
  exit 1
fi

"${SENTRITS_BIN}" host set-name \
  --host "${SENTRITS_ADMIN_HOST}" \
  --port "${SENTRITS_ADMIN_PORT}" \
  "${SENTRITS_SMOKE_NAME}"

echo "Smoke host is running. Pair iOS to your Mac IP on remote port ${SENTRITS_REMOTE_PORT}."
echo "PID: ${server_pid}"
echo "Stop with: kill ${server_pid}"
