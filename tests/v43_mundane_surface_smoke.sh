#!/usr/bin/env bash
# Mundane surface gap-fill smoke: string empty, numeric abs/floor/ceil/round,
# literal-array/object indexing, and HTML text/attribute + URL-component
# escaping/encoding helpers, with template/run/eval parity and composition.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="${NIFT_BIN:-$ROOT/nift}"
TMP=$(mktemp -d); trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP"

check() { # $1=name $2=expected $3=expr
  printf 'print(%s)\n' "$3" > t.nift
  local out
  out=$("$NIFT" run t.nift 2>err) && rc=0 || rc=$?
  out=$(printf '%s' "$out" | tr '\n' ' ' | sed 's/ $//')
  if [ "$out" = "$2" ]; then echo "PASS  $1"; else echo "FAIL  $1: expected [$2] got [$out] err[$(head -1 err)]" >&2; exit 1; fi
}

check str-empty-true   'true'  '"".empty()'
check str-empty-false  'false' '"x".empty()'
check num-abs          '3'     '(-3).abs()'
check num-floor        '3'     '(3.7).floor()'
check num-ceil         '4'     '(3.7).ceil()'
check num-round-up     '4'     '(3.5).round()'
check num-round-down   '2'     '(2.4).round()'
check num-round-neg    '-3'    '(-2.5).round()'
check lit-array-index  '2'     '[1,2,3][1]'
check lit-obj-index    '1'     '{"a":1}["a"]'
check lit-nested       '3'     '[[1,2],[3,4]][1][0]'
check html-escape      '&lt;b&gt;&amp;x&lt;/b&gt;' 'html_escape("<b>&x</b>")'
check attr-escape      'a&#39;b&amp;c' 'attr_escape("a'"'"'b&c")'
check url-encode       'a%20b%2Fc%3Fd%3De%26f' 'url_encode("a b/c?d=e&f")'
check url-unicode      'caf%C3%A9%20100%25' 'url_encode("café 100%")'

# composition (full scripts)
cat > t.nift <<'NIFT'
posts := [{"title":"Alpha"},{"title":"Beta"},{"title":"Gamma"},{"title":"Delta"},{"title":"Epsilon"}]
print(posts.take(5).length())
NIFT
[ "$("$NIFT" run t.nift 2>/dev/null)" = "5" ] && echo "PASS  comp-1" || { echo "FAIL  comp-1" >&2; exit 1; }

cat > t.nift <<'NIFT'
posts := [{"title":"Alpha"},{"title":"Beta"}]
print(html_escape(posts.first().title).to_upper())
NIFT
[ "$("$NIFT" run t.nift 2>/dev/null)" = "ALPHA" ] && echo "PASS  comp-2" || { echo "FAIL  comp-2" >&2; exit 1; }

check comp-3           'false' '[1,2,3].filter(x => x > 1).empty()'

echo "mundane surface smoke passed"