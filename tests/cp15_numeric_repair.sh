#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT:-./nift}
NIFT=$(cd "$(dirname "$NIFT")" && pwd)/$(basename "$NIFT")
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

actual=$("$NIFT" -e 'x := 1e-1000; print(!x); print(-x); print(+1e-1000); print(type(x)); s := set(); s.add(1e-1000); s.add(10e-1001); print(s.size()); m := map(); m.set(1.2300e5, "a"); m.set(123000, "b"); print(m.size()); print(m.get(123000))')
expected=$'false\n-1e-1000\n1e-1000\nfloat\n1\n1\nb'
[[ "$actual" == "$expected" ]]

actual=$("$NIFT" -e 'score := 5; E := 9; print(score-1); print(E-1); print(score - 1); print(1e-1); print(1e+1)')
expected=$'4\n8\n4\n0.1\n10'
[[ "$actual" == "$expected" ]]

if "$NIFT" -e 'print([1][1e-1000])' >"$tmp/out" 2>"$tmp/error"; then exit 1; fi
if "$NIFT" -e 'print([1].take(999999999999999999999999999999))' >"$tmp/out" 2>"$tmp/error"; then exit 1; fi
if "$NIFT" -e 'print(range(9223372036854775808))' >"$tmp/out" 2>"$tmp/error"; then exit 1; fi
grep -q 'signed 64-bit integers' "$tmp/error"

if "$NIFT" -e 'print(1 / 1e-1000)' >"$tmp/out" 2>"$tmp/error"; then exit 1; fi
grep -q 'arithmetic result is not finite' "$tmp/error"
if grep -q 'division by zero' "$tmp/error"; then exit 1; fi

printf '{"x":1,"x":2}\n' >"$tmp/duplicate.json"
printf '@json(data, "duplicate.json")\nprint(data.x)\n' >"$tmp/duplicate.f"
if (cd "$tmp" && "$NIFT" duplicate.f) >"$tmp/out" 2>"$tmp/error"; then exit 1; fi
grep -q 'duplicate object key' "$tmp/error"

echo 'CP15 numeric repair smoke: PASS'
