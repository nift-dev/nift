#!/usr/bin/env bash
# v4.4: bracket-indexed element assignment. a[i] = v, m["k"] = v, grid[r][c] = v,
# and the compound forms a[i] += v. Regression for the parser gap where arrays
# and objects could be read but not assigned by index.
set -euo pipefail
NIFT="${NIFT:-./nift}"
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT

cat >"$t/e.f" <<'F'
a := [1, 2, 3]
a[1] = 99
print(a[1])
d := {"0,0": 0}
d["0,0"] = 5
print(d["0,0"])
grid := [[1, 2], [3, 4]]
grid[1][0] = 7
print(grid[1][0])
a[0] += 10
print(a[0])
a[2] = a[2] + 1
print(a[2])
F
out=$("$NIFT_ABS" run "$t/e.f")
grep -qx "99" <<<"$out" || { echo "FAIL: array element assignment (got: $out)" >&2; exit 1; }
grep -qx "5" <<<"$out" || { echo "FAIL: object member assignment" >&2; exit 1; }
grep -qx "7" <<<"$out" || { echo "FAIL: nested grid assignment" >&2; exit 1; }
grep -qx "11" <<<"$out" || { echo "FAIL: compound element assignment" >&2; exit 1; }
grep -qx "4" <<<"$out" || { echo "FAIL: element read-after-write" >&2; exit 1; }

cat >"$t/err.f" <<'F'
a := [1, 2]
a[5] = 9
F
if "$NIFT_ABS" run "$t/err.f" >/dev/null 2>&1; then
  echo "FAIL: out-of-range element assignment should error" >&2; exit 1
fi

cat >"$t/const.f" <<'F'
const a := [1, 2]
a[0] = 9
F
if "$NIFT_ABS" run "$t/const.f" >/dev/null 2>&1; then
  echo "FAIL: const array element assignment should error" >&2; exit 1
fi

echo "PASS element assignment (arrays, objects, nested, compound, errors)"