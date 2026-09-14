#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if ! command -v node >/dev/null 2>&1; then echo "Node not installed; structured JavaScript semantic gate skipped"; exit 0; fi
TMP=$(mktemp -d "${TMPDIR:-/tmp}/minify-structured.XXXXXX"); trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/driver.cpp" <<'CPP'
#include <minify/Minify.h>
#include <iostream>
#include <sstream>
int main(){std::ostringstream s;s<<std::cin.rdbuf();std::string o,e;minify::Options x;x.optimization=minify::OptimizationLevel::Structured;if(!minify::javascript(s.str(),o,e,x)){std::cerr<<e;return 2;}std::cout<<o;}
CPP
${CXX:-g++} -std=c++17 -O2 -I"$ROOT/include" -I"$ROOT/src" "$TMP/driver.cpp" "$ROOT/src/Minify.cpp" -o "$TMP/minjs"
run_case(){ local name="$1" source="$2"; printf '%s' "$source">"$TMP/$name.js"; "$TMP/minjs"<"$TMP/$name.js">"$TMP/$name.min.js"; node "$TMP/$name.js">"$TMP/$name.o" 2>"$TMP/$name.oe"; node "$TMP/$name.min.js">"$TMP/$name.m" 2>"$TMP/$name.me"; cmp "$TMP/$name.o" "$TMP/$name.m"; cmp "$TMP/$name.oe" "$TMP/$name.me"; }
run_case simple 'function total(longLeft,longRight){return longLeft+longRight}console.log(total(2,3));'
run_case unused 'function first(longName,unusedName){return longName}console.log(first(7,8));'
run_case locals 'function total(longValue){const doubledValue=longValue*2;return doubledValue}console.log(total(4));'
run_case property 'function keep(longName){return {longName:1,value:longName}.value}console.log(keep(9));'
run_case shorthand 'function keep(longName){return {longName}}console.log(keep(4).longName);'
run_case closure 'function keep(longName){return ()=>longName}console.log(keep(5)());'
run_case dynamic 'function keep(longName){return eval("longName")}console.log(keep(6));'
run_case nested_dynamic 'function keep(longName){if(1){eval("longName")};return longName}console.log(keep(6));'
run_case indirect_eval_property 'function keep(longName){const box={eval:x=>x};return box.eval(longName)}console.log(keep(6));'
run_case duplicate 'function keep(longName,longName){return longName}console.log(keep(1,2));'
run_case arguments 'function keep(longName){return arguments[0]}console.log(keep(3));'
run_case comma_arguments 'function invoke(longName){return Math.max(0,longName,9)}console.log(invoke(4));'
run_case nested_call_in_object 'function invoke(longName){return {value:Math.max(0,longName,9)}}console.log(invoke(4).value);'
run_case nested_call_after_property 'function invoke(longName){return {value:Math.max(0,longName,9),other:1}}console.log(invoke(4).value);'
run_case ternary_in_object 'function offsets(xPadding,yPadding,crossAxis){return {x:crossAxis?2-xPadding:0,y:crossAxis?2-yPadding:0}}console.log(offsets(1,2,true));'
run_case comma_shorthand 'function pair(resolve,reject){return {resolve,reject}}console.log(Object.keys(pair(1,2)).join(","));'
run_case ternary 'function choose(longName,otherName){return longName?otherName:longName}console.log(choose(0,7));'
run_case contextual 'function* outer(){return (function keep(yield){return yield})(4)}console.log(outer().next().value);'
run_case typescript_named_export 'var ts={};(function(ts){function createBinaryExpressionTrampoline(){return 7}ts.createBinaryExpressionTrampoline=createBinaryExpressionTrampoline})(ts);console.log(ts.createBinaryExpressionTrampoline());'
echo "Structured JavaScript semantic differential test passed (19 cases)"
