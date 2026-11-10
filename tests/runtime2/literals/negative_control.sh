#!/usr/bin/env bash
# Permanent negative controls for CP15B literal/interpolation tests.
# Each mutant is built from HEAD in a disposable copy; a mutant build failure is
# a harness failure, not a successful catch. The literal tests must exit nonzero
# for every mutant.
set -euo pipefail

# B15 (combined-b3-b5.md section 75): built by tests/runtime2/sanctioned_mutant.py
# (only the changed units, nift's own compile line, into .build/mutant-nift; every
# mutant object differs from production), not by `make nift` with mutant CXXFLAGS
# in a copy of the tree, which the production build guard refuses. Run through
# make after `make nift`.
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
ORACLE=${1:-"$ROOT/nift"}
TMP=$(mktemp -d)
OUT="$ROOT/.build/mutant-nift/interp"
trap 'rm -rf "$TMP" "$OUT"' EXIT

for mutant in 1 2 3 4; do
    if ! python3 -B "$ROOT/tests/runtime2/sanctioned_mutant.py" --out "$OUT" --define=-DNIFT_INTERP_MUTANT=$mutant \
        >"$TMP/build-$mutant.log" 2>&1; then
        echo "FAIL mutant $mutant: build failed (harness failure)" >&2
        tail -30 "$TMP/build-$mutant.log" >&2
        exit 1
    fi
    grep '^built mutant' "$TMP/build-$mutant.log"
    if python3 "$ROOT/tests/runtime2/literals/run.py" --nift "$OUT/nift" --oracle "$ORACLE" \
        >"$TMP/test-$mutant.log" 2>&1; then
        echo "FAIL mutant $mutant: literal tests passed (mutant not caught)" >&2
        cat "$TMP/test-$mutant.log" >&2
        exit 1
    fi
    echo "PASS mutant $mutant: literal tests rejected the mutant"
done
echo "PASS negative controls: 4/4 mutants rejected"
