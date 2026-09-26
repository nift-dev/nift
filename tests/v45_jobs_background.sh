#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-./nift}
out=$(printf 'sleep 0.3 &\njobs\n' | "$NIFT")
grep -Eq '\[[0-9]+\] [0-9]+' <<<"$out"
grep -Eq '\[[0-9]+\] Running .*sleep 0.3 &' <<<"$out"
