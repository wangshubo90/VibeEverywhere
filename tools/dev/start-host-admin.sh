#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

cd "${CORE_ROOT}/frontend"
exec npm run start:host-admin
