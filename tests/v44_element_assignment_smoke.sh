#!/usr/bin/env bash
# v4.4: bracket-indexed element assignment. a[i] = v, m["k"] = v, grid[r][c] = v,
# and the compound forms a[i] += v. Regression for the parser gap where arrays
# and objects could be read but not assigned by index.
# Owner 2026-11-10 13:26 (combined-b3-b5.md section 81):
# - item 4: each check is its own program and reports its own FAIL line by name; the script goes
#   on to the next check and exits non-zero at the end (it used to abort silently at `out=$(...)`
#   under set -e, hiding every later check);
# - item 3: `const` is binding-only (owner Q2 option B): an element write through a const binding
#   is allowed (it prints 9); rebinding the const must still fail.
set -euo pipefail
NIFT="${NIFT:-./nift}"
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
failed=0

ok() {  # ok NAME EXPECTED PROGRAM
  printf '%s\n' "$3" > "$t/p.f"
  local rc=0 out
  out=$("$NIFT_ABS" "$t/p.f" 2>&1) || rc=$?
  if [ "$rc" -ne 0 ] || [ "$out" != "$2" ]; then
    echo "FAIL: $1 (exit $rc, got: $out)" >&2; failed=1
  fi
}
must_fail() {  # must_fail NAME PROGRAM
  printf '%s\n' "$2" > "$t/p.f"
  if "$NIFT_ABS" "$t/p.f" >/dev/null 2>&1; then echo "FAIL: $1 should error" >&2; failed=1; fi
}

ok "array element assignment" 99 'a := [1, 2, 3]
a[1] = 99
print(a[1])'
ok "object member assignment" 5 'd := {"0,0": 0}
d["0,0"] = 5
print(d["0,0"])'
ok "nested grid assignment" 7 'grid := [[1, 2], [3, 4]]
grid[1][0] = 7
print(grid[1][0])'
ok "compound element assignment" 11 'a := [1, 2, 3]
a[0] += 10
print(a[0])'
ok "element read-after-write" 4 'a := [1, 2, 3]
a[2] = a[2] + 1
print(a[2])'
must_fail "out-of-range element assignment" 'a := [1, 2]
a[5] = 9'
ok "const binding: element write allowed (binding-only const, owner Q2 B)" 9 'const a := [1, 2]
a[0] = 9
print(a[0])'
must_fail "const binding: rebinding" 'const a := [1, 2]
a = [3]'

[ "$failed" = 0 ] || { echo "FAIL element assignment" >&2; exit 1; }
echo "PASS element assignment (arrays, objects, nested, compound, errors, binding-only const)"
