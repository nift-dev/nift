#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT:-./nift}
case "$NIFT" in /*) BIN="$NIFT";; *) BIN="$(pwd)/$NIFT";; esac

out=$("$BIN" -e '
empty := secure_random_bytes(0)
a := secure_random_bytes(32)
b := secure_random_bytes(32)
print(type(empty)); print(empty.size()); print(a.size()); print(a == b)
')
[ "$(sed -n '1p' <<<"$out")" = bytes ]
[ "$(sed -n '2p' <<<"$out")" = 0 ]
[ "$(sed -n '3p' <<<"$out")" = 32 ]
[ "$(sed -n '4p' <<<"$out")" = false ]

for expression in 'secure_random_bytes()' 'secure_random_bytes(1, 2)' 'secure_random_bytes(-1)' 'secure_random_bytes(0.5)' 'secure_random_bytes("1")' 'secure_random_bytes(null)' 'secure_random_bytes(10000001)'; do
  if "$BIN" -e "$expression" >/dev/null 2>&1; then
    echo "accepted invalid secure random expression: $expression" >&2
    exit 1
  fi
done

prepared=$("$BIN" -e '
calls := 0
counts := [8, 8]
fn(next()) { old := calls; calls += 1; return old }
fn(generate_once()) { return secure_random_bytes(counts[next()]) }
i := 0
size := 0
while(i < 1) { result := generate_once(); size = result.size(); i += 1 }
print(calls); print(size)
')
[ "$(sed -n '1p' <<<"$prepared")" = 1 ]
[ "$(sed -n '2p' <<<"$prepared")" = 8 ]

NIFT_NO_PROCESS=1 "$BIN" -e 'print(secure_random_bytes(1).size())' | grep -qx 1

echo 'PASS v4.6 secure random bytes'
