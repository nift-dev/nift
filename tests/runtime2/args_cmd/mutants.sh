#!/usr/bin/env bash
# Mutation controls for tests/runtime2/args_cmd/args_cmd.py: each mutant
# (-DNIFT_ARGSCMD_MUTANT=n, one translation unit recompiled and relinked with
# the tree's other objects, so `make nift` must be current) must fail the
# suite. A build failure is a harness failure, never a detection.
#   1 script_runner.cpp       scripts get no invocation bindings (binding removed)
#   2 session_invocation.cpp  `args` loses its last argument (wrong value; scripts and sessions)
#   3 compiler_call.cpp       `cmd(...)` calls the `cmd` string (the VM-session runtime bug)
#   4 module_link.cpp         modules see the entry's bindings
#   5 compiler_invocation.cpp the bindings are writable (no readonly/const array)
#   6 compiler_invocation.cpp no top-level shadowing (`args := []` refused again)
#   7 module_link.cpp         no refusal of a module declaring `args`/`cmd` (the compiler still refuses it,
#                             with another reason)
#   8 module_link.cpp + session_invocation.cpp  the review defect back: the linked-unit scan takes a module's
#                             top-level `args :=` for the entry's replacement (and the linker check is off)
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$ROOT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# (`make -q` is unusable: the vendored libffi step always runs; a current tree plans no link.)
if make -s -n nift | grep -qE -- " -o nift$"; then
    echo "FAIL args/cmd mutants: ./nift is not current (run make nift)" >&2; exit 1
fi
fail=0
for spec in 1:src/rt/script_runner.cpp 2:src/rt/session_invocation.cpp 3:src/rt/compiler_call.cpp \
            4:src/rt/module_link.cpp 5:src/rt/compiler_invocation.cpp 6:src/rt/compiler_invocation.cpp \
            7:src/rt/module_link.cpp 8:src/rt/module_link.cpp+src/rt/session_invocation.cpp; do
    n=${spec%%:*}; src=${spec#*:}
    IFS=+ read -r -a srcs <<<"$src"
    link=""; compiles=()
    for one in "${srcs[@]}"; do
        obj=${one%.cpp}.o; mobj="$TMP/m$n-$(basename "$obj")"
        plan=$(make -s -n nift -W "$one")
        c=$(grep -F -- " -c $one -o $obj" <<<"$plan" | head -1)
        [ -n "$link" ] || link=$(grep -E -- " -o nift$" <<<"$plan" | tail -1)
        if [ -z "$c" ] || [ -z "$link" ]; then echo "FAIL mutant $n: no build plan for $one (harness failure)" >&2; exit 1; fi
        compiles+=("${c/ -c $one -o $obj/ -DNIFT_ARGSCMD_MUTANT=$n -MF $TMP/m.d -c $one -o $mobj}")
        link=${link/ $obj / $mobj }
    done
    link=${link% -o nift}" -o $TMP/nift-m$n"
    ok=1
    for c in "${compiles[@]}"; do eval "$c" >>"$TMP/build$n.log" 2>&1 || ok=0; done
    [ $ok = 1 ] && { eval "$link" >>"$TMP/build$n.log" 2>&1 || ok=0; }
    if [ $ok = 0 ]; then
        echo "FAIL mutant $n: build failed (harness failure)" >&2; tail -20 "$TMP/build$n.log" >&2; exit 1
    fi
    if python3 tests/runtime2/args_cmd/args_cmd.py --nift "$TMP/nift-m$n" >"$TMP/test$n.log" 2>&1; then
        echo "FAIL mutant $n ($src): the suite passed (not caught)"; fail=1
    else
        echo "PASS mutant $n ($src): caught, $(grep -c '^FAIL' "$TMP/test$n.log") failing check(s), first: $(grep -m1 '^FAIL' "$TMP/test$n.log" | cut -c1-110)"
    fi
    rm -f "$TMP/nift-m$n" "$TMP"/m$n-*.o
done
exit $fail
