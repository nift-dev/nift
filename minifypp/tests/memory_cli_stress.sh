#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=${MINIFY_BIN:-$ROOT/minify}
ROUNDS=${1:-20}
COUNT=${2:-70}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/minifypp-memory-cli.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
formats=(html css js jsx json xml svg)
for ((round=0; round<ROUNDS; ++round)); do
  rm -rf "$TMP/work" && mkdir -p "$TMP/work"
  files=()
  for ((i=0; i<COUNT; ++i)); do
    ext=${formats[$((i % ${#formats[@]}))]}
    f="$TMP/work/f$i.$ext"
    case "$ext" in
      html) printf '<div class="x">  hello %s <!--gone--> </div>\n' "$i" >"$f";;
      css) printf '.x%s { color : red ; margin : 0  10px ; }\n' "$i" >"$f";;
      js) printf 'const  x%s = %s ; // gone\n' "$i" "$i" >"$f";;
      jsx) printf 'const  x%s = <div title="a  b">{%s}</div>;\n' "$i" "$i" >"$f";;
      json) printf '{ "i" : %s, "ok" : true }\n' "$i" >"$f";;
      xml) printf '<root>  <i>%s</i>  </root>\n' "$i" >"$f";;
      svg) printf '<svg viewBox="0 0 1 1"> <text>%s</text> </svg>\n' "$i" >"$f";;
    esac
    files+=("$f")
  done
  "$BIN" "${files[@]}" >/dev/null
  # Re-run as in-place over the generated outputs to exercise replace/permissions paths.
  mins=("$TMP"/work/*.min.*)
  "$BIN" --in-place "${mins[@]}" >/dev/null
  # Controlled partial batch failure must not prevent valid siblings being emitted.
  printf '{"broken":}\n' >"$TMP/work/bad.json"
  printf 'const  good = true ;\n' >"$TMP/work/good.js"
  if "$BIN" "$TMP/work/bad.json" "$TMP/work/good.js" >/dev/null 2>&1; then
    echo 'mixed valid/invalid batch unexpectedly succeeded' >&2; exit 1
  fi
  test -s "$TMP/work/good.min.js"
  test ! -e "$TMP/work/bad.min.json"
done
printf 'cli_rounds=%s files_per_round=%s\n' "$ROUNDS" "$COUNT"
