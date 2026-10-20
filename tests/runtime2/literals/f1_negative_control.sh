#!/usr/bin/env bash
# F1 negative control: build the current source with the caller-scope mutant
# (NIFT_INTERP_MUTANT=4) and prove that all five F1 regressions fail in each
# context. The mutant is test-only; normal builds contain no mutant code.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
ORACLE=${1:-"$ROOT/nift"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

BUILD="$TMP/build"
mkdir -p "$BUILD"
git -C "$ROOT" archive HEAD | tar -x -C "$BUILD"
if ! make -C "$BUILD" -j"${JOBS:-4}" nift \
    CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread -DNIFT_INTERP_MUTANT=4" \
    >"$TMP/build.log" 2>&1; then
    echo "FAIL F1 negative control: defective build failed to compile (harness failure)" >&2
    tail -30 "$TMP/build.log" >&2
    exit 1
fi
if python3 "$BUILD/tests/runtime2/literals/run.py" --nift "$BUILD/nift" --oracle "$ORACLE" \
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
