#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
if ! node -e 'require("postcss")' 2>/dev/null; then
  echo "PostCSS not installed; CSS semantic differential test skipped"
  exit 0
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/minify-css-semantics.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

cat >"$TMP/driver.cpp" <<'CPP'
#include <minify/Minify.h>
#include <iostream>
#include <sstream>
#include <string>
int main(){
  std::ostringstream stream;
  stream << std::cin.rdbuf();
  std::string output, error;
  if (!minify::css(stream.str(), output, error)) {
    std::cerr << error;
    return 2;
  }
  std::cout << output;
}
CPP

${CXX:-g++} -std=c++17 -O2 -I"$ROOT/include" -I"$ROOT/src" \
  "$TMP/driver.cpp" "$ROOT/src/Minify.cpp" -o "$TMP/mincss"

node - "$TMP/mincss" <<'JS'
const {spawnSync} = require("child_process");
const postcss = require("postcss");
const selectorParser = require("postcss-selector-parser");
const valueParser = require("postcss-value-parser");
const executable = process.argv[2];

const cases = [
  `.grid { grid-template-columns: 1.15fr .85fr; font: 700 .75rem sans-serif; padding: .1em .3em; }`,
  `.a .b, .a #id, .a :hover, .a [data-x], .a * { color: red; }`,
  `* .item, [data-x] button, :is(.a, .b) > span { color: red; }`,
  `.fonts { font-family: "A B" serif; content: "a" "b"; }`,
  `@media screen and (width > 10px) { .x { display: grid; } }`,
  `@media (prefers-color-scheme: dark) { .x { color: white; } }`,
  `.x { transform: translateX(1px) scale(2); filter: blur(1px) contrast(2); }`,
  `.x { color: color-mix(in srgb, var(--bg) 92%, transparent); }`,
  `.x { width: calc(100% - 2rem); height: min(10px + 2vw, 30px); }`,
  `@supports selector(:has(*)) { .a:has(> .b) { display: block; } }`,
  `@container sidebar (width > 30rem) { .card { container-type: inline-size; } }`,
  `@layer reset, base; @layer base { .a { color: display-p3(1 0 0); } }`,
  `.a { grid-template: "a a" 1fr "b c" 2fr / minmax(0, 1fr) auto; }`,
  `.a { --tokens: alpha  beta / gamma; animation: foo 1s steps(2, jump-none); }`,
  `.a { background: linear-gradient(45deg, red 0%, blue 100%); }`,
  `.a { & > .b { margin-inline: 1cqi; } }`,
  `@font-face { font-family: "Demo"; src: url(demo.woff2) format("woff2"); }`,
];

function semantic(node) {
  if (node.type === "comment") return null;
  const result = {type: node.type};
  for (const key of ["name", "prop", "important"]) {
    if (node[key] !== undefined) result[key] = node[key];
  }
  if (node.selector !== undefined) {
    result.selector = selectorParser().processSync(node.selector, {lossless: false});
  }
  for (const key of ["params", "value"]) {
    if (node[key] === undefined) continue;
    const parsed = valueParser(node[key]);
    parsed.walk(part => {
      if (part.type === "space") part.value = " ";
      if (part.before !== undefined) part.before = "";
      if (part.after !== undefined) part.after = "";
    });
    result[key] = parsed.toString().replace(/\s*([><=,\/])\s*/g, "$1");
  }
  if (node.nodes) result.nodes = node.nodes.map(semantic).filter(Boolean);
  return result;
}

let count = 0;
for (const source of cases) {
  const run = spawnSync(executable, [], {input: source, encoding: "utf8"});
  if (run.status !== 0) throw new Error(`minifier rejected CSS: ${run.stderr}\n${source}`);
  const before = semantic(postcss.parse(source));
  const after = semantic(postcss.parse(run.stdout));
  if (JSON.stringify(before) !== JSON.stringify(after)) {
    throw new Error(`CSS semantic tree changed\nsource: ${source}\noutput: ${run.stdout}\n` +
                    `before: ${JSON.stringify(before)}\nafter: ${JSON.stringify(after)}`);
  }
  ++count;
}
console.log(`CSS PostCSS semantic differential corpus passed (${count} stylesheets)`);
JS
