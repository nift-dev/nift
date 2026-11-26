#!/usr/bin/env bash
# Negative control for str.replace's pre-allocation budget check (32e402b2, C3; owner 2026-11-10,
# combined-b3-b5.md section 64). The str.replace callgrind guard was re-baselined for the cost of
# this check; removing the check must therefore be caught by CORRECTNESS tests, so it can never
# pass as an optimisation. Builds tests/rt/native_budget.cpp twice: with the real string_methods.o
# (must pass) and with a temporary copy of string_methods.cpp whose str_replace builds into an
# unchecked std::string instead of BudgetedText (must fail: the over-budget replace succeeds).
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
TMP=$(mktemp -d)
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

CXX=${CXX:-g++}
CXXFLAGS=${CXXFLAGS:--std=c++17 -O2 -Wall -Wextra -pedantic -pthread}
: "${RT_UNIT_OBJECTS:?run via 'make test-rt-replace-budget-negative'}"
: "${NIFT_CPPFLAGS:?run via 'make test-rt-replace-budget-negative'}"
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
       "$ROOT/tests/rt/native_budget.cpp" "$2.string_methods.o" "${OBJECTS[@]}" -o "$2"; then
    echo "FAIL: could not build $2 (harness error, not a mutant detection)" >&2
    exit 1
  fi
}

cp "$ROOT/src/rt/natives/string_methods.cpp" "$TMP/string_methods.cpp"
python3 - "$TMP/string_methods.cpp" <<'PY'
import sys
path = sys.argv[1]
text = open(path).read()
# The mutant: str_replace without the budget check (an unchecked buffer with BudgetedText's API).
old = """    BudgetedText result(context);
    if (!result.reserve(text.size())) return Status::Memory;"""
new = """    struct Unchecked {  // MUTANT: the budget check removed
        std::string s;
        bool append(std::string_view a, std::string_view b = {}) { s.append(a); s.append(b); return true; }
        std::string take() { return std::move(s); }
    } result;
    (void)context;
    result.s.reserve(text.size());"""
if text.count(old) != 1:
    raise SystemExit("mutant pattern not found; update the negative control")
open(path, "w").write(text.replace(old, new, 1))
PY

build "$ROOT/src/rt/natives/string_methods.cpp" "$TMP/candidate"
build "$TMP/string_methods.cpp" "$TMP/mutant"

if ! "$TMP/candidate" >"$TMP/candidate.log" 2>&1; then
  echo "FAIL: the unmutated native_budget test failed" >&2
  cat "$TMP/candidate.log" >&2
  exit 1
fi
echo "candidate: PASS ($(tail -n 1 "$TMP/candidate.log"))"

mutant_status=0
"$TMP/mutant" >"$TMP/mutant.log" 2>&1 || mutant_status=$?
if [ "$mutant_status" -eq 0 ]; then
  echo "FAIL: str.replace without its budget check passed the budget tests" >&2
  cat "$TMP/mutant.log" >&2
  exit 1
fi
if [ "$mutant_status" -ne 1 ]; then
  echo "FAIL: the mutant did not complete its assertions (exit $mutant_status)" >&2
  tail -n 15 "$TMP/mutant.log" >&2
  exit 1
fi
grep -q "native_budget.cpp:" "$TMP/mutant.log" ||
  { echo "FAIL: the mutant failed, but not on a native_budget check (harness error?)" >&2; cat "$TMP/mutant.log" >&2; exit 1; }
echo "mutant: PASS (rejected by correctness checks: $(grep -c 'CHECK failed' "$TMP/mutant.log") failed; $(tail -n 1 "$TMP/mutant.log"))"
echo "negative-control: PASS"
