#!/usr/bin/env bash
# Expression-valued object literals (post-review v4.3 composition fix):
# {"key": expr} mirrors array literals — quoted values are literal strings,
# every other value is a Nift expression evaluated left-to-right once, with
# the JSON fast path preserved for pure JSON objects. Covers enum composition,
# nesting, optionality inside values, duplicate-key/trailing-comma errors,
# serialization (enum -> backing integer), and template/run/eval parity.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="${NIFT_BIN:-$ROOT/nift}"
TMP=$(mktemp -d); trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP"

check(){ # name expected script
  printf '%s\n' "$3" > t.nift
  if out=$("$NIFT" run t.nift 2>err); then rc=0; else rc=$?; fi
  out=$(printf '%s' "$out" | tr '\n' ' ' | sed 's/ $//')
  if [ "$out" = "$2" ]; then echo "PASS  $1"; else echo "FAIL  $1: expected [$2] got [$out] err[$(head -1 err)]" >&2; exit 1; fi
}
must_error(){ # name script
  if "$NIFT" run <(printf '%s\n' "$2") >/dev/null 2>&1; then echo "FAIL  $1: expected error" >&2; exit 1; fi
  echo "PASS  $1"
}

# --- core expression values ---
check expr-binding '5' 'x := 5
print({"value": x}.value)'
check expr-arithmetic '8' 'x := 5
print({"value": x + 3}.value)'
check expr-string 'Nift|4' 'label := "Nift"
print({"name": label, "length": label.length()}.name + "|" + {"name": label, "length": label.length()}.length)'
check expr-fn-call '6' 'f := (x => x * 2)
print({"v": f(3)}.v)'
check expr-range '10' 'print({"count": range(10).size()}.count)'
check expr-collection 'A,B' 'print({"tags": ["A","B"]}.tags.join(","))'
check expr-null 'true' 'print({"missing": null}.missing == null)'

# --- enum composition ---
cat > e.nift <<'NIFT'
enum Status { Draft, Published }
post := {"title": "Hello", "status": Status.Published}
print(post.status)
print(post.status.to_int())
print(type(post.status))
NIFT
[ "$("$NIFT" run e.nift)" = $'Published\n1\nenum' ] && echo "PASS  enum-compose" || { echo "FAIL  enum-compose" >&2; exit 1; }

# --- nesting ---
check nested-expr '3|2,3' 'x := 2
obj := {"nested": {"value": x + 1}, "items": [x, x + 1]}
print(obj.nested.value + "|" + obj.items.join(","))'
check nested-enum '1' 'enum S { A, B }
obj := {"nested": {"s": S.B}}
print(obj.nested.s.to_int())'

# --- optionality inside values ---
check coalesce 'fb' 'print({"t": null ?? "fb"}.t)'
check coalesce-bind 'fb' 'x := null
print({"t": x ?? "fb"}.t)'
check safe 'fb' 'x := null
print({"t": x?.y ?? "fb"}.t)'
check safe-nonnull '5' 'd := {"a": {"b": 5}}
print({"v": d?.a?.b}.v)'
check ternary 'b' 'print({"t": (false ? "a" : "b")}.t)'

# --- evaluation once + left-to-right ---
check eval-once '1|2|2' 'n := 0
f := (() => { n++; return n })
o := {"a": f(), "b": f()}
print(o.a + "|" + o.b + "|" + n)'

# --- value types preserved ---
check types 'null|bool|int|float|string|array|object' 'obj := {"a": null, "b": true, "c": 42, "d": 1.5, "e": "s", "f": [1,2], "g": {"h": 1}}
print(type(obj.a) + "|" + type(obj.b) + "|" + type(obj.c) + "|" + type(obj.d) + "|" + type(obj.e) + "|" + type(obj.f) + "|" + type(obj.g))'

# --- malformed syntax / errors ---
must_error dup-key 'x := 1
print({"a": x, "a": 2})'
must_error trailing-comma 'x := 1
print({"a": x,})'
must_error single-quote-key "print({'a': 1})"
must_error missing-value 'print({"a": })'
must_error missing-colon 'print({"a" 1})'
must_error error-prop 'print({"a": missing_binding})'
must_error error-nested 'print({"a": {"b": missing_binding}})'

# --- JSON fast path preserved ---
check json-escaped 'line|next' 'print({"a":"line\nnext"}.a.replace("\n","|"))'
check json-bigint '123456789012345678901234567890' 'print({"a":123456789012345678901234567890}.a)'
check json-sci '10000000000' 'print({"a":1e10}.a)'
check json-dup '1' 'print({"a": 1, "b": 2}.a)'

# --- serialization: enum -> backing integer ---
cat > ser.nift <<'NIFT'
enum Status { Draft, Published }
post := {"title": "Hello", "status": Status.Published}
print(post.stringify())
print(post.prettify().length() > 0)
st := ofstream("o.jsonl")
st.write_val(post)
close(st)
print(open("o.jsonl"))
NIFT
out=$("$NIFT" run ser.nift)
[ "$(printf '%s' "$out" | sed -n '1p')" = '{"title":"Hello","status":1}' ] || { echo "FAIL  serialize-stringify: $out" >&2; exit 1; }
[ "$(printf '%s' "$out" | sed -n '3p')" = '{"title":"Hello","status":1}' ] || { echo "FAIL  serialize-writeval" >&2; exit 1; }
echo "PASS  serialize-enum-int"

# --- template parity ---
mkdir -p .nift content templates public
printf '%s' '{"config":{"content-dir":"content/","content-ext":".md","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}' > .nift/config.json
printf '%s' '{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"}]}' > .nift/tracked.json
printf '# Home\n' > content/index.md
printf '%s' 'A$[x := 5]$[{"value": x}.value]B$[{"t": null ?? "fb"}.t]C@enum(Status){ Draft, Published }D$[{"s": Status.Published}.s]E@content' > templates/template.html
"$NIFT" build --all >/dev/null 2>&1
grep -q 'A5BfbCDPublishedE# Home' public/index.html && echo "PASS  template-parity" || { echo "FAIL  template-parity" >&2; exit 1; }

# --- eval parity ---
[ "$("$NIFT" eval '{"value": 5}.value')" = 5 ] && echo "PASS  eval-parity" || { echo "FAIL  eval-parity" >&2; exit 1; }
"$NIFT" eval --json '{"t": null ?? "fb"}' | grep -q '"t": "fb"' && echo "PASS  eval-json" || { echo "FAIL  eval-json" >&2; exit 1; }

echo "object expression literals smoke passed"