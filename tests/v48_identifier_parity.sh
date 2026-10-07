#!/usr/bin/env bash
# Freeze B2 identifier precedence and route-specific callback behavior before B4.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) B="$NIFT";; *) B="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
ok(){ printf '%s\n' "$2" > "$t/p.f"; local out; out=$("$B" "$t/p.f"); [ "$out" = "$3" ] || { echo "FAIL $1: got [$out], expected [$3]" >&2; exit 1; }; }
err(){ printf '%s\n' "$2" > "$t/p.f"; if "$B" "$t/p.f" >"$t/o" 2>"$t/e"; then echo "FAIL $1: unexpectedly succeeded" >&2; exit 1; fi; grep -Fq "$3" "$t/e" || { echo "FAIL $1: $(cat "$t/e")" >&2; exit 1; }; }
ok scalar 'x := 7
print(x)
fn(id(x)) { return x }
print(id(8))' $'7\n8'
ok scopes 'x := 1
fn(local()) { x := 2; return x }
print(local())
print(x)
for(i : [3,4]) { x := i; print(x) }
print(x)
i := 0
while(i < 2) { y := i; print(y); i += 1 }
fn(nested(x)) { for(i : [1]) { while(i < 2) { return x } }; return 0 }
print(nested(9))' $'2\n1\n3\n4\n1\n0\n1\n9'
ok captures 'n := 3
f := x => x + n
print(f(2))
n = 7
print(f(2))
g := n => n
print(g(11))
print(n)' $'5\n9\n11\n7'
ok composites 'o := {"k": 4}
a := [o]
fn(id(x)) { return x }
print(id(a)[0]["k"])
m := map()
m.set("k", 6)
c := m
print(c.get("k"))' $'4\n6'
ok callables 'fn(g()) { return 1 }
g := () => 2
h := g
print(h())
print(g())
fn(pass(f)) { return f }
l := pass(x => x)
print(l(8))' $'1\n2\n8'
ok builtin_names 'print := 7
map := 8
size := 9
print(print)
print(map)
print(size)
print(true)
print(false)
print(null)' $'7\n8\n9\ntrue\nfalse\nnull'
ok receiver 'struct(P) { value := 4; fn(read()) { return value } }
p := P()
print(p.read())
print(p.value)' $'4\n4'
ok locations 'a := [[1]]
fn(id(x)) { return x }
b := id(a[0])
a.push([2])
b.push(3)
print(a[0][1])
f := () => a[0]
c := f()
c.push(4)
print(a[0][2])' $'3\n4'
ok expressions 'f := x => x
print(f(4))
g := x => { y := x + 1; return y }
print(g(4))' $'4\n5'
err callback_block 'a := [1].map(x => { return x + 1 })' 'lambda body error: unknown value or malformed expression: return x'
ok callback_async 'f := async x => x + 1
a := [1,2].map(f)
print(a[0])
print(a[1])' $'2\n3'
ok factory_timing 'n := 0
fn(mk()) { n += 1; return x => x }
a := [3,1,2].sort_by(mk())
print(a[0])
print(n)
b := [3,1,2].map(mk())
print(b[0])
print(n)
c := [].sort_by(mk())
print(n)
d := [].map(mk())
print(n)' $'1\n3\n3\n4\n4\n5'
err unresolved 'print(missing)' 'unknown value or malformed expression: missing'
err malformed 'print(x ?)' 'ternary expression'
err reserved 'return := 2' 'return'

# Lexical module/private callable resolution and returned callable values.
mkdir -p "$t/pkg/.nift/packages/demo/src"
printf '%s\n' '{"dependencies":{"demo":{"source":"./demo","ref":"local"}}}' > "$t/pkg/manifest.json"
printf '%s\n' '{"demo":{"source":"./demo","requested":"local","commit":"local"}}' > "$t/pkg/.nift/packages.lock.json"
printf '%s\n' '{"name":"demo","version":"0.1.0","entry":"src/main.f"}' > "$t/pkg/.nift/packages/demo/manifest.json"
cat > "$t/pkg/.nift/packages/demo/src/main.f" <<'F'
fn(private_id(x)) { return x }
fn(public_fn()) { f := private_id; return f(13) }
export(public_fn)
F
printf '%s\n' 'import("demo")' 'print(public_fn())' > "$t/pkg/p.f"
[ "$(cd "$t/pkg" && "$B" p.f)" = 13 ]

mkdir -p "$t/site/.nift" "$t/site/content" "$t/site/templates"
printf '%s' '{"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/main.html","build-threads":-1}}' > "$t/site/.nift/config.json"
printf '%s' '{"tracked":[{"name":"/","title":"t","template":"templates/main.html"}]}' > "$t/site/.nift/tracked.json"
printf '%s\n' '@content' > "$t/site/templates/main.html"
cat > "$t/site/content/index.html" <<'F'
$[x := 17]
X=$[x]
@fn(id(y)){ return y }
Y=$[id(x)]
@for(i : [1]){Z=$[x]}
F
(cd "$t/site" && "$B" build --all >"$t/o" 2>"$t/e")
for expected in X=17 Y=17 Z=17; do grep -Fq "$expected" "$t/site/public/index.html"; done
echo 'PASS v4.8 identifier and callback compatibility parity'
