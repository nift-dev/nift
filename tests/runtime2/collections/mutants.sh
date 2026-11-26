#!/usr/bin/env bash
# Weekend collections (D-063) mutation controls: each mutant rebuilds the one
# translation unit holding its deliberate defect (-DNIFT_COLLECTIONS_MUTANT=n),
# links it with the normal objects, and the suite must FAIL on it.
#   mutants.sh <nift> [n ...]      (default: every mutant)
set -u -o pipefail
NIFT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); shift
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
OUT=.build/collections-mutants
mkdir -p "$OUT"
# mutant -> source file : what it breaks
# M1-M4 and M10 (value-semantics controls) are retired by D-NEXT-refsem: the
# aliasing they planted is now the contract and their code no longer exists
# (collections/superseded/INDEX.tsv); refsem/mutants.sh holds their successors.
declare -A FILE=(
  [5]=src/rt/aggregate_collection.cpp # a map insert is never counted on the heap
  [6]=src/rt/compare.cpp              # map equality depends on entry order
  [7]=src/rt/vm_iter.cpp              # a stack is iterated bottom first
  [8]=src/rt/compare.cpp              # 1.0 does not hash like 1 (indexed lookups miss)
  [9]=src/rt/isolate_transfer_decode.cpp  # a transferred collection loses its kind
  [11]=src/rt/compiler_native_args.cpp       # same() admits non-name arguments ("f", calls)
  [12]=src/rt/compiler_native_args.cpp       # built-ins no longer check arguments/receivers holding collections
  [13]=src/rt/compiler_collection.cpp        # key/comparator callbacks may return collections
  [14]=src/rt/compiler.cpp                   # a collection is the shown eval/session result
  [15]=src/rt/compiler_loops.cpp             # a map loop is analysed as a live source: the vanished-element
                                             # refusals return (B5 fix-up: the live iteration it once planted
                                             # is unreachable, every loop iterates its held entry snapshot;
                                             # witnesses parity/snapshot_map_body_{write,capture}.f)
  [16]=src/rt/compiler_native_args.cpp       # mutex(v) accepts a value holding a collection
  [17]=src/rt/aggregate_collection.cpp      # sorted inserts retain append order
)
list=("$@"); [ ${#list[@]} -eq 0 ] && list=(5 6 7 8 9 11 12 13 14 15 16 17)
CXX=${CXX:-g++}
CPPFLAGS="-Isrc -Iinclude -Iminifypp/include -Iminifypp/src -Imarkuppp/include -Imarkuppp/vendor/cmark -I.build/libffi/$(${CC:-cc} -dumpmachine 2>/dev/null || uname -m)-$(basename "${CC:-cc}")/install/include"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread"
# The normal CLI objects (the Makefile passes them; otherwise ask make).
objects=${COLLECTIONS_OBJECTS:-$(make -s -p -q nift 2>/dev/null | sed -n 's/^CLI_OBJECTS := //p' | head -1) \
  $(make -s -p -q nift 2>/dev/null | sed -n 's/^LIBFFI_A := //p' | head -1)}
fail=0
for n in "${list[@]}"; do
  src=${FILE[$n]}; obj=${src%.cpp}.o; mobj=$OUT/m$n-$(basename "$obj")
  $CXX $CPPFLAGS $CXXFLAGS -DNIFT_COLLECTIONS_MUTANT=$n -c "$src" -o "$mobj" || { echo "mutant $n: build failed"; exit 2; }
  # C80-L2 (combined-b3-b5.md section 79): the mutant object must differ from production.
  if cmp -s "$mobj" "$obj"; then echo "mutant $n: compiles to the production $obj (a compiled-out hook)"; exit 2; fi
  link=$(printf '%s\n' $objects | grep -vx "$obj" | grep -v '\.a$' | tr '\n' ' ')
  libs=$(printf '%s\n' $objects | grep '\.a$' | tr '\n' ' ')
  $CXX $CXXFLAGS $link "$mobj" -ldl $libs -o "$OUT/nift-$n" || { echo "mutant $n: link failed"; exit 2; }
  if python3 tests/runtime2/weekend_collections.py --nift "$OUT/nift-$n" > "$OUT/m$n.log" 2>&1; then
    echo "mutant $n ($src): SURVIVED"; fail=1
  else
    echo "mutant $n ($src): caught ($(grep -c '^FAIL ' "$OUT/m$n.log") failing checks)"
  fi
done
[ $fail -eq 0 ] && echo "PASS collections mutants: ${#list[@]} caught" || echo "FAIL collections mutants"
exit $fail
