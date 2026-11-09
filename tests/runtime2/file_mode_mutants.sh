#!/usr/bin/env bash
# B02 (combined-b3-b5.md section 46): negative controls for the file() buffer mode preconditions.
#
# Rebuilds src/rt/natives/host_file_handles.cpp with -DNIFT_FILE_MODE_MUTANT=n, links it in the
# production order with the tree's other objects, and runs tests/runtime2/file_mode_parity.py:
#   0  control (no mutation): must pass, and its object and binary must equal production's;
#   1  revert() without legacy's need_write (a read-only buffer reverted silently: the B02 defect);
#   2  flush() without need_write;
#   3  write_line() reporting its preconditions as "write: ...".
# Each mutant's object must differ from the control's and the mutant must fail the table.
# Usage (from the repository root; the Makefile passes the objects):
#   FILE_MODE_OBJECTS="<CLI objects> <libffi.a>" FILE_MODE_CPPFLAGS="..." file_mode_mutants.sh
set -u -o pipefail
objects=${FILE_MODE_OBJECTS:?the Makefile passes the CLI objects}
CPPFLAGS=${FILE_MODE_CPPFLAGS:?the Makefile passes CPPFLAGS}
CXX=${CXX:-g++}
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread"
OUT=.build/file-mode-mutants
TU=src/rt/natives/host_file_handles.cpp
rm -rf "$OUT"; mkdir -p "$OUT"
fail() { echo "FAIL file-mode mutants: $*"; exit 1; }

build() {  # build N: the mutated object and binary in $OUT/N
  local n=$1
  local dir=$OUT/$n
  mkdir -p "$dir"
  $CXX $CPPFLAGS $CXXFLAGS -DNIFT_FILE_MODE_MUTANT="$n" -c "$TU" -o "$dir/host_file_handles.o" >"$dir/build.log" 2>&1 ||
    { tail -20 "$dir/build.log"; fail "mutant $n: build failed (harness failure)"; }
  local link=() libs=() obj
  for obj in $objects; do
    case $obj in
      *.a) libs+=("$obj") ;;
      "${TU%.cpp}.o") link+=("$dir/host_file_handles.o") ;;
      *) link+=("$obj") ;;
    esac
  done
  $CXX $CXXFLAGS "${link[@]}" -ldl "${libs[@]}" -o "$dir/nift" >>"$dir/build.log" 2>&1 ||
    { tail -20 "$dir/build.log"; fail "mutant $n: link failed (harness failure)"; }
}

build 0
cmp -s "$OUT/0/host_file_handles.o" "${TU%.cpp}.o" || fail "the control object differs from production's (rebuild nift first)"
cmp -s "$OUT/0/nift" nift || fail "the control binary differs from ./nift (rebuild nift first)"
python3 -B tests/runtime2/file_mode_parity.py --nift "$OUT/0/nift" >"$OUT/0/test.log" 2>&1 ||
  { grep -v '^PASS' "$OUT/0/test.log" | tail -20; fail "control (NIFT_FILE_MODE_MUTANT=0) failed the table"; }
echo "PASS file-mode control: NIFT_FILE_MODE_MUTANT=0 passes ($(tail -1 "$OUT/0/test.log"))"
status=0
for n in 1 2 3; do
  build "$n"
  cmp -s "$OUT/$n/host_file_handles.o" "$OUT/0/host_file_handles.o" && fail "mutant $n: its object equals the control's"
  if python3 -B tests/runtime2/file_mode_parity.py --nift "$OUT/$n/nift" >"$OUT/$n/test.log" 2>&1; then
    echo "FAIL file-mode mutant $n: survived the table"; status=1
  else
    echo "PASS file-mode mutant $n: rejected; $(grep -c '^FAIL ' "$OUT/$n/test.log") failing check(s), first: $(grep -m1 '^FAIL ' "$OUT/$n/test.log" | cut -c1-90)"
  fi
done
rm -rf "$OUT"
[ "$status" = 0 ] && echo "file-mode mutants: all 3 rejected, control passes"
exit "$status"
