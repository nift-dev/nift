#!/usr/bin/env bash
# F1 negative control: build the current source with the caller-scope mutant
# (NIFT_INTERP_MUTANT=4) and prove that all five F1 regressions fail in each
# context. The mutant is test-only; normal builds contain no mutant code.
# B15 (combined-b3-b5.md section 75): built by tests/runtime2/sanctioned_mutant.py
# (only the changed units, nift's own compile line, into .build/mutant-nift; every
# mutant object differs from production), not by `make nift` with mutant CXXFLAGS
# in a copy of the tree, which the production build guard refuses. Run through
# make after `make nift`.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
ORACLE=${1:-"$ROOT/nift"}
TMP=$(mktemp -d)
BUILD="$ROOT/.build/mutant-nift/interp-f1"
trap 'rm -rf "$TMP" "$BUILD"' EXIT

if ! python3 -B "$ROOT/tests/runtime2/sanctioned_mutant.py" --out "$BUILD" --define=-DNIFT_INTERP_MUTANT=4 \
    >"$TMP/build.log" 2>&1; then
    echo "FAIL F1 negative control: defective build failed to compile (harness failure)" >&2
    tail -30 "$TMP/build.log" >&2
    exit 1
fi
if python3 "$ROOT/tests/runtime2/literals/run.py" --nift "$BUILD/nift" --oracle "$ORACLE" \
    >"$TMP/test.log" 2>&1; then
    echo "FAIL F1 negative control: the F1 regressions passed on the defective compiler" >&2
    cat "$TMP/test.log" >&2
    exit 1
fi
failures=$(grep -c '^FAIL f1_' "$TMP/test.log" || true)
if [ "$failures" -ne 15 ]; then
    echo "FAIL F1 negative control: expected 15 F1 failures, saw $failures (harness failure)" >&2
    cat "$TMP/test.log" >&2
    exit 1
fi
echo "PASS F1 negative control: caller-scope mutant rejected by all 15 F1 cases"
grep -E '^FAIL f1_' "$TMP/test.log" | head -6 || true
