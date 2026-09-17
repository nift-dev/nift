#!/usr/bin/env bash
# Surface-robustness smoke (surface audit): error diagnostics for the new
# mundane operations, immutability/purity of string/numeric/escaping helpers,
# and Unicode handling under the existing UTF-8 contract.
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
must_error(){ # name pattern script
  printf '%s\n' "$3" > e.nift
  if "$NIFT" run e.nift >/dev/null 2>&1; then echo "FAIL  $1: expected error" >&2; exit 1; fi
  local errout
  errout=$("$NIFT" run e.nift 2>&1) || true
  if printf '%s' "$errout" | grep -q "$2"; then echo "PASS  $1"; else echo "FAIL  $1: error did not mention [$2]" >&2; exit 1; fi
}

# --- error diagnostics: identify the operation and the bad argument ---
must_error html-escape-array   'value must be a string or scalar' 'print(html_escape([1,2]))'
must_error html-escape-arity   'expected one value' 'print(html_escape("a","b"))'
must_error url-encode-object   'value must be a string or scalar' 'print(url_encode({"a":1}))'
must_error floor-non-number    'unsupported for this value' 'print("x".floor())'
must_error empty-non-string    'unsupported for this value' 'print(42.empty())'
must_error abs-non-number      'unsupported for this value' 'print("x".abs())'
must_error abs-arity           'expected no arguments' 'print((-3).abs(1))'
must_error index-missing-key   "JSON object has no key 'z'" 'd := {"a":1}
print(d["z"])'
must_error index-non-string-key 'string binding, or string expression' 'd := {"a":1}
print(d[42])'

# --- immutability: helpers never mutate their source ---
check imm-trim    '  Hi  |Hi' 'x := "  Hi  "
y := x.trim()
print(x + "|" + y)'
check imm-floor   '-3.7|-4' 'x := (-3.7)
y := x.floor()
print(x.to_string() + "|" + y.to_string())'
check imm-replace 'aaa|bbb' 'x := "aaa"
y := x.replace("a","b")
print(x + "|" + y)'
check imm-lower   'AbC|abc' 'x := "AbC"
y := x.to_lower()
print(x + "|" + y)'
check imm-escape  '&lt;b&gt;|<b>' 'x := "<b>"
y := html_escape(x)
print(y + "|" + x)'
check imm-url     'a%20b|a b' 'x := "a b"
y := url_encode(x)
print(y + "|" + x)'
check imm-obj     '1|1' 'x := {"a":1}
y := x.get("a")
print(y.to_string() + "|" + x.size().to_string())'

# --- Unicode under the existing UTF-8 contract ---
check uni-length   '11' 'print("héllo wörld".length())'
check uni-contains 'true' 'print("héllo".contains("é"))'
check uni-starts   'true' 'print("héllo".starts_with("h"))'
check uni-escape   '&lt;héllo&gt;' 'print(html_escape("<héllo>"))'
check uni-url      'h%C3%A9llo%20w%C3%B6rld' 'print(url_encode("héllo wörld"))'
check uni-trim     'héllo' 'print("  héllo  ".trim())'
check uni-replace  'hello' 'print("héllo".replace("é","e"))'
check uni-split    'hé|llo' 'print("hé,llo".split(",").join("|"))'
# ASCII-only case folding is the documented contract
check uni-lower-ascii 'hÉllo' 'print("HÉLLO".to_lower())'

echo "surface robustness smoke passed"