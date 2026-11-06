#!/usr/bin/env bash
# B5 mutation controls (docs/runtime-2/checkpoints/b5-refsem.md section 7): each mutant
# (-DNIFT_B5_MUTANT=n, its translation units recompiled and relinked with the tree's other
# objects, so `make nift` must be current) must fail the B5 suite (tests/runtime2/b5/run.py) or
# the reference-semantics suite (tests/runtime2/refsem_a.py). A build failure is a harness
# failure, never a detection.
#   1 infer_sites.cpp    U6: an earlier session value keeps its recorded structure (B4b's model)
#   (2, a store through a siteless earlier value skipping the heap, is equivalent and retired:
#    every store's container is read through sited() first, b5-refsem.md section 7)
#   3 infer_session.cpp  U6: recorded structures are the bindings' types as written (not materialized)
#   4 infer_session.cpp  U6: every earlier session value is private (no heap)
#   6 vm_ops.cpp           R-B6: a pushed array or object is copied (value semantics again)
#   7 compiler_array_mutation.cpp+session_invocation.cpp
#                        R-B7: `args` immutability is a binding-only check again (aliases mutate it)
#   8 collection_sort.cpp Q7: the comparator sort sorts the receiver in place (no copy-sort-commit)
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$ROOT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
plain_compile() { sed -nE 's#^bash scripts/build/compile-object\.sh (--production )?([^ ]+) -- (.*) -c ([^ ]+)$#\3 -MMD -c \4 -o \2#p'; }
if make -s -n nift | grep -qE -- " -o nift$"; then
    echo "FAIL b5 mutants: ./nift is not current (run make nift)" >&2; exit 1
fi
fail=0
for spec in 1:src/rt/infer_sites.cpp 3:src/rt/infer_session.cpp 4:src/rt/infer_session.cpp \
            6:src/rt/vm_ops.cpp 7:src/rt/compiler_array_mutation.cpp+src/rt/session_invocation.cpp \
            8:src/rt/collection_sort.cpp; do
    n=${spec%%:*}; src=${spec#*:}
    IFS=+ read -r -a srcs <<<"$src"
    link=""; compiles=()
    for one in "${srcs[@]}"; do
        obj=${one%.cpp}.o; mobj="$TMP/m$n-$(basename "$obj")"
        plan=$(make -s -n nift -W "$one")
        c=$(plain_compile <<<"$plan" | grep -F -- " -c $one -o $obj" | head -1 || true)
        [ -n "$link" ] || link=$(grep -E -- " -o nift$" <<<"$plan" | tail -1)
        if [ -z "$c" ] || [ -z "$link" ]; then echo "FAIL mutant $n: no build plan for $one (harness failure)" >&2; exit 1; fi
        compiles+=("${c/ -c $one -o $obj/ -DNIFT_B5_MUTANT=$n -MF $TMP/m.d -c $one -o $mobj}")
        link=${link/ $obj / $mobj }
    done
    link=${link% -o nift}" -o $TMP/nift-m$n"
    # the probe build's comparator-profile objects, so the sort fixtures run on any toolchain
    for probe in compiler compiler_collection_sort; do
        [ -f ".build/rt/refrep/probe/$probe.o" ] || { echo "FAIL b5 mutants: make .build/b4b-guardoff/nift-guardoff first" >&2; exit 1; }
        link=${link/ src\/rt\/$probe.o / .build\/rt\/refrep\/probe\/$probe.o }
    done
    ok=1
    for c in "${compiles[@]}"; do eval "$c" >>"$TMP/build$n.log" 2>&1 || ok=0; done
    [ $ok = 1 ] && { eval "$link" >>"$TMP/build$n.log" 2>&1 || ok=0; }
    if [ $ok = 0 ]; then
        echo "FAIL mutant $n: build failed (harness failure)" >&2; tail -20 "$TMP/build$n.log" >&2; exit 1
    fi
    caught=0
    python3 tests/runtime2/b5/run.py --nift "$TMP/nift-m$n" >"$TMP/test$n.log" 2>&1 || caught=1
    if [ $caught = 0 ]; then
        python3 tests/runtime2/refsem_a.py --nift "$TMP/nift-m$n" >>"$TMP/test$n.log" 2>&1 || caught=1
    fi
    if [ $caught = 0 ]; then
        echo "FAIL mutant $n ($src): the suites passed (not caught)"; fail=1
    else
        echo "PASS mutant $n ($src): caught, $(grep -c '^FAIL' "$TMP/test$n.log") failing check(s), first: $(grep -m1 '^FAIL' "$TMP/test$n.log" | cut -c1-110)"
    fi
    rm -f "$TMP/nift-m$n" "$TMP"/m$n-*.o
done
[ $fail = 0 ] && echo "PASS b5 mutants: all caught"
exit $fail
