#!/usr/bin/env bash
set -euo pipefail
bin=${1:-./nift}
out=$(printf 'printf hello\nexit\n' | "$bin" sh --no-process 2>&1 || true)
grep -q 'external process execution disabled' <<<"$out"
