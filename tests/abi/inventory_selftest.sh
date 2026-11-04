#!/usr/bin/env bash
# Tooling checkpoint (component 6): negative controls for the inventory test.
#   inventory_selftest.sh CXX "CXXFLAGS" "LINKFLAGS" MAP OBJECTS...
# Links two throw-away copies of libnift_c.so from the same PIC objects and
# requires check_abi_symbols.py to FAIL on each: one without the version
# script (internal symbols exported, the state before this checkpoint) and
# one whose version script lacks a declared function (a documented symbol
# missing).
set -u
cxx=$1 cxxflags=$2 linkflags=$3 map=$4; shift 4
repo=$(cd "$(dirname "$0")/../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/nift-abi-selftest.XXXXXX"); trap 'rm -rf "$work"' EXIT
fail=0
check() { python3 "$repo/scripts/check_abi_symbols.py" "$1" "$repo/include/nift/c_abi.h" \
  "$repo/tests/abi/libnift_c.symbols" --extra "$repo/src/embed/abi-exports-extra.txt" > "$work/out.txt" 2>&1; }
# shellcheck disable=SC2086
$cxx $cxxflags -shared $linkflags -o "$work/open.so" "$@" || { echo "FAIL abi-selftest: link"; exit 1; }
if check "$work/open.so"; then echo "FAIL abi-selftest: a library exporting every symbol passed"; fail=1
else echo "PASS abi-selftest: a library without the version script fails ($(grep -m1 FAIL "$work/out.txt" | cut -c1-110))"; fi
grep -v 'nift_engine_render_text;' "$map" > "$work/short.map"
# shellcheck disable=SC2086
$cxx $cxxflags -shared $linkflags -Wl,--version-script="$work/short.map" -o "$work/short.so" "$@" || { echo "FAIL abi-selftest: link"; exit 1; }
if check "$work/short.so"; then echo "FAIL abi-selftest: a library lacking nift_engine_render_text passed"; fail=1
elif grep -q 'not exported: nift_engine_render_text' "$work/out.txt"; then
  echo "PASS abi-selftest: a library lacking a declared function (nift_engine_render_text) fails"
else echo "FAIL abi-selftest: the missing symbol was not named"; fail=1; fi
exit $fail
