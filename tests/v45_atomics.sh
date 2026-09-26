#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-${NIFT:-./nift}}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/atomics.f" <<'NIFT'
fn(inc_thread(x, n)) { i := 0; while(i < n) { x++; i += 1 }; return true }
@fn[async](inc_async(x, n)) { i := 0; while(i < n) { x += 1; i += 1 }; return true }

x := atomic<int>(10)
print(type(x))
print(x)
print(x++)
print(++x)
x += 5
x -= 2
x &= 6
x |= 8
x ^= 3
x %= 5
print(x)
x = 17
print(x)
print(x + 2)
print(x == 17)
print(x.load())

counter := atomic<int>(0)
a := thread(inc_thread, counter, 1000)
b := thread(inc_thread, counter, 1000)
c := inc_async(counter, 1000)
d := inc_async(counter, 1000)
a.join(); b.join()
rc := await c
rd := await d
print(counter)

flag := atomic<bool>(false)
print(type(flag))
print(flag)
flag = true
print(flag)
print(flag.load())
NIFT
out=$($NIFT "$TMP/atomics.f")
expected=$'atomic<int>\n10\n10\n12\n3\n17\n19\ntrue\n17\n4000\natomic<bool>\nfalse\ntrue\ntrue'
[[ "$out" == "$expected" ]] || { printf 'unexpected atomics output:\n%s\n' "$out" >&2; exit 1; }

if $NIFT -e 'x := atomic<int>(1.5)' >/dev/null 2>"$TMP/err"; then exit 1; fi
grep -q 'signed 64-bit integer' "$TMP/err"
if $NIFT -e 'x := atomic<bool>(0)' >/dev/null 2>"$TMP/err"; then exit 1; fi
grep -q 'initial value must be bool' "$TMP/err"

echo 'v4.5 atomics smoke passed'
