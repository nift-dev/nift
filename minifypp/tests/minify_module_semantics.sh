#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if ! command -v node >/dev/null 2>&1; then
  echo "Node not installed; JavaScript module semantic gate skipped"
  exit 0
fi
TMP=$(mktemp -d "${TMPDIR:-/tmp}/minify-module.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/original" "$TMP/minified"
printf '%s\n' 'export const value = 40; export default 2;' >"$TMP/original/dep.mjs"
cp "$TMP/original/dep.mjs" "$TMP/minified/dep.mjs"

cat >"$TMP/driver.cpp" <<'CPP'
#include <minify/Minify.h>
#include <iostream>
#include <sstream>
int main(){std::ostringstream s;s<<std::cin.rdbuf();std::string o,e;if(!minify::javascript(s.str(),o,e)){std::cerr<<e;return 2;}std::cout<<o;}
CPP
${CXX:-g++} -std=c++17 -O2 -I"$ROOT/include" -I"$ROOT/src" \
  "$TMP/driver.cpp" "$ROOT/src/Minify.cpp" -o "$TMP/minjs"

run_case() {
  local name="$1" source="$2"
  printf '%s' "$source" >"$TMP/original/$name.mjs"
  "$TMP/minjs" <"$TMP/original/$name.mjs" >"$TMP/minified/$name.mjs"
  node "$TMP/original/$name.mjs" >"$TMP/$name.orig.out" 2>"$TMP/$name.orig.err"
  node "$TMP/minified/$name.mjs" >"$TMP/$name.min.out" 2>"$TMP/$name.min.err"
  cmp "$TMP/$name.orig.out" "$TMP/$name.min.out"
  cmp "$TMP/$name.orig.err" "$TMP/$name.min.err"
}

run_case imports "import fallback,{value as imported}from './dep.mjs';export const answer=imported+fallback;console.log(answer);"
run_case exports "export default class Example{static value=7}export{Example as Named};console.log(Example.value);"
run_case top_level_await "const value=await Promise.resolve(9);export{value};console.log(value);"
run_case dynamic_import "const module=await import('./dep.mjs');console.log(module.value+module.default);"
run_case asi_export $'const value=1\nexport default value\nconsole.log(value)'

echo "JavaScript module semantic differential test passed (5 cases)"
