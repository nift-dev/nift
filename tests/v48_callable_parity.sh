#!/usr/bin/env bash
# First-class callable value parity under prepared execution (v4.8). A named
# function is a valid value in ordinary execution (call_it(g), f := g,
# pass(g) -> return f). Prepared loop/body and template execution must preserve
# that callable identity instead of rejecting a bare function name as "unknown
# value or malformed expression". Lambdas already travel correctly; the fix
# restores the named-function-as-value fallback in the prepared resolver.
# Invalid callable behaviour (arity, non-callable invocation, rendering a
# callable handle) is preserved.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) B="$NIFT";; *) B="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
ok(){ printf '%s\n' "$2" > "$t/p.f"; out=$("$B" "$t/p.f" 2>&1); [ "$out" = "$3" ] || { echo "FAIL $1: got [$out]" >&2; exit 1; }; }

# Ordinary baseline (callable-as-value already worked before CP-C).
ok "ordinary named callable as argument" 'fn(g()) { return "hi" }
fn(call_it(f)) { return f() }
print(call_it(g))' "hi"
ok "named function stored in variable" 'fn(g()) { return "hi" }
f := g
print(f())' "hi"
ok "named function returned as value" 'fn(g()) { return "hi" }
fn(identity_callable(f)) { return f }
h := identity_callable(g)
print(h())' "hi"
ok "pass-through multi-hop ordinary" 'fn(g()) { return "ok" }
fn(pass(f)) { return f }
fn(call(f)) { return f() }
h := pass(g)
print(call(h))' "ok"

# Prepared loop bodies must accept the same callable values.
ok "named callable as argument in for" 'fn(g()) { return "hi" }
fn(call_it(f)) { return f() }
for(i : [1, 2, 3]) { print(call_it(g)) }' $'hi\nhi\nhi'
ok "named callable as argument in while" 'i := 0
fn(g()) { return "hi" }
fn(call_it(f)) { return f() }
while(i < 3) { call_it(g); i = i + 1 }
print("done")' "done"
ok "named callable assigned inside prepared" 'fn(g()) { return "hi" }
for(i : [1]) { f := g; print(f()) }' "hi"
ok "named callable in nested prepared fn" 'fn(g()) { return "hi" }
fn(call_it(f)) { return f() }
fn(outer()) { for(i : [1]) { return call_it(g) } }
print(outer())' "hi"
ok "pass-through multi-hop in for" 'fn(g()) { return "ok" }
fn(pass(f)) { return f }
fn(call(f)) { return f() }
for(i : [1]) { h := pass(g); print(call(h)) }' "ok"

# Lambda parity: lambdas already travelled correctly through prepared bodies.
ok "lambda direct in prepared" 'for(i : [1]) { l := (x) => x * 2; print(l(21)) }' "42"
ok "lambda passed through fn in prepared" 'fn(call_it(f)) { return f(5) }
for(i : [1]) { print(call_it((x) => x * 2)) }' "10"
ok "returned lambda invoked later: ordinary" 'fn(mk()) { return (x) => x + 1 }
h := mk()
print(h(41))' "42"
ok "returned lambda invoked later: prepared" 'fn(mk()) { return (x) => x + 1 }
for(i : [1]) { h := mk(); print(h(41)) }' "42"

# Throwing callables preserve their behaviour through prepared execution.
ok "throwing callable: ordinary" 'fn(bad()) { throw error("boom", "user.call") }
fn(call_it(f)) { return f() }
if(1 == 1) { try { call_it(bad) } catch(e) { print(e.message) } }' "boom"
ok "throwing callable: prepared for" 'fn(bad()) { throw error("boom", "user.call") }
fn(call_it(f)) { return f() }
for(i : [1]) { try { call_it(bad) } catch(e) { print(e.message) } }' "boom"

# Existing shadowing semantics are preserved inside prepared bodies.
ok "callable shadowed by lambda inside prepared" 'fn(f()) { return "outer" }
fn(inner()) { f := (x) => "lambda:" + x; return f("z") }
for(i : [1]) { print(inner()) }' "lambda:z"

# Invalid callable behaviour is preserved.
err(){ printf '%s\n' "$2" > "$t/p.f"; if "$B" "$t/p.f" >"$t/o" 2>"$t/e"; then echo "FAIL $1: unexpectedly succeeded" >&2; exit 1; fi; grep -q "$3" "$t/e" || { echo "FAIL $1 missing [$3]: $(cat "$t/e")" >&2; exit 1; }; }
err "wrong arity preserved in prepared" 'fn(g()) { return "hi" }
fn(call_it(f)) { return f() }
for(i : [1]) { call_it(g, 1, 2) }' "callable argument count mismatch"
err "non-callable invocation preserved in prepared" 'x := 5
fn(call_it(f)) { return f() }
for(i : [1]) { print(call_it(x)) }' "undefined callable: f"

# Template (prepared) execution: named function passed as value through
# another function, plus a lambda stored in a template variable.
mkdir -p "$t/site/.nift" "$t/site/content" "$t/site/templates"
printf '%s' '{"config": {"content-dir": "content/", "output-dir": "public/", "default-template": "templates/template.html", "build-threads": -1}}' > "$t/site/.nift/config.json"
printf '%s' '{"tracked": [{"name": "/", "title": "t", "template": "templates/template.html"}]}' > "$t/site/.nift/tracked.json"
printf '%s\n' '<body>@content</body>' > "$t/site/templates/template.html"
cat > "$t/site/content/index.html" <<'E'
@fn(g()){ return "tpl-hi" }
@fn(pass(f)){ return f }
@fn(call_it(f)){ return f() }
$[H := pass(g)]
VIA_H=$[call_it(H)]
$[L := (x) => x + 1]
LAMBDA=$[L(1)]
E
(cd "$t/site" && "$B" build --all >/dev/null 2>"$t/e")
grep -q 'VIA_H=tpl-hi' "$t/site/public/index.html" || { echo "FAIL template callable pass-through: $(cat "$t/site/public/index.html")" >&2; exit 1; }
grep -q 'LAMBDA=2' "$t/site/public/index.html" || { echo "FAIL template lambda" >&2; exit 1; }

echo "PASS v4.8 callable parity"