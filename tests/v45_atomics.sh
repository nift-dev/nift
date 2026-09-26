#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-${NIFT:-./nift}}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/atomics.f" <<'NIFT'
fn(inc(x, n)) {
    i := 0
    while(i < n) {
        x.fetch_add(1)
        i += 1
    }
    return true
}
fn(set_flag(flag)) { flag.store(true); return true }

x := atomic<int>(0)
print(type(x))
print(x.load())
x.store(5)
print(x.exchange(7))
print(x.fetch_add(2))
print(x.fetch_sub(1))
print(x.load())
print(x.compare_exchange(8, 11))
print(x.load())

y := atomic<int>(9007199254740993)
print(y.load())

counter := atomic<int>(0)
a := thread(inc, counter, 1000)
b := thread(inc, counter, 1000)
c := async(inc, counter, 1000)
d := async(inc, counter, 1000)
a.join(); b.join(); await(c); await(d)
print(counter.load())

flag := atomic<bool>(false)
print(type(flag))
print(flag.load())
t := thread(set_flag, flag)
t.join()
print(flag.load())
print(flag.exchange(false))
print(flag.compare_exchange(false, true))
print(flag.load())
NIFT
out=$($NIFT "$TMP/atomics.f")
expected=$'atomic<int>\n0\n5\n7\n9\n8\ntrue\n11\n9007199254740993\n4000\natomic<bool>\nfalse\ntrue\ntrue\ntrue\ntrue'
[[ "$out" == "$expected" ]] || { printf 'unexpected atomics output:\n%s\n' "$out" >&2; exit 1; }

cat >"$TMP/bad-int.f" <<'NIFT'
x := atomic<int>(1.5)
NIFT
if $NIFT "$TMP/bad-int.f" >"$TMP/out" 2>"$TMP/err"; then echo 'fractional atomic<int> unexpectedly succeeded' >&2; exit 1; fi
grep -q 'signed 64-bit integer' "$TMP/err"

cat >"$TMP/bad-bool.f" <<'NIFT'
x := atomic<bool>(0)
NIFT
if $NIFT "$TMP/bad-bool.f" >"$TMP/out" 2>"$TMP/err"; then echo 'numeric atomic<bool> unexpectedly succeeded' >&2; exit 1; fi
grep -q 'initial value must be bool' "$TMP/err"

cat >"$TMP/bad-op.f" <<'NIFT'
x := atomic<bool>(false)
x.fetch_add(1)
NIFT
if $NIFT "$TMP/bad-op.f" >"$TMP/out" 2>"$TMP/err"; then echo 'fetch_add on atomic<bool> unexpectedly succeeded' >&2; exit 1; fi
grep -q 'only valid for atomic<int>' "$TMP/err"

echo 'v4.5 atomics smoke passed'
