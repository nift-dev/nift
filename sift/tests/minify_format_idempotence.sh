#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/sift-formats.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

cat >"$TMP/driver.cpp" <<'CPP'
#include <sift/Sift.h>
#include <iostream>
#include <sstream>
#include <string>
int main(int argc,char**argv){
  if(argc!=2) return 64;
  sift::Format f;
  std::string ext=argv[1],in,out,err;
  if(!sift::format_for_extension(ext,f)){std::cerr<<"unknown format";return 65;}
  std::ostringstream ss;ss<<std::cin.rdbuf();in=ss.str();
  if(!sift::run(f,in,out,err)){std::cerr<<err;return 2;}
  std::cout<<out;
}
CPP
${CXX:-g++} -std=c++17 -O2 -I"$ROOT/include" -I"$ROOT/src" \
  "$TMP/driver.cpp" "$ROOT/src/Sift.cpp" -o "$TMP/minify"

check_idempotent(){
  local ext=$1 src=$2
  printf '%s' "$src" >"$TMP/in"
  "$TMP/minify" "$ext" <"$TMP/in" >"$TMP/once"
  "$TMP/minify" "$ext" <"$TMP/once" >"$TMP/twice"
  cmp "$TMP/once" "$TMP/twice"
}

html_cases=(
'<!doctype html>
<html><body><main>  <h1>Hello</h1>
<p>World</p> </main></body></html>'
'<div><span>A</span> <span>B</span><strong>C</strong></div>'
'<pre>  alpha
 beta  </pre><code> a   b </code>'
'<textarea>  one
  two &amp; three </textarea>'
'<script>const r=/[<>]/; const s="</not-script>";</script>'
'<script type="module">const x = `a ${1+2}`; // keep JS semantics
console.log(x)</script>'
'<style>.a { width: calc(100% - 2rem); }</style>'
'<template><section><span>A</span> <span>B</span></section></template>'
'<div title="a > b &amp; c"><img src="x" alt="a &quot;b&quot;"></div>'
'<!-- remove --><div><!-- inner --><span>x</span></div>'
'<p>héllo 世界 😀 <em>inline</em> text</p>'
'<div data-json="{&quot;a&quot;: 1}"> x </div>'
)

css_cases=(
'body { margin: 0; padding: calc(1rem + 2px); }'
':root { --x: 1  2; --url: url("data:image/svg+xml,%3Csvg%3E"); }'
'@media (width >= 40rem) { .a { display: grid; gap: 1rem; } }'
'@container sidebar (width > 30rem) { .card { container-type: inline-size; } }'
'@layer reset, base; @layer base { .a { color: color(display-p3 1 0 0 / .5); } }'
'@supports selector(:has(*)) { .a:has(> .b) { width: clamp(1rem, 2vw + 1rem, 3rem); } }'
'.a { & > .b { margin-inline: 1cqi; } }'
'@future-rule foo(bar > baz) { .x { future-prop: alpha beta / gamma; } }'
'.a::before { content: "/* not comment */"; }'
'.a { background: linear-gradient(45deg, red 0%, blue 100%); }'
'.a { grid-template: "a a" 1fr "b c" 2fr / minmax(0, 1fr) auto; }'
'.a { font-family: "A B", system-ui; animation: foo 1s steps(2, jump-none); }'
)

json_cases=(
'{"a": 1, "b": [true, false, null], "s": "a b"}'
'{"unicode":"世界 😀","escaped":"a\\nb\\t\\\"c"}'
'[{"x":1},{"x":2},{"nested":{"a":[1,2,3]}}]'
'{"n":-1.25e+10,"zero":0,"small":0.00001}'
'{"slashes":"https://example.com/a/b","braces":"{}[]"}'
)

xml_cases=(
'<?xml version="1.0"?><root><a> text </a><b x="1 &amp; 2"/></root>'
'<root xmlns:x="urn:x"><x:item>A &lt; B</x:item></root>'
'<root><![CDATA[ x < y && y > z ]]><child/> tail </root>'
'<root><!-- c --><a>one</a> <a>two</a></root>'
'<root xml:space="preserve"> A  B </root>'
)

svg_cases=(
'<svg viewBox="0 0 10 10"><path d="M 0 0 L 10 10 Z"/></svg>'
'<svg><text xml:space="preserve"> A  B </text></svg>'
'<svg xmlns:xlink="http://www.w3.org/1999/xlink"><use xlink:href="#x"/></svg>'
'<svg><style>.a { fill: red; }</style><path class="a" d="M0 0h10v10z"/></svg>'
'<svg><foreignObject><div xmlns="http://www.w3.org/1999/xhtml"> A <b>B</b> C </div></foreignObject></svg>'
)

count=0
for x in "${html_cases[@]}"; do check_idempotent .html "$x"; count=$((count+1)); done
for x in "${css_cases[@]}";  do check_idempotent .css  "$x"; count=$((count+1)); done
for x in "${json_cases[@]}"; do check_idempotent .json "$x"; count=$((count+1)); done
for x in "${xml_cases[@]}";  do check_idempotent .xml  "$x"; count=$((count+1)); done
for x in "${svg_cases[@]}";  do check_idempotent .svg  "$x"; count=$((count+1)); done

# JSON gets an additional structural semantic oracle.
node - "$TMP/minify" <<'JS'
const {spawnSync}=require("child_process");
const exe=process.argv[2];
const cases=[
 '{"a": 1, "b": [true,false,null], "s":"a b"}',
 '{"unicode":"世界 😀","escaped":"a\\nb\\t\\\"c"}',
 '[{"x":1},{"x":2},{"nested":{"a":[1,2,3]}}]',
 '{"n":-1.25e10,"zero":0,"small":0.00001}',
];
for(const src of cases){
  const p=spawnSync(exe,['.json'],{input:src,encoding:'utf8'});
  if(p.status!==0) throw new Error(p.stderr);
  if(JSON.stringify(JSON.parse(src))!==JSON.stringify(JSON.parse(p.stdout)))
    throw new Error("JSON semantic mismatch");
}
JS

# JSON is the structurally validated mode. XML/SVG are conservative lexical
# minifiers, not validating XML parsers, so malformed XML is outside this gate.
if printf '%s' '{"a":}' | "$TMP/minify" .json >"$TMP/bad.out" 2>/dev/null; then
  echo "malformed JSON unexpectedly accepted" >&2
  exit 1
fi

echo "Generated non-JS format idempotence corpus passed ($count documents)"
