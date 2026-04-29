#!/usr/bin/env bash
set -euo pipefail

echo "BOOT marker=alpha source=stdout"
sleep 1
echo "READY marker=beta source=stdout"
sleep 1
echo "STDERR marker=gamma source=stderr" >&2
sleep 1
echo "DONE marker=delta source=stdout"

# Keep the managed capture session alive long enough for manual iOS inspection.
sleep "${SMOKE_PRODUCER_HOLD_SECONDS:-30}"

