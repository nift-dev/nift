#!/usr/bin/env bash
# Scalar-key map/set index correctness (v4.8). The ordered vectors stay the
# authoritative storage; map get/contains/replacement and set contains use a
# scalar-key index for O(1) location/membership while marked/reference keys
# keep the linear structural-equality fallback. Insertion order, replacement
# position, remove repair, clear, numeric/StrNumber key parity, equality and
# iteration must all be unchanged.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) B="$NIFT";; *) B="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
ok(){ printf '%s\n' "$2" > "$t/p.f"; out=$("$B" "$t/p.f" 2>&1); [ "$out" = "$3" ] || { echo "FAIL $1: got [$out]" >&2; exit 1; }; }

# --- MAP ---------------------------------------------------------
ok "map get/replace/contains/remove" 'm := map()
m.set("a", 1); m.set("b", 2); m.set("a", 10)
print(m.get("a"))
print(m.size())
print(m.contains("b"))
print(m.contains("z"))
m.remove("a")
print(m.get("b"))
print(m.contains("a"))
print(m.size())' $'10\n2\ntrue\nfalse\n2\nfalse\n1'
ok "map string key set/get/remove" 'm := map()
m.set("k1", "v1")
print(m.get("k1"))
m.remove("k1")
print(m.contains("k1"))' $'v1\nfalse'
ok "map bool key" 'm := map()
m.set(true, "T"); m.set(false, "F")
print(m.get(true))
print(m.contains(false))
m.remove(false)
print(m.contains(false))
print(m.size())' $'T\ntrue\nfalse\n1'
ok "map numeric + float equivalence" 'm := map()
m.set(1, "one"); m.set(1.0, "point")
m.set(2, "two")
print(m.size())
print(m.get(1))
m.remove(2)
print(m.contains(2.0))' $'2\npoint\nfalse'
ok "map huge integer (StrNumber) parity" 'm := map()
m.set(9007199254740993, "huge")
print(m.get(9007199254740993))
print(m.contains(9007199254740993))
m.remove(9007199254740993)
print(m.contains(9007199254740993))' $'huge\ntrue\nfalse'
ok "map replacement preserves entry position" 'm := map()
m.set("k10", 1); m.set("k11", 2); m.set("k12", 3)
m.set("k11", 99)
out := ""
for((k, v) : m) { out = out + k + "=" + v + ";" }
print(out)' 'k10=1;k11=99;k12=3;'
ok "map remove middle then resolves remaining" 'm := map()
m.set("a", 1); m.set("b", 2); m.set("c", 3); m.set("d", 4)
m.remove("b")
print(m.contains("a"))
print(m.contains("b"))
print(m.contains("c"))
print(m.get("d"))
out := ""
for((k, v) : m) { out = out + k }
print(out)' $'true\nfalse\ntrue\n4\nacd'
ok "map remove first" 'm := map()
m.set("a", 1); m.set("b", 2); m.set("c", 3)
m.remove("a")
print(m.get("b"))
print(m.contains("a"))' $'2\nfalse'
ok "map remove last" 'm := map()
m.set("a", 1); m.set("b", 2); m.set("c", 3)
m.remove("c")
print(m.get("b"))
print(m.contains("c"))
print(m.size())' $'2\nfalse\n2'
ok "map remove/reinsert" 'm := map()
m.set("a", 1); m.remove("a"); m.set("a", 2)
print(m.get("a"))
print(m.size())' $'2\n1'
ok "map clear resets indexes" 'm := map()
m.set("a", 1); m.set("b", 2)
m.clear()
print(m.size())
print(m.contains("a"))
m.set("b", 9)
print(m.get("b"))
print(m.size())' $'0\nfalse\n9\n1'
ok "map iteration order unchanged" 'm := map()
m.set("x", 1); m.set("y", 2); m.set("z", 3)
out := ""
for((k, v) : m) { out = out + k }
print(out)' 'xyz'
ok "map equality unchanged" 'm1 := map(); m2 := map()
m1.set("a", 1); m2.set("a", 1)
print(m1 == m2)
m2.set("b", 2)
print(m1 == m2)' $'true\nfalse'
err(){ printf '%s\n' "$2" > "$t/p.f"; if "$B" "$t/p.f" >"$t/o" 2>"$t/e"; then echo "FAIL $1: unexpectedly succeeded" >&2; exit 1; fi; grep -q "$3" "$t/e" || { echo "FAIL $1 missing [$3]: $(cat "$t/e")" >&2; exit 1; }; }
err "map missing get errors" 'm := map()
m.set("a", 1)
m.get("zz")' "get: key not found"
ok "map marked/reference key fallback" 'h := set()
m := map()
m.set(h, "handle-as-key")
print(m.get(h))
print(m.contains(h))
m.remove(h)
print(m.contains(h))' $'handle-as-key\ntrue\nfalse'
ok "set marked/reference fallback (structural equality)" 'h1 := set(); h2 := set()
s := set()
s.add(h1)
print(s.contains(h1))
print(s.contains(h2))
print(s.size())' $'true\ntrue\n1'

# --- SORTED_MAP --------------------------------------------------
ok "sorted_map insert/lookup/replace/remove keep order" 'm := sorted_map()
m.set("b", 1); m.set("a", 2); m.set("c", 3)
m.set("b", 20)
m.remove("c")
m.set("d", 4)
print(m.get("a"))
print(m.get("b"))
print(m.contains("d"))
out := ""
for((k, v) : m) { out = out + k }
print(out)' $'2\n20\ntrue\nabd'
ok "sorted_map lookup after every sort" 'm := sorted_map()
m.set("z", 1); m.set("m", 2); m.set("a", 3)
print(m.get("a"))
print(m.get("z"))
m.set("b", 4)
print(m.get("b"))
m.remove("m")
print(m.get("z"))
print(m.size())' $'3\n1\n4\n1\n3'

# --- SET ---------------------------------------------------------
ok "set contains hit/miss, duplicate add, remove" 's := set()
s.add("x"); s.add("y"); s.add("x")
print(s.contains("x"))
print(s.contains("q"))
print(s.size())
s.remove("x")
print(s.contains("x"))
print(s.size())' $'true\nfalse\n2\nfalse\n1'
ok "set numeric/huge parity" 's := set()
s.add(1); s.add(1.0); s.add(9007199254740993)
print(s.size())
print(s.contains(1.0))
print(s.contains(9007199254740993))
s.remove(1)
print(s.contains(1))
print(s.size())' $'2\ntrue\ntrue\nfalse\n1'
ok "set iteration order unchanged" 's := set()
s.add("a"); s.add("b"); s.add("c")
out := ""
for(v : s) { out = out + v }
print(out)' 'abc'
ok "set clear" 's := set()
s.add("a"); s.add("b")
s.clear()
print(s.size())
print(s.contains("a"))' $'0\nfalse'
ok "sorted_set ordering" 's := sorted_set()
s.add(30); s.add(10); s.add(20)
out := ""
for(v : s) { out = out + v }
print(out)' '102030'

# --- PREPARED / TEMPLATE PARITY ----------------------------------
ok "prepared for-body map ops" 'm := map()
m.set("a", 1)
for(i : [1]) { if(m.contains("a")) { m.set("a", 2) } }
print(m.get("a"))' "2"
ok "prepared while-body set ops" 'i := 0
s := set()
s.add("s")
while(i < 2) { if(s.contains("s")) { s.add("t") }; i += 1 }
print(s.size())' "2"

# Template execution exercises the prepared collection dispatch.
mkdir -p "$t/site/.nift" "$t/site/content" "$t/site/templates"
printf '%s' '{"config": {"content-dir": "content/", "output-dir": "public/", "default-template": "templates/template.html", "build-threads": -1}}' > "$t/site/.nift/config.json"
printf '%s' '{"tracked": [{"name": "/", "title": "t", "template": "templates/template.html"}]}' > "$t/site/.nift/tracked.json"
printf '%s\n' '<body>@content</body>' > "$t/site/templates/template.html"
cat > "$t/site/content/index.html" <<'E'
@fn(fill()){ m := map(); m.set("a", 1); if(m.contains("a")) { m.set("b", 2) }; return m.get("a") + m.get("b") }
$[fill()]
E
(cd "$t/site" && "$B" build --all >/dev/null 2>"$t/e")
grep -q '3</body>' "$t/site/public/index.html" || { echo "FAIL template map index: $(cat "$t/site/public/index.html")" >&2; exit 1; }

echo "PASS v4.8 map/set index semantics"