#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# -lt 1 ]]; then
  echo "usage: $0 <css-file>..." >&2
  exit 2
fi
mkdir -p "$ROOT/.build"
"${CXX:-g++}" -I"$ROOT/include" -I"$ROOT/src" ${CXXFLAGS:--std=c++17 -O2 -Wall -Wextra -pedantic} \
  "$ROOT/benchmarks/css_fixture_benchmark.cpp" "$ROOT/src/Minify.cpp" \
  -o "$ROOT/.build/minifypp-css-fixture-benchmark"
"$ROOT/.build/minifypp-css-fixture-benchmark" "$@"
