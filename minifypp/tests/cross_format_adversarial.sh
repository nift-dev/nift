#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/minify-cross.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

cat >"$TMP/driver.cpp" <<'CPP'
#include "minify/Minify.h"
#include <iostream>
#include <sstream>
int main(int argc,char**argv){
  if(argc!=2)return 3; minify::Format f;
  if(!minify::format_for_extension(argv[1],f))return 4;
  std::ostringstream s;s<<std::cin.rdbuf();std::string o,e;
  if(!minify::run(f,s.str(),o,e)){std::cerr<<e;return 2;}
  std::cout<<o;
}
CPP
${CXX:-g++} -std=c++17 -O2 -I"$ROOT/include" -I"$ROOT/src" "$TMP/driver.cpp" "$ROOT/src/Minify.cpp" -o "$TMP/run"

check_idem(){
  local ext="$1" src="$2"
  printf '%s' "$src" | "$TMP/run" "$ext" >"$TMP/a"
  "$TMP/run" "$ext" <"$TMP/a" >"$TMP/b"
  cmp "$TMP/a" "$TMP/b"
}
check_fail(){
  local ext="$1" src="$2"
  if printf '%s' "$src" | "$TMP/run" "$ext" >"$TMP/a" 2>"$TMP/e"; then
    echo "malformed $ext unexpectedly succeeded" >&2; exit 1
  fi
}

check_idem .html '<template><span> a </span></template><script type="module">const r=/[<>]/;</script>'
check_idem .css '@layer x{.a{width:calc(100% - 2rem);--x:1  2}}'
check_idem .js $'function f(){return\n{x:1}};const r=/https?:\\/\\//;'
check_idem .jsx 'const x=<Comp<Map<string,Array<number>>> value={m}><span>{a > b ? x : y}</span></Comp>;'
check_idem .json '{"x":[1,2],"s":"a  b"}'
check_idem .xml '<?pi  a   b?><r><![CDATA[a < b]]><x> a  b </x></r>'
check_idem .svg '<svg><text>a  b</text><path d="M 0 0 L 10 10"/></svg>'

check_fail .html '<div'
check_fail .css 'a{/*'
check_fail .js 'const r=/unterminated'
check_fail .jsx 'const x=<div>{a+1</div>;'
check_fail .json '{"a":}'
check_fail .xml '<root><!--'

echo "Minify++ cross-format adversarial test passed"
