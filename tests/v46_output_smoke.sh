#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT:-./nift}
case "$NIFT" in /*) BIN="$NIFT";; *) BIN="$(pwd)/$NIFT";; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

"$BIN" -e 'print("out"); err("problem")' >"$TMP/out" 2>"$TMP/err"
[[ "$(cat "$TMP/out")" == out ]]
[[ "$(cat "$TMP/err")" == problem ]]

if "$BIN" -e 'err()' >"$TMP/out" 2>"$TMP/err"; then
    echo 'err() without a value unexpectedly succeeded' >&2
    exit 1
fi
[[ ! -s "$TMP/out" ]]
grep -q 'err: expected one value' "$TMP/err"

if "$BIN" -e 'err("x".encode("utf-8"))' >"$TMP/out" 2>"$TMP/err"; then
    echo 'err(bytes) unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'err: bytes values cannot be rendered as text' "$TMP/err"

cat >"$TMP/workers.f" <<'NIFT'
fn(worker()) { print("thread-out"); err("thread-err"); return 1 }
@fn[async](future()) { print("async-out"); err("async-err"); return 2 }
t := thread(worker)
f := future()
print(t.join())
print(await f)
NIFT
"$BIN" "$TMP/workers.f" >"$TMP/out" 2>"$TMP/err"
grep -qx 'thread-out' "$TMP/out"
grep -qx 'async-out' "$TMP/out"
grep -qx '1' "$TMP/out"
grep -qx '2' "$TMP/out"
grep -qx 'thread-err' "$TMP/err"
grep -qx 'async-err' "$TMP/err"

echo 'PASS v4.6 execution output separation'
