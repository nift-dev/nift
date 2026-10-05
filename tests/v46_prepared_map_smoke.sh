#!/usr/bin/env bash
# CP5 prepared collection-method + map-iteration coverage. The prepared script
# path (bare calls, collection method calls, map @for bodies) must be
# observationally identical to the legacy path for map set/get/contains/
# overwrite/iteration/destructuring and key semantics, including break/continue
# and return from the surrounding callable (the last of which was a regression
# class exposed during CP1/CP5).
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
run(){ printf '%s\n' "$1" > "$t/p.f"; "$NIFT_ABS" "$t/p.f"; }

# set / get / overwrite / numeric-key unification / typed-key distinctness
out=$(run 'm := map()
m.set("b",2); m.set("b",3)
m.set(1,"a"); m.set(1.0,"b2")
m.set("1","s")
print(m.size()); print(m.get("b")); print(m.get(1.0))')
[ "$out" = $'3\n3\nb2' ] || { echo "map set/get/overwrite/unify: $out" >&2; exit 1; }

# iteration + destructuring + continue/break
out=$(run 'm := map(); i := 0
while(i < 5) { m.set("k" + i, i); i += 1 }
t := 0
for((k,v) : m) { if(v == 2) { continue }; if(v == 4) { break }; t += v }
print(t)')
[ "$out" = '4' ] || { echo "map iterate continue/break: $out" >&2; exit 1; }

# nested loops + set during iteration of another map
out=$(run 'a := map(); b := map()
i := 0
while(i < 3) { a.set(i, i); b.set("x" + i, i * 10); i += 1 }
t := 0
for((k1,v1) : a) { for((k2,v2) : b) { t += v1 + v2 } }
print(t)')
[ "$out" = '99' ] || { echo "nested map iterate: $out" >&2; exit 1; }

# return from the surrounding callable inside a map loop
out=$(run 'fn(first(m)) { for((k,v) : m) { return k } return null }
mm := map(); mm.set("z", 9); mm.set("a", 1)
print(first(mm))')
[ "$out" = 'z' ] || { echo "return from map loop: $out" >&2; exit 1; }

# return from the surrounding callable inside an array loop (v4.6 regression)
out=$(run 'fn(first(a)) { for(x : a) { return x } return null }
print(first([10,20,30]))')
[ "$out" = '10' ] || { echo "return from array loop: $out" >&2; exit 1; }

# StrNumber key + contains hit/miss
out=$(run 'm := map(); m.set(9007199254740993, 1); m.set("s", 2)
print(m.size()); print(m.contains(9007199254740993)); print(m.contains(5))')
[ "$out" = $'2\ntrue\nfalse' ] || { echo "map strnumber/contains: $out" >&2; exit 1; }

# method-call values as statements inside a prepared map loop (mutation)
out=$(run 'm := map(); mm := map()
i := 0
while(i < 4) { mm.set(i, 0); i += 1 }
for((k,v) : mm) { m.set(k, v + 1) }
print(m.size()); print(m.get(3))')
[ "$out" = $'4\n1' ] || { echo "set inside map loop: $out" >&2; exit 1; }

echo "PASS v4.7 prepared map/collection"
