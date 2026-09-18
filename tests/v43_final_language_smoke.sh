#!/usr/bin/env bash
set -euo pipefail
NIFT="${NIFT_BIN:-$(cd "$(dirname "$0")/.." && pwd)/nift}"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
run(){ printf '%s\n' "$1" > "$T/t.nift"; "$NIFT" run "$T/t.nift"; }
[[ "$(run 'print(type(null));print(type(true));print(type(1));print(type(1.5));print(type("x"));print(type([1]));print(type({"a":1}));print(is_null(null));print(is_number(1.5));print(is_int(1));print(is_float(1.5))')" == $'null\nbool\nint\nfloat\nstring\narray\nobject\ntrue\ntrue\ntrue\ntrue' ]]
[[ "$(run 'print(null ?? 7); print(3 ?? missing_name)')" == $'7\n3' ]]
[[ "$(run 'print(null?.x); print({"x":1}?.x); print(null?["x"]); print({"x":2}?["x"])')" == $'null\n1\nnull\n2' ]]
[[ "$(run 'print(false ?? 7); print(0 ?? 7); print("" ?? 7)')" == $'false\n0' ]]
[[ "$(run 'print(null ?? null ?? 7)')" == 7 ]]
[[ "$(run 'd := {"x":1}
print(d.x ?? 7); print(d?.x ?? 7)')" == $'1\n1' ]]
[[ "$(run 'd := null
print(d?.x ?? 7); print(d?.a?.b ?? 9)')" == $'7\n9' ]]
[[ "$(run 'd := {"a":{"b":5}}
print(d?.a?.b ?? 9)')" == 5 ]]
# safe access is null propagation only: missing keys remain errors
! run 'print({}?.missing)' >/dev/null 2>&1
! run 'print(42?.x)' >/dev/null 2>&1
! run 'post := {"title":"x"}
print(post?.titel)' >/dev/null 2>&1
# ternary has template/run/eval parity
[[ "$(run 'print(true ? "a" : "b"); print(false ? "a" : "b"); print([1,2].contains(2) ? "y" : "n")')" == $'a\nb\ny' ]]
[[ "$("$NIFT" eval '1 == 1 ? "y" : "n"')" == y ]]
# ?? / ?. / ?[] and ternary compose in templates (was broken by split_ternary)
cat > "$T/tpl.nift" <<'TEMPLATE'
A$[null ?? 7]B$[{"x":1}?.x]C$[null?.a?.b ?? 9]D$[true ? "y" : "n"]E@content
TEMPLATE
cat > "$T/tpl-build.sh" <<'SH'
set -e
mkdir -p "$T2/.nift" "$T2/content" "$T2/templates" "$T2/public"
printf '%s' '{"config":{"content-dir":"content/","content-ext":".md","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}' > "$T2/.nift/config.json"
printf '%s' '{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"}]}' > "$T2/.nift/tracked.json"
printf '# Home\n' > "$T2/content/index.md"
printf '%s' 'A$[null ?? 7]B$[{"x":1}?.x]C$[null?.a?.b ?? 9]D$[true ? "y" : "n"]E@content' > "$T2/templates/template.html"
(cd "$T2" && "$NIFT" build --all >/dev/null 2>&1)
grep -q 'A7B1C9DyE# Home' "$T2/public/index.html"
SH
T2="$T/tplproj"
NIFT="$NIFT" T2="$T2" bash "$T/tpl-build.sh"
[[ "$(run '[a,b] := [1,2]; print(a); print(b); [a,b] = [3,4]; print(a); print(b)')" == $'1\n2\n3\n4' ]]
[[ "$(run '{heading,date} := {"heading":"Nift","date":2026,"extra":1}; print(heading); print(date)')" == $'Nift\n2026' ]]
[[ "$(run '{a} := {"a":null}; print(a == null)')" == true ]]
! run '[a,b] := [1]' >/dev/null 2>&1
! run '{a,b} := {"a":1}' >/dev/null 2>&1
! run '[a,a] := [1,2]' >/dev/null 2>&1
! run '[a] := "x"' >/dev/null 2>&1
[[ "$(run 'print(range(5).join(","));print(range(2,6).join(","));print(range(10,0,-2).join(","));print(range(6,2).empty())')" == $'0,1,2,3,4\n2,3,4,5\n10,8,6,4,2\ntrue' ]]
! run 'print(range(0,5,0))' >/dev/null 2>&1
cat > "$T/e.nift" <<'NIFT'
enum Status { Draft, Published, Archived }
enum Code { OK = 200, Created, Missing = 404 }
print(Status.Draft); print(Status.Published.to_int()); print(Status.Archived.to_string())
print(type(Status.Draft)); print(is_enum(Status.Draft)); print(Code.OK.to_int()); print(Code.Created.to_int()); print(Code.Missing.to_int())
print(Status.Draft == Status.Draft); print(Status.Draft == Status.Published)
NIFT
[[ "$("$NIFT" run "$T/e.nift")" == $'Draft\n1\nArchived\nenum\ntrue\n200\n201\n404\ntrue\nfalse' ]]
cat > "$T/enum-json.nift" <<'NIFT'
enum Code { OK = 200, Missing = 404 }
o := ofstream("enum.jsonl")
o.write_val(Code.Missing)
close(o)
print(open("enum.jsonl"))
NIFT
[[ "$(cd "$T" && "$NIFT" run enum-json.nift)" == 404 ]]
cat > "$T/bad.nift" <<'NIFT'
enum Bad { A = 1, B = 1 }
NIFT
! "$NIFT" run "$T/bad.nift" >/dev/null 2>&1
echo 'final v4.3 language smoke passed'
