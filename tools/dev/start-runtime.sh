#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

cd "${CORE_ROOT}"
if [[ -x "./build-mac-core/sentrits" ]]; then
    exec ./build-mac-core/sentrits serve
fi

echo "sentrits binary not found in ./build-mac-core. Build the project first." >&2
exit 1
