#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DEV_ROOT="$(cd -- "${SCRIPT_DIR}/../../.." && pwd)"
WEB_ROOT="${DEV_ROOT}/Sentrits-Web"

cd "${WEB_ROOT}"
exec npm run discovery-helper
