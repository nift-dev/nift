#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if ! command -v node >/dev/null 2>&1; then
  echo "Node not installed; JavaScript scope semantic gate skipped"
  exit 0
fi
TMP=$(mktemp -d "${TMPDIR:-/tmp}/minify-scope.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
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
  printf '%s' "$source" >"$TMP/$name.js"
  "$TMP/minjs" <"$TMP/$name.js" >"$TMP/$name.min.js"
  node "$TMP/$name.js" >"$TMP/$name.orig.out" 2>"$TMP/$name.orig.err"
  node "$TMP/$name.min.js" >"$TMP/$name.min.out" 2>"$TMP/$name.min.err"
  cmp "$TMP/$name.orig.out" "$TMP/$name.min.out"
  cmp "$TMP/$name.orig.err" "$TMP/$name.min.err"
}

run_case simple 'function add(longLeft,longRight){return longLeft+longRight}console.log(add(2,3));'
run_case duplicate 'function f(longName,longName){return longName}console.log(f(1,2));'
run_case property 'function f(longName){return {longName:1,value:longName}.longName}console.log(f(9));'
run_case shorthand 'function f(longName){return {longName}}console.log(f(7).longName);'
run_case method 'function f(longName){return {longName(){return 4},value:longName}}console.log(f(2).longName());'
run_case nested 'function f(longName){function g(longName){return longName+1}return g(longName)}console.log(f(3));'
run_case closure 'function f(longName){return function(){return longName}}console.log(f(5)());'
run_case arrow 'function f(longName){return ()=>longName}console.log(f(6)());'
run_case dynamic 'function f(longName){return eval("longName")}console.log(f(8));'
run_case block_scope 'function f(longName){if(1){let other=longName}return longName}console.log(f(10));'
run_case generator 'function* f(longName){yield longName}console.log(f(11).next().value);'
run_case recursion 'const f=function named(longName){return longName?named(longName-1):3};console.log(f(2));'

printf '%s' 'function add(longLeft,longRight){return longLeft+longRight}' | "$TMP/minjs" \
  | grep -F 'function add(longLeft,longRight){return longLeft+longRight}' >/dev/null

echo "JavaScript scope adversarial semantic gate passed (12 cases)"
