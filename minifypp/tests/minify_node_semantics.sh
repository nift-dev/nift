#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if ! command -v node >/dev/null 2>&1; then
  echo "Node not installed; minifier semantic differential test skipped"
  exit 0
fi
TMP=$(mktemp -d "${TMPDIR:-/tmp}/minify-node.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/driver.cpp" <<'CPP'
#include <minify/Minify.h>
#include <iostream>
#include <sstream>
int main(){std::ostringstream s;s<<std::cin.rdbuf();std::string o,e;if(!minify::javascript(s.str(),o,e)){std::cerr<<e;return 2;}std::cout<<o;}
CPP
${CXX:-g++} -std=c++17 -O2 -I"$ROOT/include" -I"$ROOT/src" "$TMP/driver.cpp" "$ROOT/src/Minify.cpp" -o "$TMP/minjs"
run_case(){
  local name="$1" source="$2"
  printf '%s' "$source" >"$TMP/$name.js"
  "$TMP/minjs" <"$TMP/$name.js" >"$TMP/$name.min.js"
  node "$TMP/$name.js" >"$TMP/$name.orig.out" 2>"$TMP/$name.orig.err"; local a=$?
  node "$TMP/$name.min.js" >"$TMP/$name.min.out" 2>"$TMP/$name.min.err"; local b=$?
  [ "$a" -eq "$b" ]
  cmp "$TMP/$name.orig.out" "$TMP/$name.min.out"
  cmp "$TMP/$name.orig.err" "$TMP/$name.min.err"
}
run_case regex_division "const s='https://x'; console.log(/https?:\\/\\//.test(s), 12 / 3 / 2);"
run_case asi $'function f(){return\n{x:1}}; console.log(String(f()));'
run_case empty_while "let x=0; while(x++<1); console.log(x);"
run_case unicode "const π=3,café=2; console.log(π+café);"
run_case number_member "console.log(1 .toString(), 1e3 .toString());"
run_case templates 'const x=2; console.log(`a ${x > 1 ? `b ${x}` : "c"}`);'
run_case class_fields 'class A{#x=2;static y=3;get z(){return this.#x}} console.log(new A().z+A.y);'
run_case optional 'const x={a:{b:2}}; console.log(x?.a?.b ?? 0);'
run_case bigint 'console.log(String(12n+1n));'
run_case control_regex "let x=true,s='https://x'; if(x) /https?:\\/\\//.test(s) && console.log('yes');"
run_case regex_after_block "let s='https://x'; if(false){} /https?:\\/\\//.test(s)&&console.log('yes');"
run_case regex_after_function "let s='https://x'; function f(){} /https?:\\/\\//.test(s)&&console.log('yes');"
run_case regex_after_try "let s='https://x'; try{}catch(e){} /https?:\\/\\//.test(s)&&console.log('yes');"
run_case for_await_regex "async function f(){for await(const x of ['https://x']) /https?:\\/\\//.test(x)&&console.log('yes')} f();"
run_case object_division "const d=2; const n=({valueOf(){return 12}}) / 2 / d; console.log(n);"
run_case direct_object_division "const n={valueOf(){return 12}} / 2; console.log(n);"
run_case regex_after_class "class C{} /https?:\\/\\//.test('https://x')&&console.log('yes');"
run_case regex_class_charclass "class C{} /[/*}]/.test('}')&&console.log('yes');"
run_case class_expression_division "const x=class {static valueOf(){return 12}} / 2; console.log(Number.isNaN(x));"
run_case named_class_expression_division "const x=class X {static valueOf(){return 12}} / 2; console.log(Number.isNaN(x));"
run_case function_expression_division "const x=function(){} / 2; console.log(Number.isNaN(x));"
run_case async_function_expression_division "const x=async function(){} / 2; console.log(Number.isNaN(x));"
run_case catch_without_binding_regex "try{}catch{} /https?:\\/\\//.test('https://x')&&console.log('yes');"
echo "JavaScript minifier Node semantic differential test passed"
