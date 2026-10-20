#!/usr/bin/env bash
# Decimal-inference negative control: keep the Decimal decoder but compile out
# the number-literal classification. The typed-parameter regressions must then
# fail with visible silent divergences. A build failure is a harness failure.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
ORACLE=${1:-"$ROOT/nift"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

BUILD="$TMP/build"
mkdir -p "$BUILD"
git -C "$ROOT" archive HEAD | tar -x -C "$BUILD"
python3 - "$BUILD/src/rt/infer.cpp" <<'PY'
import sys
path = sys.argv[1]
lines = open(path, encoding="utf-8").read().splitlines(keepends=True)
kept = [line for line in lines if "v.is(Kind::Decimal)" not in line]
if len(kept) != len(lines) - 1:
    raise SystemExit("expected exactly one Decimal inference line")
open(path, "w", encoding="utf-8").writelines(kept)
PY
if ! make -C "$BUILD" -j"${JOBS:-4}" nift >"$TMP/build.log" 2>&1; then
    echo "FAIL decimal-inference negative control: defective build failed to compile (harness failure)" >&2
    tail -30 "$TMP/build.log" >&2
    exit 1
fi
if python3 "$BUILD/tests/runtime2/literals/decimal_infer.py" --nift "$BUILD/nift" --oracle "$ORACLE" \
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
