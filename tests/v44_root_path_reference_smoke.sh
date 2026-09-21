#!/usr/bin/env bash
set -euo pipefail
BIN="${1:-./nift}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
run_case() { local name="$1" expected="$2"; shift 2; printf '%s\n' "$@" >"$TMP/$name.f"; local got; got="$($BIN run "$TMP/$name.f")"; [[ "$got" == "$expected" ]] || { echo "$name: expected [$expected], got [$got]" >&2; exit 1; }; }
run_case nested_object $'3\n9' \
'a := {"x": [1,2]}' 'b := a["x"]' 'a["x"][0] = 3' 'print(b[0])' 'b[1] = 9' 'print(a["x"][1])'
run_case composed_path $'7\n8' \
'a := {"x": [[1]]}' 'b := a["x"]' 'c := b[0]' 'a["x"][0][0] = 7' 'print(c[0])' 'c[0] = 8' 'print(a["x"][0][0])'
run_case replacement '99' \
'a := [[1]]' 'b := a[0]' 'a[0] = [99]' 'print(b[0])'
run_case growth '1' \
'a := [[1]]' 'b := a[0]' 'i := 0' 'while(i < 1000) { a.push([i]); i++ }' 'print(b[0])'
run_case root_rebind '9' \
'a := [[1]]' 'b := a[0]' 'a = [[9]]' 'print(b[0])'
cat >"$TMP/missing.f" <<'F'
a := [[1], [2]]
b := a[1]
a.pop()
print(b)
F
set +e
"$BIN" run "$TMP/missing.f" >"$TMP/out" 2>"$TMP/err"; rc=$?
set -e
[[ $rc -ne 0 ]] || { echo 'missing-path case unexpectedly succeeded' >&2; exit 1; }
grep -q 'reference target no longer exists' "$TMP/err" || { cat "$TMP/err" >&2; exit 1; }
echo 'root+path reference smoke: PASS'
