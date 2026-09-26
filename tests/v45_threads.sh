#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-${NIFT:-./nift}}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/threads.f" <<'NIFT'
fn(add(a, b)) { return a + b }
fn(nested(x)) {
    inner := thread(add, x, 1)
    return inner.join()
}
base := 10
closure := (x) => { return base + x }
t1 := thread(add, 2, 3)
t2 := thread(closure, 5)
base = 100
t3 := thread(nested, 9)
print(t1.join())
print(t1.join())
print(t2.join())
print(t3.join())
print(t1.done())
print(t1.status())
print(hardware_concurrency() >= 1)
NIFT
out=$($NIFT "$TMP/threads.f")
expected=$'5\n5\n15\n10\ntrue\ndone\ntrue'
[[ "$out" == "$expected" ]] || { printf 'unexpected output:\n%s\n' "$out" >&2; exit 1; }

cat >"$TMP/handle-transfer.f" <<'NIFT'
fn(id(x)) { return x }
fn(joiner(h)) { return h.join() }
t := thread(id, 7)
j := thread(joiner, t)
print(j.join())
print(t.join())
NIFT
[[ "$($NIFT "$TMP/handle-transfer.f")" == $'7\n7' ]]

cat >"$TMP/error.f" <<'NIFT'
fn(bad()) { return 1 / 0 }
t := thread(bad)
print(t.join())
NIFT
if $NIFT "$TMP/error.f" >"$TMP/out" 2>"$TMP/err"; then
    echo 'thread error unexpectedly succeeded' >&2; exit 1
fi
grep -q 'thread:' "$TMP/err"

cat >"$TMP/reject.f" <<'NIFT'
f := file("x.txt")
fn(id(x)) { return x }
t := thread(id, [1, f])
NIFT
if $NIFT "$TMP/reject.f" >"$TMP/out" 2>"$TMP/err"; then
    echo 'non-transferable resource unexpectedly crossed thread boundary' >&2; exit 1
fi
grep -q 'non-transferable resource' "$TMP/err"

echo 'v4.5 thread primitive smoke passed'
