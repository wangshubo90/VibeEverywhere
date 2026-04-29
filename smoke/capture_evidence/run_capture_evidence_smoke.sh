#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

SENTRITS_BIN="${SENTRITS_BIN:-${REPO_ROOT}/build/sentrits}"
SENTRITS_HOST="${SENTRITS_HOST:-127.0.0.1}"
SENTRITS_PORT="${SENTRITS_PORT:-19085}"
SMOKE_PREPARE_SECONDS="${SMOKE_PREPARE_SECONDS:-30}"
SMOKE_PRODUCER_HOLD_SECONDS="${SMOKE_PRODUCER_HOLD_SECONDS:-10}"
SMOKE_ACTOR_HOLD_SECONDS="${SMOKE_ACTOR_HOLD_SECONDS:-1500}"
SMOKE_TITLE="${SMOKE_TITLE:-smoke-evidence-actor}"
RUNTIME_DIR="${SCRIPT_DIR}/.runtime"
ACTOR_ID_FILE="${RUNTIME_DIR}/actor-session-id"

require_file() {
  if [[ ! -x "$1" ]]; then
    echo "missing executable: $1" >&2
    exit 1
  fi
}

json_field() {
  local field="$1"
  python3 -c 'import json,sys; print(json.load(sys.stdin).get(sys.argv[1], ""))' "$field"
}

assert_contains() {
  local haystack="$1"
  local needle="$2"
  local label="$3"
  if [[ "$haystack" != *"$needle"* ]]; then
    echo "assertion failed: ${label} did not contain ${needle}" >&2
    echo "--- ${label} ---" >&2
    echo "$haystack" >&2
    exit 1
  fi
}

require_file "${SENTRITS_BIN}"
require_file "${SCRIPT_DIR}/producer.sh"
require_file "${SCRIPT_DIR}/actor_session.sh"

echo "Smoke host: ${SENTRITS_HOST}:${SENTRITS_PORT}"
echo "Creating PTY actor session. The session itself waits ${SMOKE_PREPARE_SECONDS}s before reading evidence."
mkdir -p "${RUNTIME_DIR}"
rm -f "${ACTOR_ID_FILE}"

actor_command="'${SCRIPT_DIR}/actor_session.sh' '${ACTOR_ID_FILE}'"
create_json="$("${SENTRITS_BIN}" session start \
  --host "${SENTRITS_HOST}" \
  --port "${SENTRITS_PORT}" \
  --title "${SMOKE_TITLE}" \
  --workspace "${REPO_ROOT}" \
  --provider codex \
  --json \
  --shell-command "${actor_command}" \
  -e "SENTRITS_BIN=${SENTRITS_BIN}" \
  -e "SENTRITS_HOST=${SENTRITS_HOST}" \
  -e "SENTRITS_PORT=${SENTRITS_PORT}" \
  -e "SMOKE_SCRIPT_DIR=${SCRIPT_DIR}" \
  -e "SMOKE_REPO_ROOT=${REPO_ROOT}" \
  -e "SMOKE_PREPARE_SECONDS=${SMOKE_PREPARE_SECONDS}" \
  -e "SMOKE_PRODUCER_HOLD_SECONDS=${SMOKE_PRODUCER_HOLD_SECONDS}" \
  -e "SMOKE_ACTOR_HOLD_SECONDS=${SMOKE_ACTOR_HOLD_SECONDS}")"

actor_session_id="$(printf '%s' "${create_json}" | json_field sessionId)"
if [[ -z "${actor_session_id}" ]]; then
  echo "failed to parse actor sessionId from session start response:" >&2
  echo "${create_json}" >&2
  exit 1
fi

printf '%s' "${actor_session_id}" > "${ACTOR_ID_FILE}"

echo "Actor PTY session: ${actor_session_id}"
echo "Attach/open this session from iOS now."
echo "The actor session will start capture and read evidence after ${SMOKE_PREPARE_SECONDS}s."
echo "Expected iOS result: Activity shows Evidence observed from actor ${actor_session_id}."
