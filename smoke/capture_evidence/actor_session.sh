#!/usr/bin/env bash
set -euo pipefail

actor_id_file="${1:?actor id file required}"

SENTRITS_BIN="${SENTRITS_BIN:?SENTRITS_BIN required}"
SENTRITS_HOST="${SENTRITS_HOST:?SENTRITS_HOST required}"
SENTRITS_PORT="${SENTRITS_PORT:?SENTRITS_PORT required}"
SMOKE_SCRIPT_DIR="${SMOKE_SCRIPT_DIR:?SMOKE_SCRIPT_DIR required}"
SMOKE_REPO_ROOT="${SMOKE_REPO_ROOT:?SMOKE_REPO_ROOT required}"
SMOKE_PREPARE_SECONDS="${SMOKE_PREPARE_SECONDS:-15}"
SMOKE_PRODUCER_HOLD_SECONDS="${SMOKE_PRODUCER_HOLD_SECONDS:-30}"
SMOKE_ACTOR_HOLD_SECONDS="${SMOKE_ACTOR_HOLD_SECONDS:-15}"
SMOKE_CAPTURE_TITLE="${SMOKE_CAPTURE_TITLE:-smoke-capture-evidence-log}"

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

echo "Sentrits capture/evidence actor session"
echo "Host: ${SENTRITS_HOST}:${SENTRITS_PORT}"
echo "Waiting ${SMOKE_PREPARE_SECONDS}s before starting capture."
echo "Attach from iOS now; this PTY session will become the observation actor."
sleep "${SMOKE_PREPARE_SECONDS}"

if [[ ! -f "${actor_id_file}" ]]; then
  echo "missing actor id file: ${actor_id_file}" >&2
  exit 1
fi

export SENTRITS_SESSION_ID="$(cat "${actor_id_file}")"
if [[ -z "${SENTRITS_SESSION_ID}" ]]; then
  echo "empty actor session id in ${actor_id_file}" >&2
  exit 1
fi

echo "Actor session id: ${SENTRITS_SESSION_ID}"
echo "Starting managed capture session..."

create_json="$("${SENTRITS_BIN}" capture start \
  --host "${SENTRITS_HOST}" \
  --port "${SENTRITS_PORT}" \
  --title "${SMOKE_CAPTURE_TITLE}" \
  --workspace "${SMOKE_REPO_ROOT}" \
  --json \
  -e "SMOKE_PRODUCER_HOLD_SECONDS=${SMOKE_PRODUCER_HOLD_SECONDS}" \
  -- "${SMOKE_SCRIPT_DIR}/producer.sh")"

capture_session_id="$(printf '%s' "${create_json}" | json_field sessionId)"
if [[ -z "${capture_session_id}" ]]; then
  echo "failed to parse capture session id:" >&2
  echo "${create_json}" >&2
  exit 1
fi

echo "Capture session id: ${capture_session_id}"
echo "Waiting for deterministic log lines..."

tail_output=""
for _ in {1..50}; do
  tail_output="$("${SENTRITS_BIN}" evidence tail \
    --host "${SENTRITS_HOST}" \
    --port "${SENTRITS_PORT}" \
    --lines 20 \
    "${capture_session_id}")"
  if [[ "${tail_output}" == *"DONE marker=delta"* ]]; then
    break
  fi
  sleep 0.2
done

assert_contains "${tail_output}" "BOOT marker=alpha" "tail evidence"
assert_contains "${tail_output}" "READY marker=beta" "tail evidence"
assert_contains "${tail_output}" "STDERR marker=gamma" "tail evidence"
assert_contains "${tail_output}" "DONE marker=delta" "tail evidence"

echo
echo "--- tail evidence ---"
echo "${tail_output}"

echo
echo "Reading evidence as actor ${SENTRITS_SESSION_ID}; iOS should receive observation.created."
search_output="$("${SENTRITS_BIN}" evidence search \
  --host "${SENTRITS_HOST}" \
  --port "${SENTRITS_PORT}" \
  --limit 10 \
  "${capture_session_id}" \
  "marker=beta")"
assert_contains "${search_output}" "READY marker=beta" "search evidence"

echo
echo "--- search evidence ---"
echo "${search_output}"

observations_output="$("${SENTRITS_BIN}" observations list \
  --host "${SENTRITS_HOST}" \
  --port "${SENTRITS_PORT}" \
  --limit 20)"
assert_contains "${observations_output}" "${SENTRITS_SESSION_ID}" "observations"
assert_contains "${observations_output}" "${capture_session_id}" "observations"
assert_contains "${observations_output}" "search" "observations"

echo
echo "--- observations ---"
echo "${observations_output}"
echo
echo "Smoke passed. Holding this session open for ${SMOKE_ACTOR_HOLD_SECONDS}s."
sleep "${SMOKE_ACTOR_HOLD_SECONDS}"
