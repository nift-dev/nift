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
lines = open(path).read().splitlines(keepends=True)
for i, line in enumerate(lines):
    if line.startswith('certify-targets: test-all '):
        lines[i] = line.replace('certify-targets: test-all ',
                                'certify-targets: test-runtime2-cp16-n1 test-all ', 1)
        break
else:
    raise SystemExit('certify-targets line not found')
open(path, 'w').writelines(lines)
PY
}

populate "$TMP/removed"
remove_edge "$TMP/removed"
if bash "$TMP/removed/tests/runtime2/certification_guard.sh" >"$TMP/removed.log" 2>&1; then
    echo "FAIL guard control: removing the test-rt-vm edge still passed" >&2
    cat "$TMP/removed.log" >&2
    exit 1
fi
if ! grep -Eq "removal control|does not reach test-runtime2-cp16-n1" "$TMP/removed.log"; then
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

# CP20 F6 and CP13B: removing any required edge must fail certification, naming it.
for edge in "test-rt-host: test-runtime2-f6-cleanup" "test-rt-host-mutants: test-runtime2-f6-mutants" "test-rt-host-sanitize: test-runtime2-f6-sanitize" \
            "test-rt-vm: test-runtime2-cp13b" "test-rt-vm: test-rt-source-map" "test-rt-vm: test-rt-error-oom" \
            "test-rt-vm-sanitize: test-rt-source-map-sanitize" "test-rt-vm-sanitize: test-rt-source-map-tsan" "test-rt-vm-sanitize: test-rt-error-oom-sanitize"; do
    name="${edge##* }"
    populate "$TMP/f6-$name"
    python3 - "$TMP/f6-$name/Makefile" "$edge" <<'PY'
import sys
path, edge = sys.argv[1], sys.argv[2] + '\n'
text = open(path).read()
if edge not in text:
    raise SystemExit('edge not found: ' + edge)
open(path, 'w').write(text.replace(edge, '', 1))
PY
    if bash "$TMP/f6-$name/tests/runtime2/certification_guard.sh" >"$TMP/f6-$name.log" 2>&1; then
        echo "FAIL guard control: removing '$edge' still passed" >&2; cat "$TMP/f6-$name.log" >&2; exit 1
    fi
    if ! grep -q "removal control.*$name" "$TMP/f6-$name.log"; then
        echo "FAIL guard control: removal of '$edge' was not named" >&2; cat "$TMP/f6-$name.log" >&2; exit 1
    fi
    echo "PASS guard control: removing '$edge' fails certification"
done

# CP13B certification tier: dropping the mutation gate from certify-targets
# must fail the required-target check.
populate "$TMP/cp13b-leaf"
python3 - "$TMP/cp13b-leaf/Makefile" <<'PY'
import sys
path = sys.argv[1]
text = open(path).read()
needle = 'certify-targets: test-runtime2-cp13b-mutants\n'
if needle not in text:
    raise SystemExit('certify edge not found')
open(path, 'w').write(text.replace(needle, '', 1))
PY
if bash "$TMP/cp13b-leaf/tests/runtime2/certification_guard.sh" >"$TMP/cp13b-leaf.log" 2>&1; then
    echo "FAIL guard control: dropping test-runtime2-cp13b-mutants from certify-targets still passed" >&2
    cat "$TMP/cp13b-leaf.log" >&2; exit 1
fi
grep -q "test-runtime2-cp13b-mutants" "$TMP/cp13b-leaf.log" || { echo "FAIL guard control: the missing CP13B leaf was not named" >&2; cat "$TMP/cp13b-leaf.log" >&2; exit 1; }
echo "PASS guard control: dropping the CP13B mutation gate fails certification"
