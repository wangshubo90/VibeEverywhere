#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
WEB_ROOT="$(cd -- "${SCRIPT_DIR}/../../Sentrits-Web" && pwd)"

cd "${WEB_ROOT}"
exec npm run discovery-helper
