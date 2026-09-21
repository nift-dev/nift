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

# --- CP27 adversarial expansion ---
# dynamic keys / dynamic indices / 4+ component paths
run_case dynamic_path $'5\n7\n9' \
'a := {"x": [{"y": [10, 20]}]}' 'b := a["x"][0]["y"]' 'c := a.x[0].y' 'b[0] = 5' 'print(a.x[0].y[0])' 'a["x"][0]["y"][1] = 7' 'print(b[1])' 'b[1] = 9' 'print(a.x[0].y[1])'
# 4+ component path
run_case deep_path $'2\n4' \
'a := {"l1": {"l2": {"l3": {"l4": [1, 2]}}}}' 'b := a.l1.l2.l3.l4' 'print(b[1])' 'b[1] = 4' 'print(a.l1.l2.l3.l4[1])'
# parent insertion/removal shifts the location
run_case insert_remove $'9\n1' \
'a := [[1],[2]]' 'b := a[0]' 'a.insert(0, [9])' 'print(b[0])' 'a.remove(0)' 'print(b[0])'
# append (reallocation)
run_case append_realloc $'1' \
'a := [[1]]' 'b := a[0]' 'i := 0' 'while(i < 500) { a.push([i]); i++ }' 'print(b[0])'
# reverse changes order; location tracks position
run_case reverse_tracks $'1\n3' \
'a := [[1],[2],[3]]' 'b := a[0]' 'print(b[0])' 'a.reverse()' 'print(b[0])'
# clear + path reappearance
run_case reappear $'3' \
'a := [[1]]' 'b := a[0]' 'a.clear()' 'a.push([3])' 'print(b[0])'
# missing intermediate path fails safely
cat >"$TMP/missing_mid.f" <<F
a := {"x": [[1]]}
b := a["x"]
c := b[0]
a = {}
print(c)
F
set +e
"$BIN" run "$TMP/missing_mid.f" >"$TMP/out" 2>"$TMP/err"; rc=$?
set -e
[[ $rc -ne 0 ]] || { echo 'missing-mid unexpectedly succeeded' >&2; exit 1; }
grep -q 'reference target no longer exists' "$TMP/err" || { cat "$TMP/err" >&2; exit 1; }
# root rebinding observed through composed alias
run_case composed_rebind $'1\n7\n6' \
'a := {"k": [[1],[2]]}' 'b := a.k' 'c := b[0]' 'print(c[0])' 'a = {"k": [[7],[8]]}' 'print(c[0])' 'c[0] = 6' 'print(a.k[0][0])'
# functions / closures / loops / destructuring
run_case fn_loc $'5\n5' \
'a := [[1]]' 'b := a[0]' 'f := (x) => { x[0] = x[0] + 4 }' 'f(b)' 'print(a[0][0])' 'g := (x) => x' 'h := g(b)' 'print(h[0])'
run_case destruct_loc $'1\n2' \
'a := [[1],[2]]' 'b := a[0]' 'c := a[1]' '[x] := [b]' 'print(x[0])' '[y] := [c]' 'print(y[0])'
# same() semantics
run_case same_sem $'true\nfalse\nfalse\ntrue' \
'a := [[1],[2]]' 'b := a[0]' 'c := a[0]' 'd := a[1]' 'print(same(b,c))' 'print(same(b,d))' 'e := [[1]]' 'print(same(b,e))' 'a.remove(0)' 'print(same(b,a[0]))'
# copy / deepcopy independence
run_case copy_sem $'1\n1\nfalse' \
'a := [[1]]' 'b := copy(a)' 'b[0][0] = 9' 'print(a[0][0])' 'd := deepcopy(a)' 'd[0][0] = 8' 'print(a[0][0])' 'print(same(a,b))'
# immut cannot be mutated through a location
run_case immut_loc $'5' \
'immut a := [[5]]' 'b := a[0]' 'print(b[0])'
# merge through location
run_case merge_loc $'3' \
'a := {"x": [1]}' 'b := a.x' 'a.merge({"y": 2})' 'b.push(3)' 'print(a.x[1])'
