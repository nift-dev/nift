#!/usr/bin/env bash
# Reference semantics, Phase A (D-NEXT-refsem) mutation controls: each mutant
# rebuilds the one translation unit holding its deliberate defect
# (-DNIFT_REFSEM_MUTANT=n), links it with the normal objects, and the suite
# (refsem_a.py) must FAIL on it.
#   mutants.sh <nift> [n ...]      (default: every mutant)
set -u
NIFT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); shift
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
OUT=.build/refsem-mutants
mkdir -p "$OUT"
# mutant -> source file : what it breaks
declare -A FILE=(
  [1]=src/rt/value_cycle.cpp          # the cycle check misses the receiver itself (summary shortcut first)
  [2]=src/rt/aggregate.cpp            # a shared struct instance is copied on a field write (value semantics)
  [3]=src/rt/compare.cpp              # struct == is structural (the old D-040 rule)
  [4]=src/rt/natives/collection_methods.cpp  # same() answers structurally for collections and instances
  [5]=src/rt/vm_keyed.cpp             # no reference-cycle check at all
  [6]=src/rt/compiler_structs.cpp     # a map()/set()/stack() field initialiser is one shared constant
  [7]=src/rt/infer_collection.cpp     # collection contents are unknown-empty (inference unsound under aliasing)
  [8]=src/rt/vm_iter.cpp              # a map loop iterates the live map (no snapshot)
  [9]=src/rt/copy_graph.cpp           # deepcopy() loses sharing inside the copied graph
  [10]=src/rt/copy_graph.cpp          # copy() is deep (the v4.4 rule)
  [11]=src/rt/compiler_keyed.cpp      # mutable collections are admitted as keys and members
  [12]=src/rt/vm_coro_ops.cpp         # cooperative tasks share the caller's references (no copy at spawn)
  [13]=src/rt/reference_guard.cpp     # the Phase A forward-compatibility guard is off
  [14]=src/rt/vm_keyed.cpp            # a refused map store happens before the refusal (graph changed)
  [15]=src/rt/isolate_transfer_encode.cpp  # a thread transfer loses aliasing between collections/instances
)
list=("$@"); [ ${#list[@]} -eq 0 ] && list=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)
CXX=${CXX:-g++}
CPPFLAGS="-Isrc -Iinclude -Iminifypp/include -Iminifypp/src -Imarkuppp/include -Imarkuppp/vendor/cmark -I.build/libffi/$(${CC:-cc} -dumpmachine 2>/dev/null || uname -m)-$(basename "${CC:-cc}")/install/include"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread"
# The normal CLI objects (the Makefile passes them; otherwise ask make).
objects=${REFSEM_OBJECTS:-$(make -s -p -q nift 2>/dev/null | sed -n 's/^CLI_OBJECTS := //p' | head -1) \
  $(make -s -p -q nift 2>/dev/null | sed -n 's/^LIBFFI_A := //p' | head -1)}
fail=0
for n in "${list[@]}"; do
  src=${FILE[$n]}; obj=${src%.cpp}.o; mobj=$OUT/m$n-$(basename "$obj")
  $CXX $CPPFLAGS $CXXFLAGS -DNIFT_REFSEM_MUTANT=$n -c "$src" -o "$mobj" || { echo "mutant $n: build failed"; exit 2; }
  link=$(printf '%s\n' $objects | grep -vx "$obj" | grep -v '\.a$' | tr '\n' ' ')
  libs=$(printf '%s\n' $objects | grep '\.a$' | tr '\n' ' ')
  $CXX $CXXFLAGS $link "$mobj" -ldl $libs -o "$OUT/nift-$n" || { echo "mutant $n: link failed"; exit 2; }
  if python3 tests/runtime2/refsem_a.py --nift "$OUT/nift-$n" > "$OUT/m$n.log" 2>&1; then
    echo "mutant $n ($src): SURVIVED"; fail=1
  else
    echo "mutant $n ($src): caught ($(grep -c '^FAIL ' "$OUT/m$n.log") failing checks)"
  fi
done
[ $fail -eq 0 ] && echo "PASS refsem mutants: ${#list[@]} caught" || echo "FAIL refsem mutants"
exit $fail
