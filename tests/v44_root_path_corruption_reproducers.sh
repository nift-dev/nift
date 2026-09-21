#!/usr/bin/env bash
# Permanent retained-interior-pointer corruption reproducers for the v4.4
# root+path location-reference model. Every case here was a heap-use-after-free
# under the pre-root+path aliasing-shared_ptr design; each must stay clean.
#
# Run under ASan/UBSan/LSan for the full memory certification value.
set -eu
NIFT=${NIFT:-./nift}
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

# 1. Destructuring then parent growth (classic B2 UAF).
cat >"$T/destruct.f" <<'F'
a := [[1],[2]]
[x, y] := a
a.push([3])
print(x[0])
print(y[0])
F
out=$($NIFT run "$T/destruct.f")
[ "$out" = "1
2" ] || { printf 'destruct: %s\n' "$out" >&2; exit 1; }

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
out=$($NIFT run "$T/growth.f")
[ "$out" = "1
1999" ] || { printf 'growth: %s\n' "$out" >&2; exit 1; }

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
out=$($NIFT run "$T/structarr.f")
[ "$out" = "3000" ] || { printf 'structarr: %s\n' "$out" >&2; exit 1; }

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
out=$($NIFT run "$T/mapstruct.f")
[ "$out" = "true
true" ] || { printf 'mapstruct: %s\n' "$out" >&2; exit 1; }

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
"$NIFT" build --all -r "$D" >/dev/null 2>&1 || { cd "$D" && "$NIFT" build --all >/dev/null 2>&1; } || { printf 'formap build failed\n' >&2; exit 1; }
grep -q 'SUM=6' "$D/public/index.html" || { printf 'formap: %s\n' "$(cat "$D/public/index.html")" >&2; exit 1; }

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
"$NIFT" build --all -r "$D" >/dev/null 2>&1 || { cd "$D" && "$NIFT" build --all >/dev/null 2>&1; } || { printf 'forarr build failed\n' >&2; exit 1; }
grep -q 'SUM=10' "$D/public/index.html" || { printf 'forarr: %s\n' "$(cat "$D/public/index.html")" >&2; exit 1; }

echo 'PASS v4.4 root+path corruption reproducers (destruct/growth/structarr/mapstruct/formap/forarr)'