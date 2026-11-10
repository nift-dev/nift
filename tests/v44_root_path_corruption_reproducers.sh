#!/usr/bin/env bash
# Permanent retained-interior-pointer corruption reproducers for the v4.4
# root+path location-reference model. Every case here was a heap-use-after-free
# under the pre-root+path aliasing-shared_ptr design; each must stay clean.
#
# Run under ASan/UBSan/LSan for the full memory certification value.
set -eu
# Owner 2026-11-10 13:26, item 4 (combined-b3-b5.md section 81): every check reports its own FAIL
# line by name and the script goes on to the next one, exiting non-zero at the end; a refused or
# failing run no longer aborts silently at `out=$(...)` under set -e.
failed=0
fail() { printf 'FAIL %s\n' "$*" >&2; failed=1; }
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) : ;; *) NIFT="$(pwd)/$NIFT" ;; esac
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

# 1. Destructuring then parent growth (classic B2 UAF).
cat >"$T/destruct.f" <<'F'
a := [[1],[2]]
[x, y] := a
a.push([3])
print(x[0])
print(y[0])
F
rc=0; out=$($NIFT "$T/destruct.f" 2>&1) || rc=$?
[ "$rc" = 0 ] && [ "$out" = "1
2" ] || fail "destruct: exit $rc: $out"

# 2. Nested extraction then growth under a retained location.
cat >"$T/growth.f" <<'F'
a := [[1],[2],[3]]
b := a[0]
i := 0
while(i < 2000) {
  a.push([i])
  i = i + 1
}
print(b[0])
print(a[2002][0])
F
rc=0; out=$($NIFT "$T/growth.f" 2>&1) || rc=$?
[ "$rc" = 0 ] && [ "$out" = "1
1999" ] || fail "growth: exit $rc: $out"

# 3. Array of struct handles with heavy growth (handle identity must survive).
cat >"$T/structarr.f" <<'F'
@struct(P) { v := 0 }
pts := []
i := 0
while(i < 3000) {
  pts.push(P())
  i = i + 1
}
print(pts.size())
F
rc=0; out=$($NIFT "$T/structarr.f" 2>&1) || rc=$?
[ "$rc" = 0 ] && [ "$out" = "3000" ] || fail "structarr: exit $rc: $out"

# 4. Map of struct handles with growth; identity via == and same().
cat >"$T/mapstruct.f" <<'F'
@struct(P) { v := 0 }
m := map()
i := 0
while(i < 1000) {
  m.set(i.to_string(), P())
  i = i + 1
}
p1 := P()
m.set("k", p1)
print(m.get("k") == p1)
print(same(m.get("k"), p1))
F
rc=0; out=$($NIFT "$T/mapstruct.f" 2>&1) || rc=$?
[ "$rc" = 0 ] && [ "$out" = "true
true" ] || fail "mapstruct: exit $rc: $out"

# 5. @for over a map whose body mutates the map (template-path value binding
#    must not dangle; json_bindings_ holds owned copies). Template build route.
D="$T/site5"; mkdir -p "$D/.nift" "$D/content" "$D/templates" "$D/public"
printf '%s\n' '{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html"}}' > "$D/.nift/config.json"
printf '%s\n' '{"tracked":[{"name":"/","title":"T","template":"templates/template.html"}]}' > "$D/.nift/tracked.json"
printf 'BODY\n' > "$D/content/index.html"
cat > "$D/templates/template.html" <<'E'
@content
$[m := {"a": 1, "b": 2, "c": 3}]
$[s := 0]
@for((k, v) : m){
$[s = s + v]
$[m["d"] = 4]
}
SUM=$[s]
E
if (cd "$D" && "$NIFT" build --all >"$T/build.log" 2>&1); then
  grep -q 'SUM=6' "$D/public/index.html" || fail "formap: $(cat "$D/public/index.html")"
else fail "formap: build failed: $(cat "$T/build.log")"; fi

# 6. @for over an array whose body mutates the array.
D="$T/site6"; mkdir -p "$D/.nift" "$D/content" "$D/templates" "$D/public"
printf '%s\n' '{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html"}}' > "$D/.nift/config.json"
printf '%s\n' '{"tracked":[{"name":"/","title":"T","template":"templates/template.html"}]}' > "$D/.nift/tracked.json"
printf 'BODY\n' > "$D/content/index.html"
cat > "$D/templates/template.html" <<'E'
@content
$[a := [1, 2, 3, 4]]
$[s := 0]
@for(x : a){
$[s = s + x]
$[a.push(0)]
}
SUM=$[s]
E
if (cd "$D" && "$NIFT" build --all >"$T/build.log" 2>&1); then
  grep -q 'SUM=10' "$D/public/index.html" || fail "forarr: $(cat "$D/public/index.html")"
else fail "forarr: build failed: $(cat "$T/build.log")"; fi
# --- CP34 final adversarial battery (all must stay ASan-clean) ---

# Escaped reference through object member after the parent slot is replaced.
cat >"$T/escaped.f" <<'F'
print("start")
a := {"x": [[1],[2]]}
b := a.x
c := b[0]
e := a.x[1]
a["x"] = []
print(c[0])
print(e[0])
F
# Checkpoint 1 (combined-b3-b5.md section 95): `a["x"] = []` compiles on the VM. B5's Extraction rule
# (APPROVED 2026-11-07 15:25, compatibility.md "extracted array elements stay valid"): `c` and `e`
# are the elements themselves and survive the replacement; legacy binds locations and fails. Both
# engines are pinned exactly, and both must stay clean under the sanitizers. (The first line gives
# legacy's stdout a text for rt-diff's exact rules, tools/rt-diff/policies/b5-refsem-vm.json.)
set +e
"$NIFT" "$T/escaped.f" >"$T/out" 2>"$T/err"; rc=$?
NIFT_VM=0 "$NIFT" "$T/escaped.f" >"$T/lout" 2>"$T/lerr"; lrc=$?
set -e
if [[ $rc -ne 0 ]] || [[ "$(cat "$T/out")" != "start
1
2" ]] || [[ -s "$T/err" ]]; then fail "escaped-ref: VM exit $rc: $(cat "$T/out" "$T/err")"; fi
if [[ $lrc -ne 1 ]] || [[ "$(cat "$T/lout")" != "start" ]] || ! grep -q 'escaped.f:7:7: reference target no longer exists: c\[0\]' "$T/lerr"; then
  fail "escaped-ref: legacy exit $lrc: $(cat "$T/lout" "$T/lerr")"
fi

# Reference created inside a loop scope escapes and stays live after the scope.
cat >"$T/scope.f" <<'F'
a := [[1]]
b := a[0]
i := 0
while(i < 1) {
  c := b
  c[0] = 5
  i = i + 1
}
print(a[0][0])
F
rc=0; out=$("$NIFT" "$T/scope.f" 2>&1) || rc=$?
[ "$rc" = 0 ] && [ "$out" = "5" ] || fail "scope: exit $rc: $out"

# Heavy reallocation with composed locations retained.
cat >"$T/stress.f" <<'F'
a := []
i := 0
while(i < 20000) {
  a.push([[i], {"k": [i]}])
  i = i + 1
}
b := a[0]
c := a[0][0]
d := a[0][1]
e := a[19999]
print(c[0])
print(e[1].k[0])
F
rc=0; out=$("$NIFT" "$T/stress.f" 2>&1) || rc=$?
[ "$rc" = 0 ] && [ "$out" = "0
19999" ] || fail "stress: exit $rc: $out"
[ "$failed" = 0 ] || { echo 'FAIL v4.4 root+path corruption reproducers' >&2; exit 1; }
echo 'PASS v4.4 root+path corruption reproducers (destruct/growth/structarr/mapstruct/formap/forarr, CP34 battery)'
