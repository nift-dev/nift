#!/usr/bin/env bash
# Mutation controls for tests/runtime2/args_cmd/args_cmd.py: each mutant
# (-DNIFT_ARGSCMD_MUTANT=n, one translation unit recompiled and relinked with
# the tree's other objects, so `make nift` must be current) must fail the
# suite. A build failure is a harness failure, never a detection.
#   1 script_runner.cpp       scripts get no invocation bindings (binding removed)
#   2 session_invocation.cpp  `args` loses its last argument (wrong value; scripts and sessions)
#   3 compiler_call.cpp       `cmd(...)` calls the `cmd` string (the VM-session runtime bug)
#   5 compiler_invocation.cpp the bindings are writable (no readonly/const array)
#     Witnessed by classification (review of 2a1c228a, F1): `args.push(<array
#     or object>)` takes the const-array path and is refused by the guard, while
#     the writable binding's path refuses it as `aliasing`. Phase B must still
#     re-witness it through the const-mutation error
#     (tests/runtime2/refsem/pending/args_push_after_output.f, args_sort.f).
#   6 compiler_invocation.cpp no top-level shadowing (`args := []` refused again)
# Mutants 4, 7 and 8 (module rules) were retired with D-NEXT-packages #4: modules
# now see the entry's bindings where resolution proves it, and a module's own
# `args :=` is renamed internally (8's linked-unit scan can no longer see it).
# The module rules' mutation controls are tests/runtime2/packages/mutants.sh.
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
# Tripwire (args/cmd x the reference guard): each form must still be refused
# by the Phase A guard (reference-guard:*) on the real build, before any
# effect; if one is admitted, it becomes a direct witness of mutant 5 (the
# const-mutation error) and must be added as one.
tw="$TMP/tripwire"; mkdir -p "$tw"
tripwire_forms=(
  'print("e")\nargs.push("z")\n' 'args.sort()\n' 'args.pop()\nprint(args.size())\n'
  'fn(g()) { args.push("y") }\ng()\nprint(args.size())\n' 'f := () => args.push("y")\nf()\nprint(args.size())\n'
  'args.insert(0, "y")\nprint(args.size())\n' 'x := args.push("y")\nprint(x)\n' 'print(args.reverse().size())\n'
  'args.clear()\nprint(args.size())\n' 'args.remove(0)\nprint(args.size())\n'
  'fn(f(x)) { x.push(1) }\nf(args)\nprint(args.size())\n'
  'm := map()\nm.set("a", args)\nv := m.get("a")\nv.push(1)\nprint(args.size())\n')
session_forms=('args.sort()\nprint(args.size())\n' 'x := args\nx.push("z")\nprint(args.size())\n')
printf 'fn(grow(xs)) { xs.push("z")\n  return xs.size() }\nexport(grow)\n' > "$tw/m.f"
tripwire_forms+=('import("m.f")\nprint(grow(args))\nprint(args.size())\n')
for form in "${tripwire_forms[@]}"; do
    printf "$form" > "$tw/p.f"
    out=$(cd "$tw" && NIFT_VM=require "$ROOT/nift" p.f a b 2>&1 || true)
    if ! grep -q "not compiled for the VM (reference-guard:" <<<"$out" || grep -qv '^error: \|^note: ' <<<"$out"; then
        echo "FAIL tripwire: script form now admitted or refused otherwise: $(printf "$form" | tr '\n' ' ')"
        echo "     add this form to args_cmd.py as a witness of mutant 5: ${out:0:200}"; fail=1
    fi
done
for form in "${session_forms[@]}"; do
    out=$(cd "$tw" && printf "$form" | NIFT_VM=require "$ROOT/nift" -i -e 'k := 1' a b 2>&1 || true)
    if ! grep -q "NIFT_VM session refused: reference-guard:" <<<"$out"; then
        echo "FAIL tripwire: session form now admitted or refused otherwise: $(printf "$form" | tr '\n' ' ')"
        echo "     add this form to args_cmd.py as a witness of mutant 5: ${out:0:200}"; fail=1
    fi
done
[ $fail = 0 ] && echo "PASS tripwire: ${#tripwire_forms[@]} script and ${#session_forms[@]} session forms still refused by the guard"
for spec in 1:src/rt/script_runner.cpp 2:src/rt/session_invocation.cpp 3:src/rt/compiler_call.cpp \
            5:src/rt/compiler_invocation.cpp 6:src/rt/compiler_invocation.cpp; do
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
