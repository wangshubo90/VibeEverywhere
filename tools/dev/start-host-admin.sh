#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
FRONTEND_ROOT="${CORE_ROOT}/frontend"

cd "${FRONTEND_ROOT}"
exec npm run start:host-admin
