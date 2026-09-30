#!/usr/bin/env bash
# NR12 controlled C++ Engine benchmark: compiles the benchmarked engine from
# clean sources with the project's production/release flags (Makefile
# defaults: -std=c++17 -O2), links the benchmark, and runs several samples
# reporting the median ns/render. No ambient .o files are reused.
#
# Usage: scripts/bench-release.sh [samples]
set -euo pipefail
export LC_ALL=C

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/.build/bench-release"
SAMPLES="${1:-7}"
cd "$ROOT"

CXX="${CXX:-g++}"
CC="${CC:-cc}"
CXXFLAGS="-std=c++17 -O2 -pthread"
rm -rf "$OUT"
mkdir -p "$OUT"
CC="$CC" CXX="$CXX" CFLAGS="-O2 -fPIC" bash scripts/build_vendored_libffi.sh "$OUT/libffi" >/dev/null
LIBFFI_INCLUDE="$OUT/libffi/install/include"
LIBFFI_A="$OUT/libffi/install/lib/libffi.a"
CPPFLAGS="-Isrc -Iinclude -Iminifypp/include -Iminifypp/src -Imarkuppp/include -Imarkuppp/vendor/cmark -I$LIBFFI_INCLUDE"
PARSER_SOURCES=""
for source in src/Parser*.cpp; do
  [ -f "$source" ] || continue
  PARSER_SOURCES="$PARSER_SOURCES $source"
done
[ -n "$PARSER_SOURCES" ] || { echo "no src/Parser*.cpp sources found" >&2; exit 1; }
SOURCES="
  src/ProjectOwnership.cpp
  src/PackageTransaction.cpp
  src/embed/Engine.cpp
  src/embed/Context.cpp
  src/RuntimeValue.cpp
  src/Value.cpp
  src/FileSystem.cpp
  src/JsonFile.cpp
  src/JsonSchema.cpp
  minifypp/src/Minify.cpp
  markuppp/src/Markup.cpp
  markuppp/src/AsciiDoc.cpp
  markuppp/src/ReStructuredText.cpp
  $PARSER_SOURCES
  src/Ast.cpp
  src/Automation.cpp
  src/Process.cpp
  src/JobControl.cpp
  src/Hooks.cpp
  src/ProjectInfo.cpp
  src/ProjectRead.cpp
  src/ProjectState.cpp
  src/WatchList.cpp
  src/BuildProgress.cpp
"
MARKUP_C_NAMES="blocks buffer cmark cmark_ctype houdini_href_e houdini_html_e houdini_html_u \
  html inlines iterator node references render scanners utf8"

"$CXX" --version | head -1

for source in $SOURCES; do
  object="$OUT/$(basename "${source%.cpp}").o"
  "$CXX" $CPPFLAGS $CXXFLAGS -c "$source" -o "$object"
done
for name in $MARKUP_C_NAMES; do
  "$CC" -std=c99 -O2 -Imarkuppp/vendor/cmark -c "markuppp/vendor/cmark/$name.c" -o "$OUT/cmark_$name.o"
done

SYSTEM_LIBS=""
case "$(uname -s)" in
  Linux*) SYSTEM_LIBS="-ldl" ;;
esac
"$CXX" $CPPFLAGS $CXXFLAGS tests/engine_bench.cpp -o "$OUT/engine-bench" "$OUT"/*.o "$LIBFFI_A" -pthread $SYSTEM_LIBS

echo "clean release C++ benchmark ($SAMPLES samples, median):"
"$OUT/engine-bench" "$SAMPLES"
