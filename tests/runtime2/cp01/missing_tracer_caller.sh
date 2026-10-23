#!/usr/bin/env bash
# B1-R caller control: a missing tracer is an explicit unmeasured skip (77),
# never a PASS and never a cascade of missing-count failures.
set -u -o pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NIFT="${NIFT_BIN:?set NIFT_BIN}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
STRACE="$T/no-such-tracer" NIFT_BIN="$NIFT" bash "$HERE/../cp01_simplify.sh" --syscalls-only >"$T/out" 2>&1
rc=$?
if [ $rc -ne 77 ]; then echo "FAIL missing-tracer caller: exit $rc != 77"; cat "$T/out"; exit 1; fi
if grep -q 'FAIL syscalls' "$T/out" || grep -q 'PASS CP01 simplification' "$T/out"; then echo "FAIL missing-tracer caller: fabricated PASS or missing-count failure"; cat "$T/out"; exit 1; fi
echo "PASS missing-tracer caller: explicitly unmeasured (77)"
