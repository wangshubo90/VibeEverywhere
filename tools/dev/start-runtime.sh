#!/usr/bin/env bash
set -euo pipefail

cd /Users/shubow/dev/VibeEverywhere
exec ./build/vibe-hostd serve
