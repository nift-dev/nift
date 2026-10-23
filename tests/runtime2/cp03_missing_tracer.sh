#!/usr/bin/env bash
# CP03 B3 caller control: unavailable tracing is an explicit unmeasured skip.
set -u -o pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NIFT="${NIFT_BIN:?set NIFT_BIN}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
STRACE="$T/no-such-tracer" NIFT_BIN="$NIFT" bash "$HERE/cp03_simplify.sh" >"$T/out" 2>&1
rc=$?
if [ "$rc" -ne 77 ]; then echo "FAIL CP03 missing-tracer caller: exit $rc != 77"; cat "$T/out"; exit 1; fi
if grep -q 'FAIL CP03 syscalls' "$T/out" || grep -q 'PASS CP03 simplification' "$T/out"; then
  echo "FAIL CP03 missing-tracer caller: fabricated PASS or missing-count failure"; cat "$T/out"; exit 1
fi
echo "PASS CP03 missing-tracer caller: explicitly unmeasured (77)"
