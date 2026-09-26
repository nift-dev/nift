#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-./nift}
out=$(printf 'sleep 0.05 &\nwait 1\njobs\nexit\n' | "$NIFT")
grep -Eq '\[1\] Done \(0\).*sleep 0.05 &' <<<"$out"
out=$(printf 'sleep 0.05 &\nfg 1\njobs\nexit\n' | "$NIFT")
grep -Eq '\[1\] Done \(0\).*sleep 0.05 &' <<<"$out"
# A stopped process group can be resumed in the background and waited.
out=$(printf "sh -c 'kill -STOP \$\$; sleep 0.03' &\nsleep 0.03\nbg 1\nwait 1\njobs\nexit\n" | "$NIFT")
grep -Eq '\[1\] Done \(0\)' <<<"$out"
