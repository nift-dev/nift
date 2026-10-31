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
            4:src/rt/module_link.cpp 5:src/rt/compiler_invocation.cpp 6:src/rt/compiler_invocation.cpp; do
    n=${spec%%:*}; src=${spec#*:}; obj=${src%.cpp}.o
    plan=$(make -s -n nift -W "$src")
    compile=$(grep -F -- " -c $src -o $obj" <<<"$plan" | head -1)
    link=$(grep -E -- " -o nift$" <<<"$plan" | tail -1)
    if [ -z "$compile" ] || [ -z "$link" ]; then echo "FAIL mutant $n: no build plan for $src (harness failure)" >&2; exit 1; fi
    compile=${compile/ -c $src -o $obj/ -DNIFT_ARGSCMD_MUTANT=$n -MF $TMP/m.d -c $src -o $TMP/m$n.o}
    link=${link/ $obj / $TMP/m$n.o }
    link=${link% -o nift}" -o $TMP/nift-m$n"
    if ! (eval "$compile" && eval "$link") >"$TMP/build$n.log" 2>&1; then
        echo "FAIL mutant $n: build failed (harness failure)" >&2; tail -20 "$TMP/build$n.log" >&2; exit 1
    fi
    if python3 tests/runtime2/args_cmd/args_cmd.py --nift "$TMP/nift-m$n" >"$TMP/test$n.log" 2>&1; then
        echo "FAIL mutant $n ($src): the suite passed (not caught)"; fail=1
    else
        echo "PASS mutant $n ($src): caught, $(grep -c '^FAIL' "$TMP/test$n.log") failing check(s), first: $(grep -m1 '^FAIL' "$TMP/test$n.log" | cut -c1-110)"
    fi
    rm -f "$TMP/nift-m$n" "$TMP/m$n.o"
done
exit $fail
