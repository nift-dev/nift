#!/usr/bin/env bash
# B4 mutation controls (docs/runtime-2/checkpoints/b4-dispatch.md): each mutant
# rebuilds the one translation unit holding its deliberate defect
# (-DNIFT_B4_MUTANT=n), links it into nift and into the admission unit
# (op-admission-unit); the B4 checks must FAIL on it: b4_dispatch.py on the
# mutant nift, or the unit on the mutant objects.
# Control S10 (the session lowering, CP19 mutant 10: the tag is forced to the
# string branch, so a number and null reach the string native through the
# guarded branches' unguarded path) must be COVERED by the dispatch
# precondition: the flagged branches are redirected within the family table and
# the session prints legacy's output (without B4 the mutant stops with exit 70).
#   mutants.sh <nift> <unit-objects-file> [n ...]   (default: every mutant)
set -u
NIFT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); shift
UNIT_OBJECTS_FILE=$1; shift
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
OUT=.build/b4-mutants
mkdir -p "$OUT"
# mutant -> source file : what it breaks
declare -A FILE=(
  [1]=src/rt/natives/builtin.cpp     # A1: the dispatch table omits a receiver kind (arrays)
  [3]=src/rt/op_admission.cpp        # A3: admission refuses every Dynamic case
  [4]=src/rt/vm_builtin.cpp          # A4: the VM ignores the Dynamic flag (a native meets a foreign receiver)
  [5]=src/rt/vm_builtin.cpp          # A5: a dispatch failure is an internal failure (exit 70)
  [6]=src/rt/op_admission.cpp        # A6: the unsupported and malformed failure texts are swapped
  [9]=src/rt/compiler_interp.cpp     # A9: B0's narrowing restored (scalar_only binds to_string unflagged)
)
list=("$@"); [ ${#list[@]} -eq 0 ] && list=(1 3 4 5 6 9 S10)
CXX=${CXX:-g++}
CPPFLAGS="-include src/build/production_tu.h -Isrc -Iinclude -Iminifypp/include -Iminifypp/src -Imarkuppp/include -Imarkuppp/vendor/cmark -I.build/libffi/$(${CC:-cc} -dumpmachine 2>/dev/null || uname -m)-$(basename "${CC:-cc}")/install/include"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread"
objects=${B4_OBJECTS:?the Makefile passes the CLI objects}
unit_objects=$(cat "$UNIT_OBJECTS_FILE")
fail=0
link_nift() {  # <object replaced> <mutant object> <output>
  local link libs
  link=$(printf '%s\n' $objects | grep -vx "$1" | grep -v '\.a$' | tr '\n' ' ')
  libs=$(printf '%s\n' $objects | grep '\.a$' | tr '\n' ' ')
  $CXX $CXXFLAGS $link "$2" -ldl $libs -o "$3"
}
for n in "${list[@]}"; do
  if [ "$n" = S10 ]; then
    src=src/rt/compiler_session_native.cpp; obj=${src%.cpp}.o; mobj=$OUT/s10-$(basename "$obj")
    $CXX $CPPFLAGS $CXXFLAGS -DNIFT_SESSION_ADMISSION_MUTANT=10 -c "$src" -o "$mobj" || { echo "control S10: build failed"; exit 2; }
    link_nift "$obj" "$mobj" "$OUT/nift-s10" || { echo "control S10: link failed"; exit 2; }
    d=$(mktemp -d)
    input='fn(f(v)) { print(v.to_string()) }\nf("ab")\nf(3)\nf(null)\n'
    got=$(printf "$input" | (cd "$d" && NIFT_VM=require timeout 60 "$ROOT/$OUT/nift-s10" -i 2>&1); echo "rc=$?")
    want=$(printf "$input" | (cd "$d" && NIFT_VM=0 timeout 60 "$NIFT" -i 2>&1); echo "rc=$?")
    printf '%s\n' "$got" > "$ROOT/$OUT/s10.log"
    if [ "$got" = "$want" ]; then
      echo "control S10 ($src): the dispatch precondition repairs the forced branch (output equals legacy)"
    else
      echo "control S10 ($src): NOT COVERED: got $(printf '%s' "$got" | tr '\n' '|') want $(printf '%s' "$want" | tr '\n' '|')"; fail=1
    fi
    rm -rf "$d"; [ -n "${KEEP_S10:-}" ] || rm -f "$OUT/nift-s10"
    continue
  fi
  src=${FILE[$n]}; obj=${src%.cpp}.o; mobj=$OUT/m$n-$(basename "$obj")
  $CXX $CPPFLAGS $CXXFLAGS -DNIFT_B4_MUTANT=$n -c "$src" -o "$mobj" || { echo "mutant A$n: build failed"; exit 2; }
  link_nift "$obj" "$mobj" "$OUT/nift-$n" || { echo "mutant A$n: link failed"; exit 2; }
  ulink=$(printf '%s\n' $unit_objects | grep -vx "$obj" | grep -v '\.a$' | tr '\n' ' ')
  ulibs=$(printf '%s\n' $unit_objects | grep '\.a$' | tr '\n' ' ')
  $CXX $CXXFLAGS "$OUT/../rt/op_admission_unit.o" $ulink "$mobj" -ldl $ulibs -o "$OUT/unit-$n" \
    || { echo "mutant A$n: unit link failed"; exit 2; }
  suite=0; unit=0
  python3 tests/runtime2/b4_dispatch.py --nift "$OUT/nift-$n" --quick > "$OUT/m$n.log" 2>&1 || suite=1
  "$OUT/unit-$n" > "$OUT/u$n.log" 2>&1 || unit=1
  if [ $suite -eq 0 ] && [ $unit -eq 0 ]; then
    echo "mutant A$n ($src): SURVIVED"; fail=1
  else
    echo "mutant A$n ($src): caught (suite $(grep -c '^FAIL ' "$OUT/m$n.log") failing checks; unit $( [ $unit -eq 1 ] && echo failed || echo passed))"
  fi
  rm -f "$OUT/nift-$n" "$OUT/unit-$n"
done
[ $fail -eq 0 ] && echo "PASS b4 mutants: ${#list[@]} caught or covered" || echo "FAIL b4 mutants"
exit $fail
