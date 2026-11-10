#!/usr/bin/env bash
# Decimal-inference negative control: keep the Decimal decoder but compile out
# the number-literal classification. The typed-parameter regressions must then
# fail with visible silent divergences. A build failure is a harness failure.
# B15 (combined-b3-b5.md section 75): built by tests/runtime2/sanctioned_mutant.py
# (only the changed units, nift's own compile line, into .build/mutant-nift; every
# mutant object differs from production), not by `make nift` with mutant CXXFLAGS
# in a copy of the tree, which the production build guard refuses. Run through
# make after `make nift`.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
ORACLE=${1:-"$ROOT/nift"}
TMP=$(mktemp -d)
BUILD="$ROOT/.build/mutant-nift/decimal-infer"
trap 'rm -rf "$TMP" "$BUILD"' EXIT

# The mutant (tests/runtime2/literals/mutants/decimal-infer.patch) removes the Decimal branch of
# Inference::number_literal (src/rt/infer_number.cpp since dcdea681 moved it out of infer.cpp; it used to
# delete the one `v.is(Kind::Decimal)` line of infer.cpp). A patch that no longer applies is a
# harness failure.
if ! python3 -B "$ROOT/tests/runtime2/sanctioned_mutant.py" --out "$BUILD" \
    --patch "$ROOT/tests/runtime2/literals/mutants/decimal-infer.patch" >"$TMP/build.log" 2>&1; then
    echo "FAIL decimal-inference negative control: defective build failed to compile (harness failure)" >&2
    tail -30 "$TMP/build.log" >&2
    exit 1
fi
if python3 "$ROOT/tests/runtime2/literals/decimal_infer.py" --nift "$BUILD/nift" --oracle "$ORACLE" \
    >"$TMP/test.log" 2>&1; then
    echo "FAIL decimal-inference negative control: the regressions passed without the inference hunk" >&2
    cat "$TMP/test.log" >&2
    exit 1
fi
failures=$(grep -c '^FAIL decimal_param_' "$TMP/test.log" || true)
if [ "$failures" -ne 6 ]; then
    echo "FAIL decimal-inference negative control: expected 6 silent divergences, saw $failures (harness failure)" >&2
    cat "$TMP/test.log" >&2
    exit 1
fi
if ! grep -q 'SILENT DIVERGENCE' "$TMP/test.log"; then
    echo "FAIL decimal-inference negative control: no silent divergence was demonstrated (harness failure)" >&2
    cat "$TMP/test.log" >&2
    exit 1
fi
echo "PASS decimal-inference negative control: decoder-only build shows 6 silent divergences"
grep '^FAIL decimal_param_' "$TMP/test.log" | head -3 || true
