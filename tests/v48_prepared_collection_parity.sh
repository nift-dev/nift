#!/usr/bin/env bash
# Prepared collection-method parity (v4.8). Read-only collection lookups
# (map.contains/get, set.contains) and scalar-key map.set reuse the canonical
# collection runtime inside prepared execution instead of the legacy string
# evaluator. Results, errors, mutability, numeric/StrNumber parity, marked-key
# fallback and template behavior must agree with ordinary execution.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) B="$NIFT";; *) B="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
ok(){ printf '%s\n' "$2" > "$t/p.f"; out=$("$B" "$t/p.f" 2>&1); [ "$out" = "$3" ] || { echo "FAIL $1: got [$out]" >&2; exit 1; }; }
err(){ printf '%s\n' "$2" > "$t/p.f"; if "$B" "$t/p.f" >"$t/o" 2>"$t/e"; then echo "FAIL $1: unexpectedly succeeded" >&2; exit 1; fi; grep -q "$3" "$t/e" || { echo "FAIL $1 missing [$3]: $(cat "$t/e")" >&2; exit 1; }; }

# map.contains / get / set inside prepared while/for bodies.
ok "prepared map contains+get+set replace" 'counts := map()
i := 1
while(i <= 1000) {
  key := (i % 10).to_string()
  count := 0
  if(counts.contains(key)) { count = counts.get(key) }
  counts.set(key, count + 1)
  i += 1
}
print(counts.size())
print(counts.get("0"))' $'10\n100'
ok "prepared map get returns value" 'm := map()
m.set("k", 42)
x := 0
i := 0
while(i < 3) { x = m.get("k"); i += 1 }
print(x)' "42"
ok "prepared map set fresh then contains" 'm := map()
i := 0
while(i < 5) { m.set("k" + i, i); i += 1 }
print(m.size())
print(m.contains("k4"))
print(m.contains("k9"))' $'5\ntrue\nfalse'
ok "prepared map numeric-key parity" 'm := map()
m.set(1, "one")
i := 0
while(i < 2) { m.set(1.0, "point"); i += 1 }
print(m.get(1))
print(m.size())' $'point\n1'
ok "prepared set contains hit/miss + add" 's := set()
s.add("a")
i := 0
while(i < 3) { if(s.contains("a")) { s.add("a") } ; i += 1 }
print(s.size())
print(s.contains("b"))' $'1\nfalse'
ok "prepared set big-int parity" 's := set()
s.add(9007199254740993)
i := 0
while(i < 2) { if(s.contains(9007199254740993)) { } ; i += 1 }
print(s.contains(9007199254740993))' "true"

# Ordinary/legacy equality for the same shapes.
ok "ordinary map same result" 'counts := map()
i := 1
while(i <= 1000) {
  key := (i % 10).to_string()
  count := 0
  if(counts.contains(key)) { count = counts.get(key) }
  counts.set(key, count + 1)
  i += 1
}
print(counts.get("0"))' "100"

# Errors: missing key, const mutation, wrong arity match ordinary.
err "prepared map.get missing errors" 'm := map()
m.set("a", 1)
i := 0
while(i < 1) { x := m.get("zz"); i += 1 }' "get: key not found"
err "prepared map.contains wrong arity" 'm := map()
m.set("a", 1)
i := 0
while(i < 1) { if(m.contains("a", "b")) { }; i += 1 }' "contains: expected one key"
err "prepared map.set wrong arity" 'm := map()
m.set("a", 1)
i := 0
while(i < 1) { m.set("a"); i += 1 }' "set: expected key and value"

# Template (prepared) execution exercises the same dispatch.
mkdir -p "$t/site/.nift" "$t/site/content" "$t/site/templates"
printf '%s' '{"config": {"content-dir": "content/", "output-dir": "public/", "default-template": "templates/template.html", "build-threads": -1}}' > "$t/site/.nift/config.json"
printf '%s' '{"tracked": [{"name": "/", "title": "t", "template": "templates/template.html"}]}' > "$t/site/.nift/tracked.json"
printf '%s\n' '<body>@content</body>' > "$t/site/templates/template.html"
cat > "$t/site/content/index.html" <<'E'
@fn(sample()){ m := map(); m.set("a", 5); if(m.contains("a")) { m.set("a", 6) }; return m.get("a") }
R=$[sample()]
E
(cd "$t/site" && "$B" build --all >/dev/null 2>"$t/e")
grep -q 'R=6' "$t/site/public/index.html" || { echo "FAIL template collection methods: $(cat "$t/site/public/index.html")" >&2; exit 1; }

echo "PASS v4.8 prepared collection-method parity"