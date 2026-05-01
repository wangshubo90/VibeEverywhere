#!/usr/bin/env bash
set -euo pipefail

OUT="${1:-/tmp/sentrits-pty-input.txt}"

: > "$OUT"

echo "SENTRITS_SMOKE_READY $OUT" >&2

cat >> "$OUT"