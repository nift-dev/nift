#!/usr/bin/env bash
# B4b mutation controls (docs/runtime-2/checkpoints/b4b-element-types.md): each
# mutant rebuilds the translation units holding its deliberate defect
# (-DNIFT_B4B_MUTANT=n), links them into the inference oracle
# (b4b-types-unit), the analysis-work harness (b4b-work) and nift; the oracle, the work
# gate or the b4b suite must FAIL. B4b 27: with B4B_FIXPOINT_OBJECT (the Makefile's), each
# mutant's CLI is also linked as a fixed-point build and the fixed-point gate runs on the b4b
# fixtures (tests/runtime2/b4b, guard on): its result is reported for every mutant.
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
  [30]="src/rt/infer_walk.cpp"                                  # A30: a push puts the class spelling in place of the binding (F1: no fixed point)
  [31]="src/rt/infer_sites.cpp src/rt/infer_prune.cpp"          # A31: past 16 sites the collapsed class keeps its precision (F1)
  [32]="src/rt/infer_bindings.cpp"                              # A32: a lambda keeps a captured binding's kinds read before it grew (U1)
  [34]="src/rt/codegen.cpp"                                     # A34: FunctionBuilder::emit's operand invariant removed (P1)
  [35]="src/rt/infer_sites.cpp"                                 # A35: a class a union opens skips its earlier readers (review of 16f24f36)
  [36]="src/rt/infer_bindings.cpp"                              # A36: only lambdas recorded as slot readers (4c9632b8: the owner of a lambda's write keeps a stale kind)
  [37]="src/rt/infer_notify.cpp"                                # A37: every notification scans its whole reader list (4c9632b8: quadratic on `many`)
  [38]="src/rt/infer_collection.cpp"                            # A38: reverse()/sort() results get a new site, not the receiver's (ce0a9a44, review N-1)
)
list=("$@"); [ ${#list[@]} -eq 0 ] && list=(2 19 20 21 22 23 24 25 26 27 28 29 30 31 32 34 35 36 37 38)
CXX=${CXX:-g++}
CPPFLAGS="-include src/build/production_tu.h -Isrc -Iinclude -Iminifypp/include -Iminifypp/src -Imarkuppp/include -Imarkuppp/vendor/cmark -I.build/libffi/$(${CC:-cc} -dumpmachine 2>/dev/null || uname -m)-$(basename "${CC:-cc}")/install/include"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread"
objects=${B4B_OBJECTS:?the Makefile passes the CLI objects}
unit_objects=${B4B_UNIT_OBJECTS:?the Makefile passes the unit objects}
guardoff_objects=${B4B_GUARDOFF_OBJECTS:-}  # the guard-off CLI's objects (review L4: end to end)
fixpoint_object=${B4B_FIXPOINT_OBJECT:-}    # B4b 27: the fixed-point build's inference entry
fail=0
# C46 (review of C44, condition C1): every signal is first run on the unmutated build (n=0: the
# tree's own objects, no NIFT_B4B_MUTANT). Since B5 the production build shares references, so the
# b4b suite runs in its shared-reference mode (--guardoff), as test-runtime2-b4b does; without it the
# unmutated build failed 61+ checks and every mutant was "caught" by that alone. A signal that fails
# unmutated cannot judge a mutant: the gate fails (negative control against vacuous kills).
signals() {  # signals N: build mutant N (0 = unmutated) and run every signal; sets unit suite workgate guard fixpoint
  local n=$1
  replaced=(); built=()
  if [ "$n" != 0 ]; then
    for src in ${FILES[$n]}; do
      obj=${src%.cpp}.o; mobj=$OUT/m$n-$(basename "$obj")
      $CXX $CPPFLAGS $CXXFLAGS -DNIFT_B4B_MUTANT=$n -c "$src" -o "$mobj" || { echo "mutant A$n: build failed"; exit 2; }
      replaced+=("$obj"); built+=("$mobj")
    done
  fi
  filter() { local list=$1; for r in "${replaced[@]}"; do list=$(printf '%s\n' $list | grep -vx "$r"); done; printf '%s ' $list; }
  link=$(filter "$(printf '%s\n' $objects | grep -v '\.a$')")
  libs=$(printf '%s\n' $objects | grep '\.a$' | tr '\n' ' ')
  $CXX $CXXFLAGS $link "${built[@]}" -ldl $libs -o "$OUT/nift-$n" || { echo "mutant A$n: link failed"; exit 2; }
  ulink=$(filter "$(printf '%s\n' $unit_objects | grep -v '\.a$')")
  ulibs=$(printf '%s\n' $unit_objects | grep '\.a$' | tr '\n' ' ')
  $CXX $CPPFLAGS -Itests/rt $CXXFLAGS tests/rt/b4b_types_unit.cpp $ulink "${built[@]}" -ldl $ulibs -o "$OUT/unit-$n" \
    || { echo "mutant A$n: unit link failed"; exit 2; }
  # B4b 23: the analysis-work gate (tests/runtime2/b4b/work.py) on the mutant's inference
  $CXX $CPPFLAGS -Itests/rt $CXXFLAGS tests/rt/b4b_work.cpp $ulink "${built[@]}" -ldl $ulibs -o "$OUT/work-$n" \
    || { echo "mutant A$n: work harness link failed"; exit 2; }
  unit=0; suite=0; guard="-"; workgate=0
  "$OUT/unit-$n" > "$OUT/u$n.log" 2>&1 || unit=1
  python3 tests/runtime2/b4b/work.py --harness "$OUT/work-$n" > "$OUT/w$n.log" 2>&1 || workgate=1
  python3 tests/runtime2/b4b_types.py --nift "$OUT/nift-$n" --guardoff --quick > "$OUT/m$n.log" 2>&1 || suite=1
  if [ -n "$guardoff_objects" ]; then
    glink=$(filter "$(printf '%s\n' $guardoff_objects | grep -v '\.a$')")
    $CXX $CXXFLAGS $glink "${built[@]}" -ldl $libs -o "$OUT/guardoff-$n" || { echo "mutant A$n: guard-off link failed"; exit 2; }
    { python3 tests/runtime2/b4b_types.py --nift "$OUT/guardoff-$n" --guardoff --quick &&
      python3 tests/runtime2/b4b/gen_alias.py --nift "$OUT/guardoff-$n" --sample 600 --seed 1; } > "$OUT/g$n.log" 2>&1 \
      && guard=passed || guard=failed
  fi
  fixpoint="-"
  if [ -n "$fixpoint_object" ]; then
    flink=$(filter "$(printf '%s\n' $objects | grep -v '\.a$' | grep -vx src/rt/infer_session.o)")
    $CXX $CXXFLAGS $flink "${built[@]}" "$fixpoint_object" -ldl $libs -o "$OUT/fixpoint-$n" \
      || { echo "mutant A$n: fixed-point link failed"; exit 2; }
    python3 tests/runtime2/b4b/fixpoint.py --nift "$OUT/fixpoint-$n" --corpus-only b4b > "$OUT/f$n.log" 2>&1 \
      && fixpoint=passed || fixpoint=failed
  fi
  rm -f "$OUT/nift-$n" "$OUT/unit-$n" "$OUT/guardoff-$n" "$OUT/work-$n" "$OUT/fixpoint-$n"
}
signals 0
if [ $unit -ne 0 ] || [ $suite -ne 0 ] || [ $workgate -ne 0 ] || [ "$guard" = failed ] || [ "$fixpoint" = failed ]; then
  echo "FAIL b4b mutants: the unmutated build fails a signal (oracle $unit, suite $suite, work gate $workgate, guard-off $guard, fixed-point gate $fixpoint): a mutant cannot be judged by it (see $OUT/*0.log)"
  exit 1
fi
echo "PASS b4b mutants negative control: the unmutated build passes every signal (oracle, suite --guardoff, work gate, guard-off, fixed-point gate)"
for n in "${list[@]}"; do
  signals "$n"
  by=()
  [ $unit -ne 0 ] && by+=("oracle $(grep -c 'case ' "$OUT/u$n.log") failing cases")
  [ $suite -ne 0 ] && by+=("suite $(grep -c '^FAIL ' "$OUT/m$n.log") failing checks")
  [ $workgate -ne 0 ] && by+=("work gate $(grep -c '^FAIL ' "$OUT/w$n.log") failures")
  [ "$guard" = failed ] && by+=("guard-off")
  [ "$fixpoint" = failed ] && by+=("fixed-point gate ($(grep -c '^FAIL non-fixed' "$OUT/f$n.log") non-fixed points)")
  if [ ${#by[@]} -eq 0 ]; then
    echo "mutant A$n (${FILES[$n]}): SURVIVED"; fail=1
  else
    echo "mutant A$n (${FILES[$n]}): caught by $(IFS=';'; echo "${by[*]}" | sed 's/;/; /g')"
  fi
done
[ $fail -eq 0 ] && echo "PASS b4b mutants: ${#list[@]} caught" || echo "FAIL b4b mutants"
exit $fail
