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
    # Each control is done with its copy before the next one is made: drop the
    # previous tree copies (logs stay) so the self-test needs one copy of disk.
    find "$TMP" -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} +
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

# CP20 F6, CP13B, CP14B, CP18, CP17 and CP22: removing any required edge must fail certification, naming it.
for edge in "test-rt-host: test-runtime2-f6-cleanup" "test-rt-host-mutants: test-runtime2-f6-mutants" "test-rt-host-sanitize: test-runtime2-f6-sanitize" \
            "test-rt-vm: test-runtime2-cp13b" "test-rt-vm: test-rt-source-map" "test-rt-vm: test-rt-error-oom" \
            "test-rt-vm-sanitize: test-rt-source-map-sanitize" "test-rt-vm-sanitize: test-rt-source-map-tsan" "test-rt-vm-sanitize: test-rt-error-oom-sanitize" \
            "test-rt-vm: test-runtime2-cp14b" "test-rt-vm: test-runtime2-cp14b-sort" "test-rt-vm: test-runtime2-cp14b-witnesses" \
            "test-rt-vm-sanitize: test-runtime2-cp14b-sanitize" "test-rt-vm: test-runtime2-d040-witnesses" "test-rt-vm: test-runtime2-cp14b-profile" \
            "test-embed: test-runtime2-cp18" "test-runtime2-cp18: test-runtime2-cp18-switch" "test-runtime2-cp18: test-runtime2-cp18-cow" \
            "test-embed: test-runtime2-cp18-sanitize" \
            "test-rt-vm: test-runtime2-cp17-tripwire" \
            "test-rt-host: test-rt-handles" \
            "test-rt-host: test-rt-handles-guards" \
            "test-rt-host: test-runtime2-cp17-handles" \
            "test-rt-host: test-runtime2-cp17-n2-rebuild" \
            "test-rt-host-sanitize: test-rt-handles-sanitize" \
            "test-rt-host-sanitize: test-rt-handles-tsan" \
            "test-rt-host-sanitize: test-runtime2-cp17-sanitize" \
            "test-rt-host-mutants: test-runtime2-cp17-routing-mutants" \
            "test-rt-host-mutants: test-runtime2-cp17-n2-rebuild-mutant" \
            "test-rt-host-mutants: test-runtime2-cp17-handle-mutants" \
            "test-rt-host-mutants: test-runtime2-cp17-unwind-mutants" \
            "test-rt-host: test-runtime2-cp17-handle-allocation" \
            "test-rt-vm: test-runtime2-cp22" "test-rt-vm-sanitize: test-runtime2-cp22-sanitize" \
            "test-rt-vm-sanitize: test-runtime2-cp22-tsan"; do
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
    # The guard names the child either in its edge removal control or, when the
    # child is itself a REQUIRED target, in the reachability check that runs first.
    if ! grep -qE "removal control.*$name|does not reach $name\b" "$TMP/f6-$name.log"; then
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

# CP14B certification tier: the same control for its mutation gate.
populate "$TMP/cp14b-leaf"
python3 - "$TMP/cp14b-leaf/Makefile" <<'PY'
import sys
path = sys.argv[1]
text = open(path).read()
needle = 'certify-targets: test-runtime2-cp14b-mutants\n'
if needle not in text:
    raise SystemExit('certify edge not found')
open(path, 'w').write(text.replace(needle, '', 1))
PY
if bash "$TMP/cp14b-leaf/tests/runtime2/certification_guard.sh" >"$TMP/cp14b-leaf.log" 2>&1; then
    echo "FAIL guard control: dropping test-runtime2-cp14b-mutants from certify-targets still passed" >&2
    cat "$TMP/cp14b-leaf.log" >&2; exit 1
fi
grep -q "test-runtime2-cp14b-mutants" "$TMP/cp14b-leaf.log" || { echo "FAIL guard control: the missing CP14B leaf was not named" >&2; cat "$TMP/cp14b-leaf.log" >&2; exit 1; }
echo "PASS guard control: dropping the CP14B mutation gate fails certification"

# CP22 certification tier: the same control for the coroutine mutation gate.
populate "$TMP/cp22-leaf"
python3 - "$TMP/cp22-leaf/Makefile" <<'PY'
import sys
path = sys.argv[1]
text = open(path).read()
needle = 'certify-targets: test-runtime2-cp22-mutants\n'
if needle not in text:
    raise SystemExit('certify edge not found')
open(path, 'w').write(text.replace(needle, '', 1))
PY
if bash "$TMP/cp22-leaf/tests/runtime2/certification_guard.sh" >"$TMP/cp22-leaf.log" 2>&1; then
    echo "FAIL guard control: dropping test-runtime2-cp22-mutants from certify-targets still passed" >&2
    cat "$TMP/cp22-leaf.log" >&2; exit 1
fi
grep -q "test-runtime2-cp22-mutants" "$TMP/cp22-leaf.log" || { echo "FAIL guard control: the missing CP22 leaf was not named" >&2; cat "$TMP/cp22-leaf.log" >&2; exit 1; }
echo "PASS guard control: dropping the CP22 mutation gate fails certification"
