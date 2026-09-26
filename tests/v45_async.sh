#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-${NIFT:-./nift}}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/async.f" <<'NIFT'
fn(add(a, b)) { return a + b }
fn(nested(x)) {
    f := async(add, x, 1)
    return await(f)
}
base := 10
closure := (x) => { return base + x }
a := async(add, 2, 3)
b := async(closure, 5)
base = 100
c := async(nested, 9)
print(await(a))
print(a.await())
print(b.await())
print(c.await())
print(a.done())
print(a.status())
print(type(a))
NIFT
out=$($NIFT "$TMP/async.f")
[[ "$out" == $'5\n5\n15\n10\ntrue\ndone\nasync' ]] || { printf 'unexpected async output:\n%s\n' "$out" >&2; exit 1; }

# A bounded pool must still make forward progress when workers await nested work.
cat >"$TMP/nested-many.f" <<'NIFT'
fn(inner(x)) { return x + 1 }
fn(outer(x)) {
    f := async(inner, x)
    return await(f)
}
a := async(outer, 1)
b := async(outer, 2)
c := async(outer, 3)
d := async(outer, 4)
print(await(a) + await(b) + await(c) + await(d))
NIFT
[[ "$($NIFT "$TMP/nested-many.f")" == '14' ]]

# Prove that at least two async jobs can be executing before either completes.
# The main runtime holds gate; both workers must increment started before gate is
# released. A serial event loop or single active worker cannot reach started=2.
cat >"$TMP/parallel.f" <<'NIFT'
fn(blocker(started, gate)) {
    started.lock()
    v := started.get()
    started.set(v + 1)
    started.unlock()
    gate.lock()
    gate.unlock()
    return true
}
started := mutex(0)
gate := mutex()
gate.lock()
a := async(blocker, started, gate)
b := async(blocker, started, gate)
seen := 0
spins := 0
while(seen < 2 && spins < 1000000) {
    started.lock()
    seen = started.get()
    started.unlock()
    spins += 1
}
print(seen)
gate.unlock()
print(await(a))
print(await(b))
NIFT
[[ "$($NIFT "$TMP/parallel.f")" == $'2\ntrue\ntrue' ]]

cat >"$TMP/error.f" <<'NIFT'
fn(bad()) { return 1 / 0 }
f := async(bad)
print(await(f))
NIFT
if $NIFT "$TMP/error.f" >"$TMP/out" 2>"$TMP/err"; then echo 'async error unexpectedly succeeded' >&2; exit 1; fi
grep -q 'async:' "$TMP/err"

cat >"$TMP/reject.f" <<'NIFT'
f := file("x.txt")
fn(id(x)) { return x }
a := async(id, [f])
NIFT
if $NIFT "$TMP/reject.f" >"$TMP/out" 2>"$TMP/err"; then echo 'async resource transfer unexpectedly succeeded' >&2; exit 1; fi
grep -q 'non-transferable resource' "$TMP/err"

echo 'v4.5 async/await smoke passed'
