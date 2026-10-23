#!/usr/bin/env bash
# B2-R controls: a build that returns failure cannot be accepted because its
# output looks right, including settling no-op builds.
set -u -o pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NIFT="${NIFT_BIN:?set NIFT_BIN}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
NIFT_REAL="$NIFT" ONLY=crash CYCLES=0 python3 "$HERE/ordinary.py" fake="$HERE/failed_settle.py" >"$T/settle.log" 2>&1
if [ $? -eq 0 ]; then echo "FAIL failed-settle control: failed settling builds accepted"; cat "$T/settle.log"; fail=1
elif ! grep -q 'settle: build failed rc=1' "$T/settle.log"; then echo "FAIL failed-settle control: missing failed-settle reason"; tail -10 "$T/settle.log"; fail=1
else echo "PASS failed-settle control: failed settling builds rejected"; fi
NIFT_REAL="$NIFT" ONLY=basic CYCLES=0 python3 "$HERE/ordinary.py" fake="$HERE/failed_build.py" >"$T/build.log" 2>&1
if [ $? -eq 0 ]; then echo "FAIL failed-build control: failed build with correct output accepted"; cat "$T/build.log"; fail=1
elif ! grep -q 'build failed rc=1' "$T/build.log"; then echo "FAIL failed-build control: missing failed-build reason"; tail -10 "$T/build.log"; fail=1
else echo "PASS failed-build control: failed build with correct output rejected"; fi
exit $fail
