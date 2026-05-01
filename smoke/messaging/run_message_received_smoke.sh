#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

SENTRITS_BIN="${SENTRITS_BIN:-${REPO_ROOT}/build/sentrits}"
SENTRITS_ADMIN_HOST="${SENTRITS_ADMIN_HOST:-127.0.0.1}"
SENTRITS_ADMIN_PORT="${SENTRITS_ADMIN_PORT:-19185}"
SENTRITS_REMOTE_HOST="${SENTRITS_REMOTE_HOST:-127.0.0.1}"
SENTRITS_REMOTE_PORT="${SENTRITS_REMOTE_PORT:-19186}"
SMOKE_TITLE="${SMOKE_TITLE:-smoke-message-receiver}"
RUNTIME_DIR="${SCRIPT_DIR}/.runtime"
DATA_DIR="${RUNTIME_DIR}/data"
PID_FILE="${RUNTIME_DIR}/smoke-host.pid"
LOG_FILE="${RUNTIME_DIR}/smoke-host.log"
RECEIVED_FILE="${RUNTIME_DIR}/received-input.txt"

require_executable() {
  if [[ ! -x "$1" ]]; then
    echo "missing executable: $1" >&2
    exit 1
  fi
}

json_field() {
  local field="$1"
  python3 -c 'import json,sys; print(json.load(sys.stdin).get(sys.argv[1], ""))' "$field"
}

post_message() {
  local session_id="$1"
  local text="$2"
  local submit="$3"
  python3 - "${SENTRITS_ADMIN_HOST}" "${SENTRITS_ADMIN_PORT}" "${session_id}" "${text}" "${submit}" <<'PY'
import json
import sys
import urllib.request

host, port, session_id, text, submit = sys.argv[1:]
body = json.dumps({
    "kind": "text",
    "text": text,
    "submit": submit == "true",
}).encode("utf-8")
request = urllib.request.Request(
    f"http://{host}:{port}/sessions/{session_id}/messages",
    data=body,
    headers={"Content-Type": "application/json"},
    method="POST",
)
with urllib.request.urlopen(request, timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY
}

wait_for_file_contains() {
  local path="$1"
  local needle="$2"
  for _ in {1..50}; do
    if [[ -f "${path}" ]] && grep -Fq "${needle}" "${path}"; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

file_contains() {
  local path="$1"
  local needle="$2"
  [[ -f "${path}" ]] && grep -Fq "${needle}" "${path}"
}

wait_for_file_exists() {
  local path="$1"
  for _ in {1..50}; do
    if [[ -f "${path}" ]]; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

cleanup() {
  if [[ -f "${PID_FILE}" ]]; then
    local pid
    pid="$(cat "${PID_FILE}")"
    if [[ -n "${pid}" ]] && kill -0 "${pid}" >/dev/null 2>&1; then
      kill "${pid}" >/dev/null 2>&1 || true
      for _ in {1..20}; do
        if ! kill -0 "${pid}" >/dev/null 2>&1; then
          break
        fi
        sleep 0.1
      done
      if kill -0 "${pid}" >/dev/null 2>&1; then
        kill -9 "${pid}" >/dev/null 2>&1 || true
      fi
      wait "${pid}" 2>/dev/null || true
    fi
    rm -f "${PID_FILE}"
  fi
}
trap cleanup EXIT

require_executable "${SENTRITS_BIN}"
require_executable "${SCRIPT_DIR}/actor_receiver.sh"

mkdir -p "${RUNTIME_DIR}"
rm -rf "${DATA_DIR}"
rm -f "${PID_FILE}" "${LOG_FILE}" "${RECEIVED_FILE}"

echo "Starting Sentrits smoke host on ${SENTRITS_ADMIN_HOST}:${SENTRITS_ADMIN_PORT}"
"${SENTRITS_BIN}" serve \
  --admin-host "${SENTRITS_ADMIN_HOST}" \
  --admin-port "${SENTRITS_ADMIN_PORT}" \
  --remote-host "${SENTRITS_REMOTE_HOST}" \
  --remote-port "${SENTRITS_REMOTE_PORT}" \
  --datadir "${DATA_DIR}" \
  --no-discovery \
  </dev/null >"${LOG_FILE}" 2>&1 &
host_pid="$!"
echo "${host_pid}" > "${PID_FILE}"

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
  echo "smoke host did not become reachable; log follows:" >&2
  sed -n '1,160p' "${LOG_FILE}" >&2
  exit 1
fi

receiver_command="'${SCRIPT_DIR}/actor_receiver.sh' '${RECEIVED_FILE}'"
echo "Creating receiver session..."
create_json="$("${SENTRITS_BIN}" session start \
  --host "${SENTRITS_ADMIN_HOST}" \
  --port "${SENTRITS_ADMIN_PORT}" \
  --title "${SMOKE_TITLE}" \
  --workspace "${REPO_ROOT}" \
  --provider codex \
  --env-mode clean \
  --json \
  --shell-command "${receiver_command}")"

session_id="$(printf '%s' "${create_json}" | json_field sessionId)"
if [[ -z "${session_id}" ]]; then
  echo "failed to parse receiver sessionId from session start response:" >&2
  echo "${create_json}" >&2
  exit 1
fi

if ! wait_for_file_exists "${RECEIVED_FILE}"; then
  echo "receiver did not create output file; host log follows:" >&2
  sed -n '1,200p' "${LOG_FILE}" >&2
  exit 1
fi

echo "Receiver session: ${session_id}"
echo "Posting message without submit..."
partial_response="$(post_message "${session_id}" "SMOKE_MESSAGE partial-" false)"
partial_status="$(printf '%s' "${partial_response}" | json_field status)"
if [[ "${partial_status}" != "ok" ]]; then
  echo "submit:false message POST did not return ok:" >&2
  echo "${partial_response}" >&2
  exit 1
fi

sleep 0.3
if file_contains "${RECEIVED_FILE}" "SMOKE_MESSAGE partial-"; then
  echo "receiver file unexpectedly contained submit:false message before Enter" >&2
  echo "--- received file ---" >&2
  cat "${RECEIVED_FILE}" >&2
  exit 1
fi

echo "Posting message with submit..."
submit_response="$(post_message "${session_id}" "complete" true)"
submit_status="$(printf '%s' "${submit_response}" | json_field status)"
if [[ "${submit_status}" != "ok" ]]; then
  echo "submit:true message POST did not return ok:" >&2
  echo "${submit_response}" >&2
  exit 1
fi

if ! wait_for_file_contains "${RECEIVED_FILE}" "SMOKE_MESSAGE partial-complete"; then
  echo "receiver file did not contain expected message" >&2
  echo "--- received file ---" >&2
  [[ -f "${RECEIVED_FILE}" ]] && cat "${RECEIVED_FILE}" >&2
  echo "--- host log ---" >&2
  sed -n '1,200p' "${LOG_FILE}" >&2
  exit 1
fi

echo
echo "--- submit:false response ---"
echo "${partial_response}"
echo
echo "--- submit:true response ---"
echo "${submit_response}"
echo
echo "--- received file ---"
cat "${RECEIVED_FILE}"
echo
echo "Smoke passed."
