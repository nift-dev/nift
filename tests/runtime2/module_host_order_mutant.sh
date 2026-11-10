#!/usr/bin/env bash
# R1 negative control: a build without the module-host-op refusal
# (-DNIFT_R1_MUTANT) must fail tests/runtime2/module_host_order.py. A build
# failure is a harness failure, never a detection.
# B15 (combined-b3-b5.md section 75): the mutant is built by
# tests/runtime2/sanctioned_mutant.py (only the units that name the macro, nift's
# own compile line, into .build/mutant-nift; every object differs from
# production), not by `make nift` with mutant CXXFLAGS in a copy of the tree,
# which the production build guard refuses. Run through make
# (test-runtime2-module-host-order-mutant), after `make nift`.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUT="$ROOT/.build/mutant-nift/r1"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP" "$OUT"' EXIT
if ! python3 -B "$ROOT/tests/runtime2/sanctioned_mutant.py" --out "$OUT" --define=-DNIFT_R1_MUTANT >"$TMP/build.log" 2>&1; then
    echo "FAIL R1 mutant: build failed (harness failure)" >&2
    tail -20 "$TMP/build.log" >&2
    exit 1
fi
cat "$TMP/build.log"
if python3 "$ROOT/tests/runtime2/module_host_order.py" --nift "$OUT/nift" >"$TMP/test.log" 2>&1; then
    echo "FAIL R1 mutant: module_host_order passed without the refusal (not caught)" >&2
    cat "$TMP/test.log" >&2
    exit 1
fi
grep -c '^FAIL' "$TMP/test.log" | sed 's/^/R1 mutant: failing cases: /'
echo "PASS R1 mutant: module_host_order rejected the build without the refusal"
