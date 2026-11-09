#!/usr/bin/env bash
# B03 (combined-b3-b5.md section 48): negative control for the TLS-destructor rule.
# Rebuilds src/rt/handles/table.cpp with -DNIFT_TLS_MUTANT=n and links it in the production order:
#   0  control: object and binary identical to production; tls_destructors.py passes;
#   1  the CP17 thread_local std::vector (lazy TLS destructor registration, the B03 abort):
#      tls_destructors.py must fail, naming table.o.
# Usage (from the repository root; the Makefile passes the objects):
#   TLS_OBJECTS="<CLI objects> <libffi.a>" TLS_CPPFLAGS="..." tls_mutants.sh
set -u -o pipefail
objects=${TLS_OBJECTS:?the Makefile passes the CLI objects}
CPPFLAGS=${TLS_CPPFLAGS:?the Makefile passes CPPFLAGS}
CXX=${CXX:-g++}
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread"
OUT=.build/tls-mutants
TU=src/rt/handles/table.cpp
rm -rf "$OUT"; mkdir -p "$OUT"
fail() { echo "FAIL tls mutants: $*"; exit 1; }
build() {
  local n=$1
  local dir=$OUT/$n
  mkdir -p "$dir"
  $CXX $CPPFLAGS $CXXFLAGS -DNIFT_TLS_MUTANT="$n" -c "$TU" -o "$dir/table.o" >"$dir/build.log" 2>&1 ||
    { tail -20 "$dir/build.log"; fail "mutant $n: build failed (harness failure)"; }
  local link=() libs=() obj
  for obj in $objects; do
    case $obj in
      *.a) libs+=("$obj") ;;
      "${TU%.cpp}.o") link+=("$dir/table.o") ;;
      *) link+=("$obj") ;;
    esac
  done
  $CXX $CXXFLAGS "${link[@]}" -ldl "${libs[@]}" -o "$dir/nift" >>"$dir/build.log" 2>&1 ||
    { tail -20 "$dir/build.log"; fail "mutant $n: link failed (harness failure)"; }
}
build 0
cmp -s "$OUT/0/table.o" "${TU%.cpp}.o" || fail "the control object differs from production's (rebuild nift first)"
cmp -s "$OUT/0/nift" nift || fail "the control binary differs from ./nift (rebuild nift first)"
python3 -B tests/runtime2/tls_destructors.py --nift "$OUT/0/nift" --objects "$OUT/0/table.o" >"$OUT/0/test.log" 2>&1 ||
  { cat "$OUT/0/test.log"; fail "control (NIFT_TLS_MUTANT=0) failed"; }
echo "PASS tls control: NIFT_TLS_MUTANT=0 passes"
build 1
cmp -s "$OUT/1/table.o" "$OUT/0/table.o" && fail "mutant 1: its object equals the control's"
if python3 -B tests/runtime2/tls_destructors.py --nift "$OUT/1/nift" --objects "$OUT/1/table.o" >"$OUT/1/test.log" 2>&1; then
  cat "$OUT/1/test.log"; rm -rf "$OUT"; echo "FAIL tls mutant 1: survived"; exit 1
fi
grep -q "table.o registers a TLS destructor" "$OUT/1/test.log" || { cat "$OUT/1/test.log"; fail "mutant 1: not named"; }
echo "PASS tls mutant 1: rejected ($(grep -m1 'imports __cxa' "$OUT/1/test.log" | cut -c1-60)...; table.o named)"
rm -rf "$OUT"
