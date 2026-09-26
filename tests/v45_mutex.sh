#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-${NIFT:-./nift}}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/mutex.f" <<'NIFT'
fn(inc(m, n)) {
    i := 0
    while(i < n) {
        m.lock()
        v := m.get()
        m.set(v + 1)
        m.unlock()
        i += 1
    }
    return true
}
m := mutex(0)
a := thread(inc, m, 250)
b := thread(inc, m, 250)
c := thread(inc, m, 250)
d := thread(inc, m, 250)
a.join(); b.join(); c.join(); d.join()
m.lock()
print(m.get())
print(m.locked())
m.unlock()
print(m.locked())
print(m.try_lock())
print(m.try_lock())
m.unlock()
print(type(m))
NIFT
out=$($NIFT "$TMP/mutex.f")
[[ "$out" == $'1000\ntrue\nfalse\ntrue\nfalse\nmutex' ]] || { printf 'unexpected mutex output:\n%s\n' "$out" >&2; exit 1; }

cat >"$TMP/recursive.f" <<'NIFT'
m := mutex()
m.lock()
m.lock()
NIFT
if $NIFT "$TMP/recursive.f" >"$TMP/out" 2>"$TMP/err"; then echo 'recursive lock unexpectedly succeeded' >&2; exit 1; fi
grep -q 'recursive lock is not supported' "$TMP/err"

cat >"$TMP/unowned.f" <<'NIFT'
m := mutex(1)
print(m.get())
NIFT
if $NIFT "$TMP/unowned.f" >"$TMP/out" 2>"$TMP/err"; then echo 'unowned get unexpectedly succeeded' >&2; exit 1; fi
grep -q 'requires the current thread to hold the lock' "$TMP/err"

cat >"$TMP/wrong-owner.f" <<'NIFT'
fn(bad_unlock(m)) { m.unlock(); return true }
m := mutex(0)
m.lock()
t := thread(bad_unlock, m)
t.join()
NIFT
if $NIFT "$TMP/wrong-owner.f" >"$TMP/out" 2>"$TMP/err"; then echo 'wrong-owner unlock unexpectedly succeeded' >&2; exit 1; fi
grep -q 'unlock by non-owner' "$TMP/err"

echo 'v4.5 mutex smoke passed'
