#!/usr/bin/env bash
# B2 controls: an absent crash injection, failed plain recovery, and a bad
# oracle must all make ordinary.py fail.
set -u -o pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NIFT="${NIFT_BIN:?set NIFT_BIN}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
NIFT_REAL="$NIFT" ONLY=crash CYCLES=0 python3 "$HERE/ordinary.py" fake="$HERE/no_crash.py" >"$T/nocrash.log" 2>&1
if [ $? -eq 0 ]; then echo "FAIL crash control: suppressed crash injection passed"; cat "$T/nocrash.log"; fail=1; else echo "PASS crash control: suppressed crash injection rejected"; fi
if ! grep -q 'injected crash did not terminate' "$T/nocrash.log"; then echo "FAIL crash control: missing expected rejection reason"; fail=1; fi

NIFT_REAL="$NIFT" NIFT_FAKE_CRASH_MODE=fail ONLY=crash CYCLES=0 python3 "$HERE/ordinary.py" fake="$HERE/fake_crash.py" >"$T/fail.log" 2>&1
if [ $? -eq 0 ]; then echo "FAIL crash control: failed plain recovery passed"; cat "$T/fail.log"; fail=1; else echo "PASS crash control: failed plain recovery rejected"; fi
if ! grep -q 'plain recovery failed' "$T/fail.log"; then echo "FAIL crash control: missing recovery failure reason"; fail=1; fi
NIFT_REAL="$NIFT" ONLY=crash CYCLES=0 python3 "$HERE/ordinary.py" fake="$HERE/fake_crash_bad.py" >"$T/bad.log" 2>&1
if [ $? -eq 0 ]; then echo "FAIL crash control: bad oracle passed"; cat "$T/bad.log"; fail=1; else echo "PASS crash control: bad oracle rejected"; fi
if ! grep -q 'differs from clean oracle' "$T/bad.log"; then echo "FAIL crash control: missing bad-oracle reason"; fail=1; fi
exit $fail
