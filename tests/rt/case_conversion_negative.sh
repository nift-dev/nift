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
# Source lists come from the Makefile (RT_BUILTINS_CORE / RT_BUILTINS_SOURCES)
# so the control links exactly what the real test links. Paths are relative to
# ROOT; every compile runs from ROOT.
: "${RT_BUILTINS_CORE:?run via 'make test-rt-case-conversion-negative'}"
: "${RT_BUILTINS_SOURCES:?run via 'make test-rt-case-conversion-negative'}"
cd "$ROOT"
read -r -a CORE <<<"$RT_BUILTINS_CORE"
NATIVES=()
for file in $RT_BUILTINS_SOURCES; do
  [ "$(basename "$file")" = "string_methods.cpp" ] || NATIVES+=("$file")
done

build() { # source, binary: a build failure is a harness failure, never a detection
  if ! "$CXX" -I"$ROOT/src" -I"$ROOT/include" -I"$ROOT/tests/rt" $CXXFLAGS \
       "$ROOT/tests/rt/case_conversion_unit.cpp" "$1" "${NATIVES[@]}" "${CORE[@]}" -o "$2"; then
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
