#!/usr/bin/env bash
# Owner 2026-11-10 13:26, item 4 (combined-b3-b5.md section 81): every check reports its own FAIL
# line by name and the script goes on, exiting non-zero at the end; under set -e a failing run or
# bare test used to abort it silently.
set -euo pipefail

NIFT=${NIFT:-./nift}
case "$NIFT" in /*) BIN="$NIFT";; *) BIN="$(pwd)/$NIFT";; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
failed=0
fail() { echo "FAIL $*" >&2; failed=1; }
run() { local rc=0; "$BIN" "$@" >"$TMP/out" 2>"$TMP/err" || rc=$?; return "$rc"; }

if run -e 'print("out"); err("problem")'; then
    [[ "$(cat "$TMP/out")" == out ]] || fail "print/err separation: stdout [$(cat "$TMP/out")]"
    [[ "$(cat "$TMP/err")" == problem ]] || fail "print/err separation: stderr [$(cat "$TMP/err")]"
else
    fail "print/err separation: exit $?: $(cat "$TMP/err")"
fi

if run -e 'err()'; then
    fail 'err() without a value unexpectedly succeeded'
else
    [[ ! -s "$TMP/out" ]] || fail "err() without a value: stdout [$(cat "$TMP/out")]"
    grep -q 'err: expected one value' "$TMP/err" || fail "err() without a value: stderr [$(cat "$TMP/err")]"
fi

if run -e 'err("x".encode("utf-8"))'; then
    fail 'err(bytes) unexpectedly succeeded'
else
    grep -q 'err: bytes values cannot be rendered as text' "$TMP/err" || fail "err(bytes): stderr [$(cat "$TMP/err")]"
fi

cat >"$TMP/workers.f" <<'NIFT'
fn(worker()) { print("thread-out"); err("thread-err"); return 1 }
@fn[async](future()) { print("async-out"); err("async-err"); return 2 }
t := thread(worker)
f := future()
print(t.join())
print(await f)
NIFT
if run "$TMP/workers.f"; then
    for line in thread-out async-out 1 2; do grep -qx "$line" "$TMP/out" || fail "worker output: stdout lacks [$line]"; done
    for line in thread-err async-err; do grep -qx "$line" "$TMP/err" || fail "worker output: stderr lacks [$line]"; done
else
    fail "worker output: exit $?: $(cat "$TMP/err")"
fi

[ "$failed" = 0 ] || { echo 'FAIL v4.6 execution output separation' >&2; exit 1; }
echo 'PASS v4.6 execution output separation'
