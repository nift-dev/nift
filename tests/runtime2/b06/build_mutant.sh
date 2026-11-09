#!/usr/bin/env bash
# B06 (combined-b3-b5.md section 37): build mutant N (-DNIFT_B06_MUTANT=N, its translation unit
# recompiled and relinked with the tree's other objects; `make nift` must be current) as OUT.
# The probe build's comparator-profile objects are linked in, as in tests/runtime2/b5/mutants.sh,
# so the comparator fixtures run on any toolchain.
#   1 src/rt/infer_collection.cpp  an empty element type is proven only by the B3 positional rule
#   2 src/rt/infer_callable.cpp    a callee with no kind and no target yet is inexact (B4b 8 H1)
set -euo pipefail
n=${1:?usage: build_mutant.sh N OUT}; out=${2:?usage: build_mutant.sh N OUT}
case $n in 1) src=src/rt/infer_collection.cpp ;; 2) src=src/rt/infer_callable.cpp ;;
           *) echo "FAIL b06 mutant: unknown mutant $n" >&2; exit 2 ;; esac
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$ROOT"
if make -s -n nift | grep -qE -- " -o nift$"; then
    echo "FAIL b06 mutant: ./nift is not current (run make nift)" >&2; exit 1
fi
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
plain_compile() { sed -nE 's#^bash scripts/build/compile-object\.sh (--production )?([^ ]+) -- (.*) -c ([^ ]+)$#\3 -MMD -c \4 -o \2#p'; }
obj=${src%.cpp}.o; mobj="$TMP/m$n-$(basename "$obj")"
plan=$(make -s -n nift -W "$src")
c=$(plain_compile <<<"$plan" | grep -F -- " -c $src -o $obj" | head -1 || true)
link=$(grep -E -- " -o nift$" <<<"$plan" | tail -1)
[ -n "$c" ] && [ -n "$link" ] || { echo "FAIL b06 mutant $n: no build plan for $src (harness failure)" >&2; exit 1; }
c=${c/ -c $src -o $obj/ -DNIFT_B06_MUTANT=$n -MF $TMP/m.d -c $src -o $mobj}
link=${link/ $obj / $mobj }
link=${link% -o nift}" -o $out"
for probe in compiler compiler_collection_sort; do
    [ -f ".build/rt/refrep/probe/$probe.o" ] || { echo "FAIL b06 mutant: make .build/b4b-guardoff/nift-guardoff first" >&2; exit 1; }
    link=${link/ src\/rt\/$probe.o / .build\/rt\/refrep\/probe\/$probe.o }
done
eval "$c" >"$TMP/build.log" 2>&1 && eval "$link" >>"$TMP/build.log" 2>&1 || {
    echo "FAIL b06 mutant $n: build failed (harness failure)" >&2; tail -20 "$TMP/build.log" >&2; exit 1; }
