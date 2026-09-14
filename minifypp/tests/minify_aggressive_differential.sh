#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if ! command -v node >/dev/null 2>&1; then echo "Node not installed; aggressive differential gate skipped"; exit 0; fi
TMP=$(mktemp -d "${TMPDIR:-/tmp}/minify-aggressive.XXXXXX"); trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/driver.cpp" <<'CPP'
#include <minify/Minify.h>
#include <iostream>
#include <sstream>
int main(){std::ostringstream s;s<<std::cin.rdbuf();std::string o,e;minify::Options x(minify::OptimizationLevel::Aggressive);if(!minify::javascript(s.str(),o,e,x)){std::cerr<<e;return 2;}std::cout<<o;}
CPP
${CXX:-g++} -std=c++17 -O2 -I"$ROOT/include" -I"$ROOT/src" "$TMP/driver.cpp" "$ROOT/src/Minify.cpp" -o "$TMP/minjs"
run_case(){
  local name="$1" source="$2"
  printf '%s' "$source" >"$TMP/$name.js"
  "$TMP/minjs" <"$TMP/$name.js" >"$TMP/$name.min.js"
  node "$TMP/$name.js" >"$TMP/$name.orig" 2>"$TMP/$name.orig.err"
  node "$TMP/$name.min.js" >"$TMP/$name.min" 2>"$TMP/$name.min.err"
  cmp "$TMP/$name.orig" "$TMP/$name.min"; cmp "$TMP/$name.orig.err" "$TMP/$name.min.err"
  if command -v terser >/dev/null 2>&1; then
    terser "$TMP/$name.js" -c -m -o "$TMP/$name.terser.js"
    node "$TMP/$name.terser.js" >"$TMP/$name.terser" 2>"$TMP/$name.terser.err"
    cmp "$TMP/$name.orig" "$TMP/$name.terser"; cmp "$TMP/$name.orig.err" "$TMP/$name.terser.err"
  fi
  if command -v esbuild >/dev/null 2>&1; then
    esbuild "$TMP/$name.js" --minify --outfile="$TMP/$name.esbuild.js" >/dev/null
    node "$TMP/$name.esbuild.js" >"$TMP/$name.esbuild" 2>"$TMP/$name.esbuild.err"
    cmp "$TMP/$name.orig" "$TMP/$name.esbuild"; cmp "$TMP/$name.orig.err" "$TMP/$name.esbuild.err"
  fi
}
run_case fold 'console.log((200+30),(9*9),(90-11));'
run_case overflow 'console.log((9007199254740991+1));'
run_case branch 'console.log(true?123:456,false?"a":"b");'
run_case branch_effect 'let n=0;console.log(true?(n++,7):8,n);'
run_case dead 'function f(){return 7;debugger;}console.log(f());'
run_case compound 'function f(longValue){longValue=longValue+2;return longValue}console.log(f(5));'
run_case declarations 'function f(){var first;var second;first=2;second=3;return first+second}console.log(f());'
run_case iife 'console.log((function(){return 42})());'
run_case negative_zero 'console.log(Object.is((0-0),-0),Object.is((-0),-0));'
run_case coercion 'const x={valueOf(){return 2}};console.log(x+x);'
run_case call_argument 'console.log(String(1+1),Array(8*8).length);'
echo "Aggressive JavaScript differential corpus passed (11 cases; available competitors included)"
