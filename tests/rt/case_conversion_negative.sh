#!/usr/bin/env bash
# Negative control for the focused case-conversion tests. Builds the real
# candidate, then a temporary copy of string_methods.cpp with a deliberately
# wrong ASCII mapping. The real test must exit 0; the mutant must exit nonzero.
# Run from anywhere; the script locates the repository root itself.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
TMP=$(mktemp -d)
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

CXX=${CXX:-g++}
CXXFLAGS=${CXXFLAGS:--std=c++17 -O2 -Wall -Wextra -pedantic -pthread}
CORE=(
  "$ROOT/src/rt/value.cpp" "$ROOT/src/rt/string.cpp" "$ROOT/src/rt/aggregate.cpp"
  "$ROOT/src/rt/atom.cpp" "$ROOT/src/rt/heap_account.cpp" "$ROOT/src/rt/compare.cpp"
  "$ROOT/src/rt/number.cpp" "$ROOT/src/rt/decimal.cpp" "$ROOT/src/rt/render.cpp"
  "$ROOT/src/rt/error.cpp" "$ROOT/src/rt/handle.cpp" "$ROOT/src/rt/legacy_bridge.cpp"
  "$ROOT/src/RuntimeValue.cpp"
)
NATIVES=()
for file in "$ROOT"/src/rt/natives/*.cpp; do
  [ "$(basename "$file")" = "string_methods.cpp" ] || NATIVES+=("$file")
done

build_and_run() {
  local source=$1 binary=$2 log=$3
  "$CXX" -Isrc -Itests/rt $CXXFLAGS "$ROOT/tests/rt/case_conversion_unit.cpp" "$source" \
    "${NATIVES[@]}" "${CORE[@]}" -o "$binary"
  "$binary" >"$log" 2>&1
}

cp "$ROOT/src/rt/natives/string_methods.cpp" "$TMP/string_methods.cpp"
python3 - "$TMP/string_methods.cpp" <<'PY'
import sys
path = sys.argv[1]
text = open(path).read()
old = "map[c] = static_cast<unsigned char>(c - ('a' - 'A'));"
new = "map[c] = static_cast<unsigned char>(c - ('a' - 'B'));"
if old not in text:
    raise SystemExit("mutant pattern not found; update the negative control")
open(path, "w").write(text.replace(old, new, 1))
PY

if ! build_and_run "$ROOT/src/rt/natives/string_methods.cpp" "$TMP/candidate" "$TMP/candidate.log"; then
  echo "FAIL: the unmutated candidate test failed" >&2
  cat "$TMP/candidate.log" >&2
  exit 1
fi
echo "candidate: PASS ($(tail -n 1 "$TMP/candidate.log"))"

if build_and_run "$TMP/string_methods.cpp" "$TMP/mutant" "$TMP/mutant.log"; then
  echo "FAIL: the deliberately broken mapping passed the test" >&2
  cat "$TMP/mutant.log" >&2
  exit 1
fi
echo "mutant: PASS (nonzero exit; $(tail -n 1 "$TMP/mutant.log"))"
echo "negative-control: PASS"
