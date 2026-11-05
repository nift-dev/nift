#!/usr/bin/env bash
# B3 mutation controls (docs/runtime-2/checkpoints/b3-structs.md): each mutant
# rebuilds the one translation unit holding its deliberate defect
# (-DNIFT_B3_MUTANT=n), links it into nift and into the value-layer unit
# (struct-render-unit), and the B3 suites must FAIL on it: b3_structs.py on
# the mutant nift, or the unit on the mutant objects.
#   mutants.sh <nift> <unit-objects-file> [n ...]   (default: every mutant)
set -u
NIFT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); shift
UNIT_OBJECTS_FILE=$1; shift
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
OUT=.build/b3-mutants
mkdir -p "$OUT"
# mutant -> source file : what it breaks
declare -A FILE=(
  [1]=src/rt/natives/format_methods.cpp   # D1: rendering writes private fields
  [2]=src/rt/isolate_transfer_decode.cpp  # D2: a transfer drops the descriptor's privacy
  [3]=src/rt/compiler_structs.cpp         # D3: a replaced session schema rewrites the old instances' descriptor
  [4]=src/rt/copy_graph.cpp               # D4: copy/deepcopy give the copy an all-public descriptor
  [5]=src/rt/isolate_task.cpp             # D5: a program crossing into an isolate loses its descriptors' privacy
  [6]=src/rt/natives/format_methods.cpp   # D6: the pretty form drops the comma after each field line
  [7]=src/rt/natives/format_methods.cpp   # D7: rendering walks the instance's fields, unpaired with the descriptor
  [8]=src/rt/compiler_structs.cpp         # D8: rendering a container dispatches to a struct's own presentation method
  [9]=src/rt/compiler_structs.cpp         # D9: an explicit call on a mixed receiver binds the built-in despite a user method
)
list=("$@"); [ ${#list[@]} -eq 0 ] && list=(1 2 3 4 5 6 7 8 9)
CXX=${CXX:-g++}
CPPFLAGS="-include src/build/production_tu.h -Isrc -Iinclude -Iminifypp/include -Iminifypp/src -Imarkuppp/include -Imarkuppp/vendor/cmark -I.build/libffi/$(${CC:-cc} -dumpmachine 2>/dev/null || uname -m)-$(basename "${CC:-cc}")/install/include"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread"
objects=${B3_OBJECTS:?the Makefile passes the CLI objects}
unit_objects=$(cat "$UNIT_OBJECTS_FILE")
fail=0
for n in "${list[@]}"; do
  src=${FILE[$n]}; obj=${src%.cpp}.o; mobj=$OUT/m$n-$(basename "$obj")
  $CXX $CPPFLAGS $CXXFLAGS -DNIFT_B3_MUTANT=$n -c "$src" -o "$mobj" || { echo "mutant D$n: build failed"; exit 2; }
  link=$(printf '%s\n' $objects | grep -vx "$obj" | grep -v '\.a$' | tr '\n' ' ')
  libs=$(printf '%s\n' $objects | grep '\.a$' | tr '\n' ' ')
  $CXX $CXXFLAGS $link "$mobj" -ldl $libs -o "$OUT/nift-$n" || { echo "mutant D$n: link failed"; exit 2; }
  ulink=$(printf '%s\n' $unit_objects | grep -vx "$obj" | grep -v '\.a$' | tr '\n' ' ')
  ulibs=$(printf '%s\n' $unit_objects | grep '\.a$' | tr '\n' ' ')
  $CXX $CXXFLAGS "$OUT/../rt/struct_render_unit.o" $ulink "$mobj" -ldl $ulibs -o "$OUT/unit-$n" \
    || { echo "mutant D$n: unit link failed"; exit 2; }
  suite=0; unit=0
  python3 tests/runtime2/b3_structs.py --nift "$OUT/nift-$n" > "$OUT/m$n.log" 2>&1 || suite=1
  "$OUT/unit-$n" > "$OUT/u$n.log" 2>&1 || unit=1
  if [ $suite -eq 0 ] && [ $unit -eq 0 ]; then
    echo "mutant D$n ($src): SURVIVED"; fail=1
  else
    echo "mutant D$n ($src): caught (suite $(grep -c '^FAIL ' "$OUT/m$n.log") failing checks; unit $( [ $unit -eq 1 ] && echo failed || echo passed))"
  fi
  rm -f "$OUT/nift-$n" "$OUT/unit-$n"
done
[ $fail -eq 0 ] && echo "PASS b3 mutants: ${#list[@]} caught" || echo "FAIL b3 mutants"
exit $fail
