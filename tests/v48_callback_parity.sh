#!/usr/bin/env bash
# B5 compatibility oracle: route differences are intentional test expectations.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) B="$NIFT";; *) B="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
ok(){ printf '%s\n' "$2" > "$t/p.f"; local out; out=$("$B" "$t/p.f"); [ "$out" = "$3" ] || { echo "FAIL $1: got [$out], expected [$3]" >&2; exit 1; }; }
err(){ printf '%s\n' "$2" > "$t/p.f"; if "$B" "$t/p.f" >"$t/o" 2>"$t/e"; then echo "FAIL $1: unexpectedly succeeded" >&2; exit 1; fi; grep -Fq "$3" "$t/e" || { echo "FAIL $1: $(cat "$t/e")" >&2; exit 1; }; }
ok shapes 'z := () => 3
f := x => x + 1
g := (x,y) => x * y
v := (x,...xs) => x + xs.size()
print(z())
print(f(2))
print(g(3,4))
print(v(5,6,7))' $'3\n3\n12\n7'
ok blocks 'f := x => { y := x + 1; if(y > 2) { return y }; return 0 }
g := () => { n := 0; while(n < 3) { n += 1 }; return n }
h := () => { x := 1 }
print(f(3))
print(g())
print(h())' $'4\n3\nnull'
ok capture_identity 'n := 2
o := {"k": 3}
m := map()
m.set("k", 4)
f := x => x + n
g := x => x + o.k
h := x => x + m.get("k")
n = 5
o["k"] = 6
m.set("k", 7)
print(f(1))
print(g(1))
print(h(1))
print(f == f)
print(f == (x => x + n))' $'6\n7\n8\ntrue\nfalse'
ok closures 'fn(mk(n)) { return x => x + n }
fn(pass(f)) { return f }
a := mk(2)
b := mk(7)
print(a(3))
print(b(3))
c := pass(a)
print(c(4))
outer := x => (y => x + y)
d := outer(10)
print(d(2))' $'5\n10\n6\n12'
ok reentrant 'fn(recur(n)) { if(n == 0) { return 0 }; return [n].map(x => recur(x - 1))[0] + 1 }
print(recur(4))
a := [1,2].map(x => [3,4].map(y => x + y)[0])
print(a.join(","))' $'4\n4,5'
ok contexts 'fn(work()) { return [1,2].map(x => x + 1).join(",") }
print(work())
for(i : [1]) { print(work()); for(j : [1]) { a := [i,j].map(x => x * 2); print(a.join(",")) } }
i := 0
while(i < 1) { print(work()); i += 1 }' $'2,3\n2,3\n2,2\n2,3'
ok apis 'print([3,1,2].sort_by(x => x + 1).join(","))
print([1,2].map(x => x * 2).join(","))
print([1,2,3].filter(x => x > 1).join(","))
print([1,2,3].reduce((a,x) => a + x, 0))
g := [1,2,3].group_by(x => x % 2)
print(g["1"].join(","))
ix := [1,2].index_by(x => x + 1)
print(ix["2"])
p := [1,2,3].partition(x => x > 1)
print(p["matched"].join(","))
print([1,1,2].unique_by(x => x + 1).join(","))
print([3,1,2].min_by(x => x + 1))
print([3,1,2].max_by(x => x + 1))
c := [1,2,3].count_by(x => x % 2)
print(c["1"])
print([1,2].any(x => x > 1))
print([1,2].all(x => x > 0))
print([1,2].find(x => x > 1))' $'1,2,3\n2,4\n2,3\n6\n1,3\n1\n2,3\n1,2\n1\n3\n2\ntrue\ntrue\n2'
ok handle_and_comparator 's := set()
s.add(3)
s.add(1)
print(s.map(x => x + 1).join(","))
a := [3,1,2]
a.sort((x,y) => x < y)
print(a.join(","))
m := map()
m.set("a", 2)
m.set("b", 3)
print(m.map((k,v) => v + 1).join(","))' $'4,2\n1,2,3\n3,4'
ok reference 'a := [[1]]
f := x => x
b := f(a[0])
a.push([2])
b.push(3)
print(a[0][1])
c := () => a[0]
d := c()
d.push(4)
print(a[0][2])' $'3\n4'
ok shadowing 'n := 4
f := n => n + 1
g := x => { y := 7; return y }
print(f(2))
print(g(2))
fn(h()) { return 3 }
h := () => 9
k := h
print(k())
print(h())' $'3\n7\n3\n9'
ok async 'n := 5
f := async x => x + n
p := f(2)
n = 7
print(type(p))
print(await p)
a := [1,2].map(f)
print(a.join(","))' $'future\n7\n8,9'
ok short_circuit 'n := 0
fn(yes(x)) { n += 1; return true }
print([1,2,3].any(yes))
print(n)
print([1,2,3].find(yes))
print(n)' $'true\n1\n1\n2'
ok mutation 'n := 0
a := [1,2,3].map(x => n += x)
print(a.join(","))
print(n)' $'1,3,6\n6'
ok increment 'n := 0
f := () => ++n
print(f())
print(f())
print(n)
g := () => n--
print(g())
print(n)
h := () => --n
print(h())
print(n)' $'1\n2\n2\n2\n1\n0\n0'
ok multiple_selectors 'n := 0
fn(mk()) { n += 1; return x => x }
a := [3,1,2].sort_by(mk(), "asc", mk(), "desc")
print(a.join(","))
print(n)' $'1,2,3\n6'
ok numeric_edges 'f := (a,b) => a + b
g := (a,b) => a % b
print(f(9007199254740993, 2))
print(g(-9223372036854775808, -1))
print([1.5,2.5].map(x => x + 0.25).join(","))' $'9007199254740995\n0\n1.75,2.75'
ok dynamic_type 'n := 1
f := x => x + n
print(f(2))
n = 3
print(f(2))
g := x => x + "!"
print(g("a"))' $'3\n5\na!'
err block_callback 'a := [1].map(x => { return x + 1 })' 'lambda body error: unknown value or malformed expression: return x'
err arity 'a := [1].map((x,y) => x + y)' 'callback argument count mismatch'
err local_shadows_parameter 'g := n => { n := 7; return n }; print(g(2))' 'binding already declared in this scope: n'
err non_callable 'a := [1].map(2)' 'callback must be callable'
err selector_result 'a := [1].sort_by(x => [x])' 'key must be scalar'
err comparator_result 'a := [2,1]; a.sort((x,y) => x + y)' 'comparator must return bool'
err division 'a := [1].map(x => x / 0)' 'lambda body error: division by zero'
err overflow 'f := x => x + 1; print(f(9223372036854775807))' 'signed 64-bit integer overflow'
err nested_failure 'a := [1].map(x => [x].map(y => y / 0)[0])' 'division by zero'
ok throwing 'fn(bad(x)) { throw error("boom", "user.callback") }
try { a := [1].map(bad) } catch(e) { print(e.message) }' 'boom'

# Reuse the established identifier, module, template and timing expectations.
NIFT="$B" bash "$(dirname "$0")/v48_identifier_parity.sh"

# Identical cached code in distinct modules must retain distinct environments.
mkdir -p "$t/pkg/.nift/packages/a/src" "$t/pkg/.nift/packages/b/src"
printf '%s\n' '{"dependencies":{"a":{"source":"./a","ref":"local"},"b":{"source":"./b","ref":"local"}}}' > "$t/pkg/manifest.json"
printf '%s\n' '{"a":{"source":"./a","requested":"local","commit":"local"},"b":{"source":"./b","requested":"local","commit":"local"}}' > "$t/pkg/.nift/packages.lock.json"
for module in a b; do
    printf '{"name":"%s","version":"0.1.0","entry":"src/main.f"}\n' "$module" > "$t/pkg/.nift/packages/$module/manifest.json"
done
printf '%s\n' 'n := 13' 'fn(make_a()) { return x => x + n }' 'fn(async_a()) { return async x => x + n }' 'export(make_a)' 'export(async_a)' > "$t/pkg/.nift/packages/a/src/main.f"
printf '%s\n' 'n := 17' 'fn(make_b()) { return x => x + n }' 'fn(async_b()) { return async x => x + n }' 'export(make_b)' 'export(async_b)' > "$t/pkg/.nift/packages/b/src/main.f"
cat > "$t/pkg/p.f" <<'F'
import("a")
import("b")
n := 100
f := make_a()
g := make_b()
print(f(2))
fa := async_a()
fb := async_b()
pa := fa(3)
pb := fb(3)
print(g(2))
print(await pa)
print(await pb)
F
[ "$(cd "$t/pkg" && "$B" p.f)" = $'15\n19\n16\n20' ]

# Escaped capture hammer and worker cloning; cached plans contain no context.
ok hammer 'fn(mk(n)) { return x => x + n }
a := []
i := 0
while(i < 400) { a.push(mk(i)); i += 1 }
sum := 0
for(f : a) { sum += f(1) }
print(sum)
f := async x => x * 2
p := f(3)
q := f(4)
print(await p)
print(await q)' $'80200\n6\n8'
echo 'PASS v4.8 callback compatibility matrix'
