#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if ! command -v node >/dev/null 2>&1; then
  echo "Node not installed; generated minifier semantic corpus skipped"
  exit 0
fi
TMP=$(mktemp -d "${TMPDIR:-/tmp}/minify-generated.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/driver.cpp" <<'CPP'
#include <minify/Minify.h>
#include <iostream>
#include <string>
int main(){
  std::string in,out,err; std::size_t line=0;
  while(std::getline(std::cin,in)){
    ++line;
    if(!minify::javascript(in,out,err)){std::cerr<<"case "<<line<<": "<<err;return 2;}
    std::cout<<out<<"\n";
  }
}
CPP
${CXX:-g++} -std=c++17 -O2 -I"$ROOT/include" -I"$ROOT/src" "$TMP/driver.cpp" "$ROOT/src/Minify.cpp" -o "$TMP/minjs"

prefixes=(
  'if(false){}'
  'if(true){let q=1;}'
  'while(false){}'
  'for(let i=0;i<0;i++){}'
  'try{}catch(e){}'
  'function f(){}'
  'class C{}'
  'switch(1){case 1:break;}'
  '{let a=1;}'
  'do{}while(false);'
  'try{}finally{}'
  'class C extends Object{}'
  'async function f(){}'
  'function* g(){}'
  'if(true){class C{}}'
  '{class C{}}'
  'if(false){}else{}'
  'try{}catch(e){}finally{}'
  'label:{}'
  'for(;;){break;}'
  'for(const x of []){}'
  'try{}catch{}'
  'if(true){function inner(){}}'
  'class C{static {let x=1;}}'
  'outer:for(;;){break outer;}'
  'if(false){class X{}}else{function y(){}}'
  'with({}){}'
  'if(true)label:{}'
  'while(false)label:{}'
  'try{throw 1}catch(e){if(e){}}'
  'switch(2){default:{let x=1;}}'
  'for(const k in {}){}'
  'const z=1;'
  'let z=1;'
  'var z=1;'
  'debugger;'
  'class C{m(){return 1;}}'
  'class C{get x(){return 1;}set x(v){}}'
  'class C{#x=1;m(){return this.#x;}}'
  'class C{static #x=1;}'
  'async function* ag(){}'
  '(()=>{})();'
  'if(true){label:{}}'
  'try{if(false){}}catch{}'
  'switch(1){case 1:{break;}}'
  'for(let i=0;i<1;i++){continue;}'
  'do{break;}while(false);'
  'if(true){try{}finally{}}'
  'while(false){class C{}}'
  'for(const x of [1]){if(x)break;}'
  'for(let i=0;i<1;i++){class C{}}'
  'switch(1){case 1:class C{};break;}'
  'try{throw 1}catch{class C{}}'
  'function f(){class C{} return 1;}'
  'async function f(){await Promise.resolve();}'
  'function* f(){yield 1;}'
  'async function* f(){yield await 1;}'
  'class C{async m(){return 1;}}'
  'class C{*m(){yield 1;}}'
  'class C{async *m(){yield 1;}}'
  'class C{static{class D{}}}'
  'label:{class C{}}'
  'class C{static get x(){return 1;}}'
  'class C{static set x(v){}}'
  'class C{m(){try{return 1}finally{}}}'
  'if(true){for(const x of [1]){if(x){break;}}}'
  'try{class C{}}finally{class D{}}'
  'class C{#x=1;static #y=2;m(){return this.#x;}}'
  'class C extends Object{constructor(){super();}}'
  'if(true){const f=()=>1;f();}'
  'if(true){const f=async()=>1;}'
  'if(true){const f=function(){};}'
  'if(true){const f=class{};}'
  'async function f(){for await(const x of asyncGen()){} }'
  'switch(1){case 1:{class C{};break;}default:break;}'
  'try{throw 1}catch(e){switch(e){case 1:break;}}'
  'if(true){do{}while(false);}'
  'if(true){while(false){}elseLabel:{}}'
  'outer:{inner:{break inner;}break outer;}'
  'class C{m(){return ()=>({x:1});}}'
  'class C{static m(){return class D{};}}'
  'class C{m(){return function*(){yield 1;};}}'
  'class C{m(){return async function(){return 1;};}}'
  'class C{m(){return /a/.test("a");}}'

  'class C{static x=(()=>1)();}'
  'class C{static #x=(()=>1)();static get(){return this.#x;}}'
  'const f=async()=>await 1;'
  'const f=async x=>await x;'
  'const f=function*(){yield* [1];};'
  'const f=async function*(){yield await 1;};'
  'const o={async m(){return 1},*g(){yield 1}};'
  'const o={get x(){return 1},set x(v){}};'
  'for(const [a,b] of [[1,2]]){}'
  'for(const {a} of [{a:1}]){}'
  'try{}catch({message}){}'
  'if(true){(()=>{})()}'
  'while(false){(()=>{})()}'
  'switch(1){case 1:(()=>{})();break;}'
  'class C extends (class{}){}'
  'class C{static{try{}finally{}}}'
  'class C{static{for(let i=0;i<1;i++){} }}'
  'class C{m(){try{return 1}finally{}}}'
  'class C{m(){switch(1){case 1:return 1;}}}'
  'const f=(x={a:1})=>x.a;'
  'const f=({a=1}={})=>a;'
  'const f=([a=1]=[])=>a;'
  'const o={m({a=1}={}){return a}};'
  'const o={m([a=1]=[]){return a}};'
  'const o={["x"](){return 1}};'
  'const o={...{a:1},b:2};'
  'const x={a:1};({...x});'
  'const x=[1];[...x];'
  'try{throw {a:1}}catch({a}){}'
  'try{throw [1]}catch([a]){}'
  'for(const {a=1} of [{}]){}'
  'for(const [a=1] of [[]]){}'
  'if(true){class C{static{class D{}}}}'
)

regexes=(
  "/a/.test('a')"
  "/https?:\/\//.test('https://x')"
  "/[/*}]/.test('}')"
  "/a{1,2}/g.test('aa')"
  "/(?<=a)b/.test('ab')"
  "/a\/b/u.test('a/b')"
  "/^[\p{L}]+$/u.test('abc')"
  "/(?:a|b)+/i.test('A')"
  "/[\\/]x/.test('/x')"
  "/[<>]/.test('<')"
  "/[{}<>]/.test('>')"
  "/(?:<|>)/.test('<')"
  "/\\/\\*not-comment\\*\\//.test('/*not-comment*/')"
  "/\\/\\/not-comment/.test('//not-comment')"
  "/^(?<x>a)(?=\\k<x>)/.test('aa')"
  "/\\p{Script=Greek}+/u.test('α')"
  "/[😀-🙏]/u.test('😀')"
  "/^(?:https?:\\/\\/)?[^/]+$/.test('example.com')"
  "/(?<!a)b/.test('cb')"
  "/a(?=b)/.test('ab')"
  "/a(?!b)/.test('ac')"
  "/(?<word>\\w+)\\s+\\k<word>/.test('x x')"
  "/[\\u{1F600}-\\u{1F64F}]/u.test('😀')"
  "/\\x2f\\x2a/.test('/*')"
  "/[\\]\\[]/.test('[')"
  "/(?:\\/\\/|\\/\\*)/.test('//')"
  "/^[$_\\p{ID_Start}][$_\\p{ID_Continue}]*$/u.test('α1')"
  "/^a.b$/s.test('a\\nb')"
  "/a/y.test('a')"
  "/a/d.test('a')"
  "/(?:ab){2,3}?/.test('abab')"
  "/(?<n>\\d+)(?:\\.\\k<n>)?/.test('12.12')"
  "/[\\s\\S]*/.test('\\n')"
  "/(?:\\u0061|a)+/u.test('aa')"
  "/[^/*<>]+/.test('abc')"
  "/\\u{10FFFF}/u.test('\\u{10FFFF}')"
  "/[\\p{ASCII}&&\\p{Letter}]/v.test('A')"
  "/[\\q{ab|cd}]/v.test('ab')"
  "/(?<=\\bfoo)bar/.test('foobar')"
  "/(?<!\\w)foo/.test(' foo')"
  "/(?:a|b|c){1,4}/.test('abc')"
  "/[[a-z]&&[^aeiou]]/v.test('b')"
  "/(?<a>a)(?<b>b)\\k<a>\\k<b>/.test('abab')"
  "/^(?:\\p{Emoji_Presentation}|\\p{Letter})+$/u.test('A😀')"

  "/(?<=(?:a|b))c/.test('ac')"
  "/(?<!ab)c/.test('xc')"
  "/(?<x>a|b)c\\k<x>/.test('aca')"
  "/(?:[/*<>]|\\\\.)+/.test('<')"
  "/[\\p{Letter}\\p{Number}_]+/u.test('α1')"
  "/\\u{1F680}/u.test('🚀')"
  "/(?:^|\\s)#[\\w-]+/u.test(' #tag')"
  "/(?:(?:ab)+|cd?){1,3}/.test('abab')"
  "/\\/(?:[^/\\\\]|\\\\.)+\\/[gimsuy]*/.test('/x/g')"
  "/[\\x00-\\x1f]/.test('\\n')"
  "/(?=(?:a.*){2})a.*a/.test('aba')"
  "/(?<=\\bfoo\\s)bar/.test('foo bar')"
  "/(?<!\\w)foo(?!\\w)/.test(' foo ')"
  "/(?<q>['\\\"])(.*?)\\k<q>/.test('\\\"x\\\"')"
  "/(?:\\\\.|[^\\\\/])+/u.test('abc')"
  "/[\\p{ASCII}&&\\p{Letter}]/v.test('A')"
  "/[\\q{ab|cd}]/v.test('a')"
  "/\\p{RGI_Emoji}/v.test('😀')"
  "/(?:(?<a>a)|(?<b>b))+/.test('ab')"
  "/^(?:[^'\\\"]|'[^']*'|\\\"[^\\\"]*\\\")+$/.test('abc')"
  "/(?:\\u0061|\\x62|c)+/.test('abc')"
  "/[\\s\\S]*?<\\/script>/i.test('x</script>')"
)

suffixes=(
  "&&console.log('T');"
  ";console.log('T');"
)

count=0
: >"$TMP/orig-all.js"
: >"$TMP/batch.in"
for prefix in "${prefixes[@]}"; do
  for regex in "${regexes[@]}"; do
    for suffix in "${suffixes[@]}"; do
      source="$prefix $regex$suffix"
      printf '%s\n' "$source" >>"$TMP/batch.in"
      printf '{console.log("CASE:%d");%s}\n' "$count" "$source" >>"$TMP/orig-all.js"
      count=$((count+1))
    done
  done
done

# Counter-cases ensure regex-oriented fixes do not break division.
counter=(
  'const x={valueOf(){return 12}} / 2; console.log(x);'
  'const x=(()=>12) / 2; console.log(x);'
  'const x=({a:1}) / /a/.test("a"); console.log(Number.isNaN(x));'
  'const x=(()=>({valueOf(){return 12}}))() / 2; console.log(x);'
  'const x=(async function(){return 12}) / 2; console.log(Number.isNaN(x));'
  'const x=(function*(){}) / 2; console.log(Number.isNaN(x));'
  'const x=class extends Object{} / 2; console.log(Number.isNaN(x));'
  'const x=(class Named{}) / 2; console.log(Number.isNaN(x));'
  'const x=function(){} / 2; console.log(Number.isNaN(x));'
  'const x=async function(){} / 2; console.log(Number.isNaN(x));'
  'const x=function named(){} / /a/.test("a"); console.log(Number.isNaN(x));'
  'const x=({valueOf(){return 12}}) / 3; console.log(x);'
  'const x=(function(){return 12}) / 3; console.log(Number.isNaN(x));'
  'const x=(()=>12) / /a/.test("a"); console.log(x);'
  'const x=({valueOf(){return 12}}) / /a/.test("a"); console.log(x);'
)
for source in "${counter[@]}"; do
  printf '%s\n' "$source" >>"$TMP/batch.in"
  printf '{console.log("CASE:%d");%s}\n' "$count" "$source" >>"$TMP/orig-all.js"
  count=$((count+1))
done

# Minify the whole generated corpus in one native process. Each source is still
# transformed independently; batching removes thousands of process startups.
"$TMP/minjs" <"$TMP/batch.in" >"$TMP/batch.out"
: >"$TMP/min-all.js"
idx=0
while IFS= read -r minified; do
  printf '{console.log("CASE:%d");%s}\n' "$idx" "$minified" >>"$TMP/min-all.js"
  idx=$((idx+1))
done <"$TMP/batch.out"
test "$idx" -eq "$count"

node "$TMP/orig-all.js" >"$TMP/orig.out" 2>"$TMP/orig.err"
node "$TMP/min-all.js" >"$TMP/min.out" 2>"$TMP/min.err"
cmp "$TMP/orig.out" "$TMP/min.out"
cmp "$TMP/orig.err" "$TMP/min.err"

echo "Generated JavaScript minifier semantic corpus passed ($count programs)"
