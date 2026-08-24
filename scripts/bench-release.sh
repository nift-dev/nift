#!/usr/bin/env bash
# NR12 controlled C++ Engine benchmark: compiles the benchmarked engine from
# clean sources with the project's production/release flags (Makefile
# defaults: -std=c++17 -O2), links the benchmark, and runs several samples
# reporting the median ns/render. No ambient .o files are reused.
#
# Usage: scripts/bench-release.sh [samples]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/.build/bench-release"
SAMPLES="${1:-7}"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++17 -O2 -pthread"
CPPFLAGS="-Isrc -Iinclude -Iminifypp/include -Iminifypp/src"
SOURCES="
  src/Engine.cpp
  src/Context.cpp
  src/Value.cpp
  src/FileSystem.cpp
  src/JsonFile.cpp
  src/JsonSchema.cpp
  minifypp/src/Minify.cpp
  src/Parser.cpp
  src/ProjectInfo.cpp
  src/ProjectRead.cpp
  src/ProjectState.cpp
  src/WatchList.cpp
  src/BuildProgress.cpp
"

mkdir -p "$OUT"
"$CXX" --version | head -1

for source in $SOURCES; do
  object="$OUT/$(basename "${source%.cpp}").o"
  "$CXX" $CPPFLAGS $CXXFLAGS -c "$source" -o "$object"
done

"$CXX" $CPPFLAGS $CXXFLAGS tests/engine_bench.cpp -o "$OUT/engine-bench" "$OUT"/*.o -pthread

echo "clean release C++ benchmark ($SAMPLES samples, median):"
"$OUT/engine-bench" "$SAMPLES"
