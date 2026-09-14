#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BENCHMARK_ROOT=${MINIFYPP_BENCHMARK_ROOT:-"$ROOT/../minification-benchmarks"}
ARTIFACTS="$BENCHMARK_ROOT/packages/artifacts/node_modules"

if ! command -v node >/dev/null 2>&1; then
  echo "Node not installed; real-bundle JavaScript syntax gate skipped"
  exit 0
fi
if [[ ! -d "$ARTIFACTS" ]]; then
  echo "Benchmark artifacts not installed; real-bundle JavaScript syntax gate skipped"
  exit 0
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/minify-real-bundles.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

cat >"$TMP/driver.cpp" <<'CPP'
#include <minify/Minify.h>
#include <iostream>
#include <sstream>
#include <string>
int main(int argc,char**argv){
  std::ostringstream source;source<<std::cin.rdbuf();std::string output,error;
  minify::Options options;
  const std::string mode=argc>1?argv[1]:"conservative";
  if(mode=="structured")options.optimization=minify::OptimizationLevel::Structured;
  else if(mode=="aggressive")options.optimization=minify::OptimizationLevel::Aggressive;
  if(!minify::javascript(source.str(),output,error,options)){std::cerr<<error;return 2;}
  std::cout<<output;
}
CPP
${CXX:-g++} -std=c++17 -O2 -I"$ROOT/include" -I"$ROOT/src" \
  "$TMP/driver.cpp" "$ROOT/src/Minify.cpp" -o "$TMP/minjs"

fixtures=(
  "react/cjs/react.development.js"
  "moment/moment.js"
  "jquery/dist/jquery.js"
  "vue/dist/vue.js"
  "lodash/lodash.js"
  "d3/dist/d3.js"
  "terser/dist/bundle.min.js"
  "three/build/three.js"
  "victory/dist/victory.js"
  "echarts/dist/echarts.js"
  "antd/dist/antd.js"
  "typescript/lib/typescript.js"
)

count=0
for fixture in "${fixtures[@]}"; do
  source="$ARTIFACTS/$fixture"
  [[ -f "$source" ]] || { echo "Missing benchmark fixture: $source" >&2; exit 1; }
  name=${fixture%%/*}
  for mode in conservative structured aggressive; do
    "$TMP/minjs" "$mode" <"$source" >"$TMP/$name-$mode.js"
    node --check "$TMP/$name-$mode.js" >/dev/null
    count=$((count+1))
  done
done

echo "Real-bundle JavaScript syntax gate passed ($count outputs)"
