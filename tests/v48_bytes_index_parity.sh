#!/usr/bin/env bash
# Bytes variable-index parity (v4.8). A bytes value must accept a computed
# (variable/expression) index everywhere an array does: top-level, assignment
# RHS, function parameters/locals/returns, function-call and method-call
# arguments, if/while/for bodies, and template (prepared) execution. Bounds and
# type errors are preserved.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) B="$NIFT";; *) B="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
# Owner 2026-11-10 13:26, item 4 (combined-b3-b5.md section 81): a failing or refused run reports
# its check by name (it used to abort silently at `out=$(...)` under set -e).
# Review of C100-C102, P2-1 (combined-b3-b5.md section 92): each failing check prints its FAIL line and the
# script goes on; it exits non-zero at the end.
failed=0
# A failing check names its VM-default refusal reason and inventory status when it was refused.
refusal_of(){ sed -n '/not compiled for the VM (/{s/.*not compiled for the VM (\([^)]*\)).*/\1/p;q;}' "$1"; }
why(){ local r; r=$(refusal_of "$1"); if [ -n "$r" ]; then echo "refused with $r (inventory status $(python3 -B "$(cd "$(dirname "$0")" && pwd)/runtime2/approved_refusal.py" status "$r"))"; else head -c 300 "$1"; fi; }
ok(){ printf '%s\n' "$2" > "$t/p.f"; local rc=0; out=$("$B" "$t/p.f" 2>&1) || rc=$?; printf '%s' "$out" > "$t/o"; [ "$rc" = 0 ] && [ "$out" = "$3" ] || { echo "FAIL $1: exit $rc, $(why "$t/o")" >&2; failed=1; return 0; }; }
err(){ printf '%s\n' "$2" > "$t/p.f"; if "$B" "$t/p.f" >"$t/o" 2>"$t/e"; then echo "FAIL $1: unexpectedly succeeded" >&2; failed=1; return 0; fi; grep -q "$3" "$t/e" || { echo "FAIL $1: stderr lacks [$3]: $(why "$t/e")" >&2; failed=1; return 0; }; }
ok "bytes variable index top-level" 'b := bytes([10,20,30]); i := 1
print(b[i])' "20"
ok "bytes variable index assignment RHS" 'b := bytes([10,20,30]); i := 1
x := b[i]
print(x)' "20"
ok "bytes index function parameter" 'fn(f(v, j)) { return v[j] }
b := bytes([10,20,30])
print(f(b, 1))' "20"
ok "bytes index function local + return" 'fn(f()) { b := bytes([10,20,30]); i := 1; return b[i] }
print(f())' "20"
ok "bytes index function-call argument" 'fn(id(v)) { return v }
b := bytes([10,20,30]); i := 1
print(id(b[i]))' "20"
ok "bytes index method-call argument (push in loop)" 'b := bytes([10,20,30]); out := []
for(i : [0,1,2]) { out.push(b[i]) }
print(out.size())' "3"
ok "bytes index if body" 'b := bytes([10,20,30]); i := 1
if(true) { x := b[i]; print(x) }' "20"
ok "bytes index while body" 'b := bytes([10,20,30]); i := 1
while(i == 1) { print(b[i]); i = i + 1 }' "20"
ok "bytes index for body" 'b := bytes([10,20,30])
for(i : [1]) { print(b[i]) }' "20"
ok "array variable index still works" 'a := [10,20,30]; i := 1
print(a[i])' "20"
ok "array expression index still works" 'a := [10,20,30]; i := 0
print(a[i+1])' "20"
ok "bytes literal index still works" 'b := bytes([10,20,30])
print(b[0])' "10"
ok "bytes expression receiver index works" 'i := 1
print(bytes([10,20,30])[i])' "20"

# Bounds / type errors preserved.

err "bytes index out of range" 'b := bytes([10,20,30]); i := 9
print(b[i])' "bytes index 9 is out of range"
err "bytes index non-integer" 'b := bytes([10,20,30]); i := "x"
print(b[i])' "bytes indices must be non-negative integers"
err "prepared negative bytes index" 'b := bytes([4]); i := 0; k := -1
while(i < 1) { print(b[k]); i += 1 }' "invalid index"
err "prepared out-of-bounds bytes index" 'b := bytes([4]); i := 0; k := 1
while(i < 1) { print(b[k]); i += 1 }' "bytes index 1 is out of range"
err "array index out of range preserved" 'a := [10,20,30]; i := 9
print(a[i])' "is out of range"

# Template (prepared) execution.
mkdir -p "$t/site/.nift" "$t/site/content" "$t/site/templates"
printf '%s' '{"config": {"content-dir": "content/", "output-dir": "public/", "default-template": "templates/template.html", "build-threads": -1}}' > "$t/site/.nift/config.json"
printf '%s' '{"tracked": [{"name": "/", "title": "t", "template": "templates/template.html"}]}' > "$t/site/.nift/tracked.json"
printf '%s\n' '<body>@content</body>' > "$t/site/templates/template.html"
cat > "$t/site/content/index.html" <<'E'
@fn(byte_at()){ b := bytes([10,20,30]); i := 1; return b[i] }
v=$[byte_at()]
E
(cd "$t/site" && "$B" build --all >/dev/null 2>"$t/e") || { echo "FAIL template site build: $(cat "$t/e")" >&2; failed=1; }
grep -q 'v=20' "$t/site/public/index.html" || { echo "FAIL template bytes variable index" >&2; failed=1; }

[ "$failed" = 0 ] || { echo "FAIL v4.8 bytes indexing parity" >&2; exit 1; }
echo "PASS v4.8 bytes indexing parity"
