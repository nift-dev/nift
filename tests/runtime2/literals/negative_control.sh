#!/usr/bin/env bash
# Permanent negative controls for CP15B literal/interpolation tests.
# Each mutant is built from HEAD in a disposable copy; a mutant build failure is
# a harness failure, not a successful catch. The literal tests must exit nonzero
# for every mutant.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
ORACLE=${1:-"$ROOT/nift"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

for mutant in 1 2 3 4; do
    build="$TMP/m$mutant"
    mkdir -p "$build"
    git -C "$ROOT" archive HEAD | tar -x -C "$build"
    if ! make -C "$build" -j"${JOBS:-4}" nift \
        CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread -DNIFT_INTERP_MUTANT=$mutant" \
        >"$TMP/build-$mutant.log" 2>&1; then
        echo "FAIL mutant $mutant: build failed (harness failure)" >&2
        tail -30 "$TMP/build-$mutant.log" >&2
        exit 1
    fi
    if python3 "$build/tests/runtime2/literals/run.py" --nift "$build/nift" --oracle "$ORACLE" \
        >"$TMP/test-$mutant.log" 2>&1; then
        echo "FAIL mutant $mutant: literal tests passed (mutant not caught)" >&2
        cat "$TMP/test-$mutant.log" >&2
        exit 1
    fi
    echo "PASS mutant $mutant: literal tests rejected the mutant"
done
echo "PASS negative controls: 4/4 mutants rejected"
