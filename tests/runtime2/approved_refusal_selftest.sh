#!/usr/bin/env bash
# Negative controls for tests/runtime2/approved_refusal.sh (review of C83-C99, P2-2; combined-b3-b5.md
# section 89), and the Python helper's own self-test.
#   approved_refusal_selftest.sh NIFT
set -euo pipefail
NIFT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
. "$HERE/approved_refusal.sh"
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
printf 'h := set()\nm := map()\nm.set(h, 1)\nprint(m.size())\n' > "$t/key.f"
printf 'print(len("abc"))\n' > "$t/len.f"
failed=0
expect() {  # expect pass|fail NAME ARGS...
  local want=$1 name=$2; shift 2
  if approved_refusal "$@" >"$t/out" 2>&1; then got=pass; else got=fail; fi
  if [ "$got" = "$want" ]; then echo "PASS control $name: $want ($(head -c 160 "$t/out" | head -1))"
  else echo "FAIL control $name: wanted $want, got $got: $(cat "$t/out")"; failed=1; fi
}
expect pass "approved reason, legacy oracle"   "$NIFT" key collection-mutable-key D "$t/key.f" 1
expect fail "missing legacy oracle"            "$NIFT" key collection-mutable-key D "$t/key.f"
expect fail "wrong legacy oracle"              "$NIFT" key collection-mutable-key D "$t/key.f" 2
expect fail "PENDING-OWNER reason"             "$NIFT" len callable:len D "$t/len.f" ""
expect fail "reason without an inventory row"  "$NIFT" key no-such-reason:at-all D "$t/key.f" 1
expect fail "another reason than the refusal"  "$NIFT" key callable:timer D "$t/key.f" 1
python3 -B "$HERE/approved_refusal.py" self-test --nift "$NIFT" || failed=1
[ "$failed" = 0 ] || { echo "FAIL approved_refusal self-test"; exit 1; }
echo "PASS approved_refusal self-test (shell and Python)"
