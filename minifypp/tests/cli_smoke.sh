#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN="$ROOT/minify"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/nift-minifier-cli.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

printf 'const  x =  1 ;\n' >"$TMP/app.js"
"$BIN" "$TMP/app.js" >/dev/null
grep -Fxq 'const x=1;' "$TMP/app.min.js"
grep -Fxq 'const  x =  1 ;' "$TMP/app.js"

printf '.x { color : red ; }\n' >"$TMP/site.css"
"$BIN" --in-place "$TMP/site.css" >/dev/null
grep -Fxq '.x{color:red;}' "$TMP/site.css"
test ! -e "$TMP/site.min.css"

printf '{ "broken": }\n' >"$TMP/bad.json"
cp "$TMP/bad.json" "$TMP/original"
if "$BIN" "$TMP/bad.json" >"$TMP/bad.log" 2>&1; then
  echo 'malformed JSON unexpectedly succeeded' >&2; exit 1
fi
cmp "$TMP/bad.json" "$TMP/original"
test ! -e "$TMP/bad.min.json"

if "$BIN" --wat "$TMP/app.js" >"$TMP/option.log" 2>&1; then
  echo 'unknown option unexpectedly succeeded' >&2; exit 1
fi
grep -Fq "unknown option '--wat'" "$TMP/option.log"

test "$($BIN --version)" = 'Minify++ 1.1.0'
echo 'Standalone minifier CLI smoke test passed'
