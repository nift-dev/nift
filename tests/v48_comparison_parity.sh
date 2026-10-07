#!/usr/bin/env bash
# Prepared equality / comparison parity (v4.8). A condition evaluated directly
# by script-level if/while/@if must accept exactly the same == and != values as
# ordinary expression evaluation and as prepared (loop/function/template)
# execution — one canonical deep-equality semantics (runtime_equal). Direct
# conditions on arrays, objects, bytes, nested collections and scalars must
# agree with precomputed equality. Ordering stays numbers/strings-only: array
# and object ordering remains invalid everywhere.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) B="$NIFT";; *) B="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
ok(){ printf '%s\n' "$2" > "$t/p.f"; out=$("$B" "$t/p.f" 2>&1); [ "$out" = "$3" ] || { echo "FAIL $1: got [$out]" >&2; exit 1; }; }

# Ordinary expression equality (the canonical baseline).
ok "ordinary array equality" 'print([1, 2] == [1, 2])' "true"
ok "ordinary object equality" 'print({"a": 1} == {"a": 1})' "true"
ok "ordinary nested equality" 'print([[1,[2]],{"k":[3]}] == [[1,[2]],{"k":[3]}])' "true"
ok "ordinary bytes equality" 'print(bytes([10,20,30]) == bytes([10,20,30]))' "true"

# Direct prepared conditions must equal ordinary semantics.
ok "direct if array ==" 'if([1, 2] == [1, 2]) { print("yes") }' "yes"
ok "direct if array !=" 'if([1, 2] != [1, 3]) { print("yes") }' "yes"
ok "direct if object ==" 'if({"a": 1} == {"a": 1}) { print("yes") }' "yes"
ok "direct if object !=" 'if({"a": 1} != {"a": 2}) { print("yes") }' "yes"
ok "direct if nested ==" 'if([[1,[2]],{"k":[3]}] == [[1,[2]],{"k":[3]}]) { print("yes") }' "yes"
ok "direct if bytes ==" 'if(bytes([10,20,30]) == bytes([10,20,30])) { print("yes") }' "yes"
ok "direct if bytes !=" 'if(bytes([10,20,30]) != bytes([10,20])) { print("yes") }' "yes"
ok "direct if null ==" 'if(null == null) { print("yes") }
if(null != null) { print("no") }' "yes"
ok "direct if bool ==" 'if(true == true) { print("yes") }
if(false != true) { print("yes") }' $'yes\nyes'
ok "direct if int ==" 'if(3 == 3) { print("yes") }
if(3 != 4) { print("yes") }' $'yes\nyes'
ok "direct if float ==" 'if(3.25 == 3.25) { print("yes") }
if(1.5 != 2.0) { print("yes") }' $'yes\nyes'
ok "direct if numeric coercion" 'if(3 == 3.0) { print("yes") }
print(3 == 3.0)' $'yes\ntrue'
ok "direct if string ==" 'if("abc" == "abc") { print("yes") }
if("a" != "b") { print("yes") }' $'yes\nyes'
ok "direct if mismatched types are unequal" 'if([1,2] == 1) { print("no") } else { print("neq") }
if(1 == "1") { print("no") } else { print("neq2") }' $'neq\nneq2'

# Precomputed equality and direct conditions must be interchangeable.
ok "precomputed vs direct parity" 'a := [1,2]
same := a == [1,2]
if(same != (a == [1,2])) { print("mismatch") } else { print("match") }' "match"
ok "precomputed condition then direct nested" 'same := [1,2] == [1,2]
if(same && [1,2] == [1,2]) { print("parity") }' "parity"

# Function bodies: direct conditions in return and nested call sites.
ok "function condition" 'fn(f()) {
  if([1,2] == [1,2]) { return "yes" }
  return "no"
}
print(f())' "yes"
ok "nested function condition" 'fn(f(x)) { return x == [1,2] }
fn(g()) { if(f([1,2])) { return "yes" } return "no" }
print(g())' "yes"
ok "return comparison expression" 'fn(f()) { return [1,2] == [1,2] }
print(f())' "true"

# While / prepared for-body conditions.
ok "while condition" 'i := 0
while([1,2] == [1,2] && i < 1) { i += 1 }
print(i)' "1"
ok "prepared for-body condition" 'for(i : [0]) { if([1,2] == [1,2]) { print("loop") } }' "loop"

# Truthiness of a direct boolean comparison result is preserved.
ok "direct != in while guard via negation" 'i := 0
while(i != 1) { i += 1 }
print(i)' "1"

# Invalid ordered comparisons remain invalid (not broadened to collections).
err(){ printf '%s\n' "$2" > "$t/p.f"; if "$B" "$t/p.f" >"$t/o" 2>"$t/e"; then echo "FAIL $1: unexpectedly succeeded" >&2; exit 1; fi; grep -q "$3" "$t/e" || { echo "FAIL $1 missing [$3]: $(cat "$t/e")" >&2; exit 1; }; }
err "ordinary array ordering rejected" 'print([1] < [2])' "ordering comparisons require two numbers or two strings"
err "direct if array ordering rejected" 'if([1] < [2]) { print("yes") }' "ordering comparisons require two numbers or two strings"
err "direct if object ordering rejected" 'if({"a":1} > {"b":2}) { print("yes") }' "ordering comparisons require two numbers or two strings"

# Template (prepared) execution.
mkdir -p "$t/site/.nift" "$t/site/content" "$t/site/templates"
printf '%s' '{"config": {"content-dir": "content/", "output-dir": "public/", "default-template": "templates/template.html", "build-threads": -1}}' > "$t/site/.nift/config.json"
printf '%s' '{"tracked": [{"name": "/", "title": "t", "template": "templates/template.html"}]}' > "$t/site/.nift/tracked.json"
printf '%s\n' '<body>@content</body>' > "$t/site/templates/template.html"
cat > "$t/site/content/index.html" <<'E'
@if([1,2] == [1,2]){A_EQ}
@if([1,2] != [1,3]){A_NE}
@if({"k":[1,2]} == {"k":[1,2]}){O_EQ}
@if(bytes([10,20]) == bytes([10,20])){B_EQ}
@if(null == null){N_EQ}
@if("a" == "a"){S_EQ}
E
(cd "$t/site" && "$B" build --all >/dev/null 2>"$t/e")
for mark in A_EQ A_NE O_EQ B_EQ N_EQ S_EQ; do
  grep -q "$mark" "$t/site/public/index.html" || { echo "FAIL template missing $mark" >&2; exit 1; }
done

echo "PASS v4.8 comparison parity"