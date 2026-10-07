#!/usr/bin/env bash
# Prepared object-literal parity (v4.8). Object literals in prepared loop/body
# and template execution now construct directly instead of falling back to the
# legacy evaluator, while ordinary object-literal semantics are preserved.
# Unambiguous forms (double-quoted keys, expression members) are prepared;
# string-valued/escaped-key forms keep the legacy fallback. Results, ordering,
# duplicate-key errors and negative semantics must match ordinary execution.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) B="$NIFT";; *) B="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
ok(){ printf '%s\n' "$2" > "$t/p.f"; out=$("$B" "$t/p.f" 2>&1); [ "$out" = "$3" ] || { echo "FAIL $1: got [$out]" >&2; exit 1; }; }
err(){ printf '%s\n' "$2" > "$t/p.f"; if "$B" "$t/p.f" >"$t/o" 2>"$t/e"; then echo "FAIL $1: unexpectedly succeeded" >&2; exit 1; fi; grep -q "$3" "$t/e" || { echo "FAIL $1 missing [$3]: $(cat "$t/e")" >&2; exit 1; }; }

# Ordinary baselines (unchanged).
ok "ordinary scalar members" 'i := 5
o := {"k": i, "v": i + 1}
print(o["k"])
print(o["v"])' $'5\n6'
ok "ordinary nested object" 'x := 2
print({"a": {"b": x}}["a"]["b"])' "2"
ok "ordinary array member" 'print({"a": [1, 2, 3]}["a"][2])' "3"
ok "ordinary bool/null members" 'print({"t": true, "n": null}["t"])
print({"t": true, "n": null}["n"])' $'true\nnull'
ok "ordinary empty object" 'print({}.size())' "0"
ok "ordinary string member" 'print({"a": "x"}["a"])' "x"

# Prepared loop/while bodies must match ordinary results.
ok "prepared while object build + read" 'i := 0
out := []
while(i < 3) { out.push({"k": i, "v": i + 1}); i += 1 }
print(out.size())
print(out[1]["k"])
print(out[2]["v"])' $'3\n1\n3'
ok "prepared for body object literal" 'total := 0
for(i : [1, 2, 3]) { o := {"x": i * 10}; total += o["x"] }
print(total)' "60"
ok "prepared nested object in loop" 'i := 0
while(i < 2) { o := {"a": {"b": i}}; print(o["a"]["b"]); i += 1 }' $'0\n1'
ok "prepared object with array member" 'i := 0
while(i < 2) { o := {"a": [i, i + 1]}; print(o["a"][1]); i += 1 }' $'1\n2'
ok "prepared object arg to function" 'fn(f(o)) { return o["k"] }
out := 0
i := 0
while(i < 3) { out = f({"k": i}); i += 1 }
print(out)' "2"
ok "prepared object with string member (legacy fallback still correct)" 'i := 0
while(i < 2) { o := {"s": "hello"}; print(o["s"]); i += 1 }' $'hello\nhello'
ok "prepared object member ordering preserved" 'i := 0
while(i < 1) { o := {"a": 1, "b": 2, "c": 3}; s := ""; for((k, v) : o) { s = s + k }; print(s); i += 1 }' "abc"

# Duplicate-key and malformed semantics match ordinary.
err "ordinary duplicate key" 'print({"a": 1, "a": 2})' "object literal: duplicate object key 'a'"
err "prepared duplicate key" 'i := 0
while(i < 1) { o := {"a": 1, "a": 2}; i += 1 }' "object literal: duplicate object key 'a'"
err "ordinary non-quoted key" 'print({a: 1})' "object literal"
err "ordinary trailing comma" 'print({"a": 1,})' "object literal"

# Template (prepared) execution.
mkdir -p "$t/site/.nift" "$t/site/content" "$t/site/templates"
printf '%s' '{"config": {"content-dir": "content/", "output-dir": "public/", "default-template": "templates/template.html", "build-threads": -1}}' > "$t/site/.nift/config.json"
printf '%s' '{"tracked": [{"name": "/", "title": "t", "template": "templates/template.html"}]}' > "$t/site/.nift/tracked.json"
printf '%s\n' '<body>@content</body>' > "$t/site/templates/template.html"
cat > "$t/site/content/index.html" <<'E'
@fn(mkobj()){ o := {"k": 7, "v": 8}; return o["k"] + o["v"] }
R=$[mkobj()]
E
(cd "$t/site" && "$B" build --all >/dev/null 2>"$t/e")
grep -q 'R=15' "$t/site/public/index.html" || { echo "FAIL template object literal: $(cat "$t/site/public/index.html")" >&2; exit 1; }

echo "PASS v4.8 prepared object-literal parity"