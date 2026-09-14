#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BENCHMARK_ROOT=${MINIFYPP_BENCHMARK_ROOT:-"$ROOT/../minification-benchmarks"}
CLI="$BENCHMARK_ROOT/packages/bench/benchmark/cli.ts"
BUILD="$BENCHMARK_ROOT/packages/minifiers/native/build-minifypp.sh"

if ! command -v node >/dev/null 2>&1; then
  echo "Node not installed; real-bundle semantic gate skipped"
  exit 0
fi
if [[ ! -f "$CLI" || ! -x "$BUILD" || ! -d "$BENCHMARK_ROOT/node_modules" ]]; then
  echo "Benchmark checkout or dependencies unavailable; real-bundle semantic gate skipped"
  exit 0
fi

MINIFYPP_SOURCE_DIR="$ROOT" "$BUILD"

artifacts=(react moment jquery vue lodash d3 terser three victory echarts antd typescript)
modes=(default structured aggressive)
count=0

for artifact in "${artifacts[@]}"; do
  for mode in "${modes[@]}"; do
    result=$(cd "$BENCHMARK_ROOT" && node "$CLI" \
      --artifact "$artifact" --minifier minifypp --instance "$mode")
    if [[ "$result" != *'"minifiedBytes"'* || "$result" != *'"minzippedBytes"'* ]]; then
      echo "Unexpected benchmark result for $artifact/$mode: $result" >&2
      exit 1
    fi
    count=$((count+1))
  done
done

echo "Real-bundle semantic gate passed ($count validated outputs)"
