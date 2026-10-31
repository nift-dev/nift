#!/usr/bin/env bash
# Mutation controls for tests/runtime2/packages/packages.py: each mutant
# (-DNIFT_PACKAGES_MUTANT=n, one translation unit recompiled and relinked with
# the tree's other objects, so `make nift` must be current) must fail the
# suite. A build failure is a harness failure, never a detection.
#   1 package_resolve.cpp     the importing script's directory is the project (not cwd)
#   2 package_resolve.cpp     legacy's acquire_read: `.nift/` is created on lookup
#   3 PackageTransaction.cpp  the lock file is truncated on open (O_TRUNC)
#   4 package_resolve.cpp     the lock is never released (held while the program runs)
#   5 package_resolve.cpp     no lock is taken (nor the journal checked)
#   6 module_graph.cpp        every frame prefix spelled `import: `
#   7 module_invocation.cpp   module top-level reads of the entry's args admitted
#   8 module_invocation.cpp   the init-reach closure is not followed (no fixed point)
#   9 module_invocation.cpp   rename by spelling (modules without their own args renamed too)
#  10 module_graph.cpp        no package-relative containment
#  11 module_graph.cpp        a feature refusal preferred over a later program error
#  12 module_graph.cpp        statements checked in reverse order (competing errors reversed)
#  13 module_link.cpp         the clash reported is the last in text, not the first (order rule).
#                             A literal hash-iteration mutant is not observable with libstdc++ here:
#                             collect() inserts in reverse text order and the set iterates newest
#                             first, so its order coincides with text order; the order rule is what
#                             the reported position must follow, and this mutant breaks it.
#  14 module_link.cpp         generated names shown in messages (no display names)
#  15 module_link.cpp         a fixed generated name, never checked for collisions
#  16 module_graph.cpp        no frozen-provenance check (relative import below an absolute one)
#  17 module_scan.cpp         modules not prevalidated at load (syntax errors no longer stop the walk)
#  18 module_link.cpp         display names replaced as substrings (nift__args__m1 inside ..m11)
#  19 module_scan.cpp         the early-return note for any nested `return`
#  20 module_link_prefix.cpp  no entry prefix (earlier invocation-init / compile errors lose)
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$ROOT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
if make -s -n nift | grep -qE -- " -o nift$"; then
    echo "FAIL packages mutants: ./nift is not current (run make nift)" >&2; exit 1
fi
fail=0
for spec in 1:src/rt/package_resolve.cpp 2:src/rt/package_resolve.cpp 3:src/PackageTransaction.cpp \
            4:src/rt/package_resolve.cpp 5:src/rt/package_resolve.cpp 6:src/rt/module_graph.cpp \
            7:src/rt/module_invocation.cpp 8:src/rt/module_invocation.cpp 9:src/rt/module_invocation.cpp \
            10:src/rt/module_graph.cpp 11:src/rt/module_graph.cpp 12:src/rt/module_graph.cpp \
            13:src/rt/module_link.cpp 14:src/rt/module_link.cpp 15:src/rt/module_link.cpp \
            16:src/rt/module_graph.cpp 17:src/rt/module_scan.cpp 18:src/rt/module_link.cpp \
            19:src/rt/module_scan.cpp 20:src/rt/module_link_prefix.cpp; do
    n=${spec%%:*}; one=${spec#*:}
    obj=${one%.cpp}.o; mobj="$TMP/m$n-$(basename "$obj")"
    plan=$(make -s -n nift -W "$one")
    c=$(grep -F -- " -c $one -o $obj" <<<"$plan" | head -1)
    link=$(grep -E -- " -o nift$" <<<"$plan" | tail -1)
    if [ -z "$c" ] || [ -z "$link" ]; then echo "FAIL mutant $n: no build plan for $one (harness failure)" >&2; exit 1; fi
    c=${c/ -c $one -o $obj/ -DNIFT_PACKAGES_MUTANT=$n -MF $TMP/m.d -c $one -o $mobj}
    link=${link/ $obj / $mobj }
    link=${link% -o nift}" -o $TMP/nift-m$n"
    if ! { eval "$c" && eval "$link"; } >>"$TMP/build$n.log" 2>&1; then
        echo "FAIL mutant $n: build failed (harness failure)" >&2; tail -20 "$TMP/build$n.log" >&2; exit 1
    fi
    if python3 tests/runtime2/packages/packages.py --nift "$TMP/nift-m$n" >"$TMP/test$n.log" 2>&1; then
        echo "FAIL mutant $n ($one): the suite passed (not caught)"; fail=1
    else
        echo "PASS mutant $n ($one): caught, $(grep -c '^FAIL' "$TMP/test$n.log") failing check(s), first: $(grep -m1 '^FAIL' "$TMP/test$n.log" | cut -c1-110)"
    fi
    rm -f "$TMP/nift-m$n" "$TMP"/m$n-*.o
done
exit $fail
