#!/usr/bin/env bash
# F1 negative control: the new shadowing regressions must fail on 560d9532
# (the reviewed Stage A tip before the caller-scope correction). Build that
# commit in a disposable copy, then run the current regression test against it.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
ORACLE=${1:-"$ROOT/nift"}
OLD_COMMIT=560d95321eb3410355db9783daf783e8099c0e0b
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/old"
git -C "$ROOT" archive "$OLD_COMMIT" | tar -x -C "$TMP/old"
if ! make -C "$TMP/old" -j"${JOBS:-4}" nift >"$TMP/build.log" 2>&1; then
    echo "FAIL F1 negative control: old tip did not build (harness failure)" >&2
    tail -20 "$TMP/build.log" >&2
    exit 1
fi
if python3 "$ROOT/tests/runtime2/literals/run.py" --nift "$TMP/old/nift" --oracle "$ORACLE" \
    >"$TMP/test.log" 2>&1; then
    echo "FAIL F1 negative control: the regressions passed on the old tip" >&2
    cat "$TMP/test.log" >&2
    exit 1
fi
echo "PASS F1 negative control: old tip 560d9532 is rejected by the new regressions"
grep -E '^FAIL f1_' "$TMP/test.log" | head -6 || true
