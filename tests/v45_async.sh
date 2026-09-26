#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-${NIFT:-./nift}}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/async.f" <<'NIFT'
@fn[async](add(a, b)) { return a + b }
base := 10
closure := async (x) => { return base + x }
a := add(2, 3)
b := closure(5)
base = 100
print(type(a))
print(await a)
print(await add(4, 5))
print(await b)
print(a.done())
print(a.status())
NIFT
out=$($NIFT "$TMP/async.f")
[[ "$out" == $'future\n5\n9\n15\ntrue\ndone' ]] || { printf 'unexpected async output:\n%s\n' "$out" >&2; exit 1; }

# Nested futures must make forward progress in the bounded worker pool.
cat >"$TMP/nested-many.f" <<'NIFT'
@fn[async](inner(x)) { return x + 1 }
@fn[async](outer(x)) { return await inner(x) }
a := outer(1)
b := outer(2)
c := outer(3)
d := outer(4)
ra := await a
rb := await b
rc := await c
rd := await d
print(ra + rb + rc + rd)
NIFT
[[ "$($NIFT "$TMP/nested-many.f")" == '14' ]]

# Async lambdas are first-class and return futures.
cat >"$TMP/lambda.f" <<'NIFT'
f := async (x, y) => { return x * y }
p := f(6, 7)
print(type(p))
print(await p)
NIFT
[[ "$($NIFT "$TMP/lambda.f")" == $'future\n42' ]]

cat >"$TMP/error.f" <<'NIFT'
@fn[async](bad()) { return 1 / 0 }
f := bad()
print(await f)
NIFT
if $NIFT "$TMP/error.f" >"$TMP/out" 2>"$TMP/err"; then echo 'async error unexpectedly succeeded' >&2; exit 1; fi
grep -q 'future:' "$TMP/err"

# Old public library-style spellings are deliberately gone.
if $NIFT -e 'fn(id(x)){return x}; p := async(id, 1)' >/dev/null 2>"$TMP/err"; then exit 1; fi
grep -q 'was removed' "$TMP/err"
if $NIFT -e '@fn[async](id(x)){return x}; p := id(1); x := await(p)' >/dev/null 2>"$TMP/err"; then exit 1; fi
grep -q 'was removed' "$TMP/err"

echo 'v4.5 async/future smoke passed'
