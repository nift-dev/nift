#!/usr/bin/env bash
# B4b mutation controls (docs/runtime-2/checkpoints/b4b-element-types.md): each
# mutant rebuilds the translation units holding its deliberate defect
# (-DNIFT_B4B_MUTANT=n), links them into the inference oracle
# (b4b-types-unit) and into nift; the oracle or the b4b suite must FAIL.
#   mutants.sh <nift> [n ...]   (default: every mutant)
set -u
NIFT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); shift
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
OUT=.build/b4b-mutants
mkdir -p "$OUT"
# mutant -> source files : what it breaks
declare -A FILES=(
  [2]="src/rt/infer_sites.cpp"                                  # A2: a store reaches only the binding stored through
  [19]="src/rt/infer_sites.cpp"                                 # A19: stores in functions/lambdas miss the caller's sites
  [20]="src/rt/infer_sites.cpp src/rt/infer_collection.cpp"     # A20: one mutator (reverse) dropped from the list (C1)
  [21]="src/rt/infer_escape.cpp"                                # A21: session code trusts element types (C5)
  [22]="src/rt/infer_sites.cpp"                                 # A22: an inner part has no site (nested alias)
  [23]="src/rt/infer_escape.cpp"                                # A23: an escaping function keeps its parameter types (C4)
  [24]="src/rt/infer_sites.cpp"                                 # A24: a mutator on a union receiver is not modelled (C2)
  [26]="src/rt/infer_sites.cpp"                                 # A26: no merge beyond 16 sites (H2; the glob scaling check)
  [27]="src/rt/infer_sites.cpp"                                 # A27: a grown site's readers are not re-walked (H2)
  [28]="src/rt/infer_escape.cpp"                                # A28: an inexact callee widens no target (H1)
  [29]="src/rt/infer_sites.cpp"                                 # A29: a push/index/member store into earlier session values exposes nothing (M1)
  [25]="src/rt/compiler_assign_expr.cpp"                        # A25: an assignment's constant value moved as a register (P1)
)
list=("$@"); [ ${#list[@]} -eq 0 ] && list=(2 19 20 21 22 23 24 25 26 27 28 29)
CXX=${CXX:-g++}
CPPFLAGS="-include src/build/production_tu.h -Isrc -Iinclude -Iminifypp/include -Iminifypp/src -Imarkuppp/include -Imarkuppp/vendor/cmark -I.build/libffi/$(${CC:-cc} -dumpmachine 2>/dev/null || uname -m)-$(basename "${CC:-cc}")/install/include"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread"
objects=${B4B_OBJECTS:?the Makefile passes the CLI objects}
unit_objects=${B4B_UNIT_OBJECTS:?the Makefile passes the unit objects}
guardoff_objects=${B4B_GUARDOFF_OBJECTS:-}  # the guard-off CLI's objects (review L4: end to end)
fail=0
for n in "${list[@]}"; do
  replaced=(); built=()
  for src in ${FILES[$n]}; do
    obj=${src%.cpp}.o; mobj=$OUT/m$n-$(basename "$obj")
    $CXX $CPPFLAGS $CXXFLAGS -DNIFT_B4B_MUTANT=$n -c "$src" -o "$mobj" || { echo "mutant A$n: build failed"; exit 2; }
    replaced+=("$obj"); built+=("$mobj")
  done
  filter() { local list=$1; for r in "${replaced[@]}"; do list=$(printf '%s\n' $list | grep -vx "$r"); done; printf '%s ' $list; }
  link=$(filter "$(printf '%s\n' $objects | grep -v '\.a$')")
  libs=$(printf '%s\n' $objects | grep '\.a$' | tr '\n' ' ')
  $CXX $CXXFLAGS $link "${built[@]}" -ldl $libs -o "$OUT/nift-$n" || { echo "mutant A$n: link failed"; exit 2; }
  ulink=$(filter "$(printf '%s\n' $unit_objects | grep -v '\.a$')")
  ulibs=$(printf '%s\n' $unit_objects | grep '\.a$' | tr '\n' ' ')
  $CXX $CPPFLAGS -Itests/rt $CXXFLAGS tests/rt/b4b_types_unit.cpp $ulink "${built[@]}" -ldl $ulibs -o "$OUT/unit-$n" \
    || { echo "mutant A$n: unit link failed"; exit 2; }
  unit=0; suite=0; guard="-"
  "$OUT/unit-$n" > "$OUT/u$n.log" 2>&1 || unit=1
  python3 tests/runtime2/b4b_types.py --nift "$OUT/nift-$n" --quick > "$OUT/m$n.log" 2>&1 || suite=1
  if [ -n "$guardoff_objects" ]; then
    glink=$(filter "$(printf '%s\n' $guardoff_objects | grep -v '\.a$')")
    $CXX $CXXFLAGS $glink "${built[@]}" -ldl $libs -o "$OUT/guardoff-$n" || { echo "mutant A$n: guard-off link failed"; exit 2; }
    { python3 tests/runtime2/b4b_types.py --nift "$OUT/guardoff-$n" --guardoff --quick &&
      python3 tests/runtime2/b4b/gen_alias.py --nift "$OUT/guardoff-$n" --sample 600 --seed 1; } > "$OUT/g$n.log" 2>&1 \
      && guard=passed || guard=failed
  fi
  if [ $unit -eq 0 ] && [ $suite -eq 0 ] && [ "$guard" != failed ]; then
    echo "mutant A$n (${FILES[$n]}): SURVIVED"; fail=1
  else
    echo "mutant A$n (${FILES[$n]}): caught (oracle $(grep -c 'case ' "$OUT/u$n.log") failing cases; suite $(grep -c '^FAIL ' "$OUT/m$n.log") failing checks; guard-off $guard)"
  fi
  rm -f "$OUT/nift-$n" "$OUT/unit-$n" "$OUT/guardoff-$n"
done
[ $fail -eq 0 ] && echo "PASS b4b mutants: ${#list[@]} caught" || echo "FAIL b4b mutants"
exit $fail
