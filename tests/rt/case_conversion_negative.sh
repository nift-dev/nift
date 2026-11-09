#!/usr/bin/env bash
# Negative control for the focused case-conversion tests. Builds the real
# candidate, then a temporary copy of string_methods.cpp with a deliberately
# wrong ASCII mapping. The real test must exit 0; the mutant must exit nonzero.
# Run from anywhere through make (it passes the source lists); the script
# locates the repository root itself and compiles from there.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
TMP=$(mktemp -d)
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

CXX=${CXX:-g++}
CXXFLAGS=${CXXFLAGS:--std=c++17 -O2 -Wall -Wextra -pedantic -pthread}
# B09 (combined-b3-b5.md section 51): the control links the production runtime objects the real
# test links (RT_UNIT_OBJECTS from the Makefile), with string_methods.o replaced by the candidate
# or the mutant, each compiled from source exactly as production compiles it.
: "${RT_UNIT_OBJECTS:?run via 'make test-rt-case-conversion-negative'}"
: "${NIFT_CPPFLAGS:?run via 'make test-rt-case-conversion-negative'}"
cd "$ROOT"
OBJECTS=()
for file in $RT_UNIT_OBJECTS; do
  [ "$file" = src/rt/natives/string_methods.o ] || OBJECTS+=("$file")
done
[ "${#OBJECTS[@]}" -lt "$(wc -w <<<"$RT_UNIT_OBJECTS")" ] ||
  { echo "FAIL: src/rt/natives/string_methods.o is not in RT_UNIT_OBJECTS (harness error)" >&2; exit 1; }

build() { # source, binary: a build failure is a harness failure, never a detection
  if ! "$CXX" ${PRODUCTION_GUARD:-} $NIFT_CPPFLAGS $CXXFLAGS -c "$1" -o "$2.string_methods.o" ||
     ! "$CXX" $NIFT_CPPFLAGS -I"$ROOT/tests/rt" $CXXFLAGS \
       "$ROOT/tests/rt/case_conversion_unit.cpp" "$2.string_methods.o" "${OBJECTS[@]}" -o "$2"; then
    echo "FAIL: could not build $2 (harness error, not a mutant detection)" >&2
    exit 1
  fi
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

build "$ROOT/src/rt/natives/string_methods.cpp" "$TMP/candidate"
build "$TMP/string_methods.cpp" "$TMP/mutant"

if ! "$TMP/candidate" >"$TMP/candidate.log" 2>&1; then
  echo "FAIL: the unmutated candidate test failed" >&2
  cat "$TMP/candidate.log" >&2
  exit 1
fi
echo "candidate: PASS ($(tail -n 1 "$TMP/candidate.log"))"

if "$TMP/mutant" >"$TMP/mutant.log" 2>&1; then
  echo "FAIL: the deliberately broken mapping passed the test" >&2
  cat "$TMP/mutant.log" >&2
  exit 1
fi
echo "mutant: PASS (nonzero exit; $(tail -n 1 "$TMP/mutant.log"))"
echo "negative-control: PASS"
