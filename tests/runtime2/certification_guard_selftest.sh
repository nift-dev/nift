#!/usr/bin/env bash
# Control for the certification guard's N1 removal protection.
#  1. removing test-rt-vm -> test-runtime2-cp16-n1 makes the guard fail;
#  2. removing that edge while adding certify-targets -> test-runtime2-cp16-n1
#     must still make the guard fail (an alternate edge is not accepted).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/cp13b-guard-selftest-XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
populate() {
    local dest="$1"
    mkdir -p "$dest"
    git -C "$ROOT" archive HEAD | tar -x -C "$dest"
}
remove_edge() {
    python3 - "$1/Makefile" <<'PY'
import sys
path = sys.argv[1]
text = open(path).read()
needle = 'test-rt-vm: test-runtime2-cp16-n1\n'
if needle not in text:
    raise SystemExit('edge not found')
open(path, 'w').write(text.replace(needle, '', 1))
PY
}
add_direct_edge() {
    python3 - "$1/Makefile" <<'PY'
import sys
path = sys.argv[1]
text = open(path).read()
needle = 'certify-targets: test-all '
if needle not in text:
    raise SystemExit('certify-targets line not found')
open(path, 'w').write(text.replace(needle, 'certify-targets: test-runtime2-cp16-n1 test-all ', 1))
PY
}

populate "$TMP/removed"
remove_edge "$TMP/removed"
if bash "$TMP/removed/tests/runtime2/certification_guard.sh" >"$TMP/removed.log" 2>&1; then
    echo "FAIL guard control: removing the test-rt-vm edge still passed" >&2
    cat "$TMP/removed.log" >&2
    exit 1
fi
if ! grep -q "removal control" "$TMP/removed.log"; then
    echo "FAIL guard control: removal failure was not named" >&2
    cat "$TMP/removed.log" >&2
    exit 1
fi
echo "PASS guard control: removing the test-rt-vm edge fails certification"

populate "$TMP/alternate"
remove_edge "$TMP/alternate"
add_direct_edge "$TMP/alternate"
if bash "$TMP/alternate/tests/runtime2/certification_guard.sh" >"$TMP/alternate.log" 2>&1; then
    echo "FAIL guard control: an alternate certify-targets edge replaced the removed edge" >&2
    cat "$TMP/alternate.log" >&2
    exit 1
fi
if ! grep -Eq "removal control|alternate-edge control" "$TMP/alternate.log"; then
    echo "FAIL guard control: alternate-edge failure was not named" >&2
    cat "$TMP/alternate.log" >&2
    exit 1
fi
echo "PASS guard control: an alternate certify-targets edge is rejected"
