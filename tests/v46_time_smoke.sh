#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT:-./nift}
case "$NIFT" in /*) BIN="$NIFT";; *) BIN="$(pwd)/$NIFT";; esac

before=$(( $(date +%s) * 1000 ))
out=$("$BIN" -e 'print(type(epoch())); print(epoch()); print(type(sleep(0)))')
after=$(( $(date +%s) * 1000 + 2000 ))
epoch_type=$(sed -n '1p' <<<"$out")
epoch_value=$(sed -n '2p' <<<"$out")
sleep_type=$(sed -n '3p' <<<"$out")
[ "$epoch_type" = int ]
[[ "$epoch_value" =~ ^[0-9]+$ ]]
[ "$epoch_value" -ge $((before - 2000)) ]
[ "$epoch_value" -le "$after" ]
[ "$sleep_type" = null ]

start=$(date +%s%N 2>/dev/null || true)
"$BIN" -e 'sleep(25)' >/dev/null
end=$(date +%s%N 2>/dev/null || true)
if [[ "$start" =~ ^[0-9]+$ && "$end" =~ ^[0-9]+$ ]]; then
  [ $(( (end - start) / 1000000 )) -ge 15 ]
fi

for expression in 'epoch(1)' 'sleep()' 'sleep(1, 2)' 'sleep(-1)' 'sleep(0.5)' 'sleep("1")' 'sleep(null)' 'sleep(9223372036854775808)'; do
  if "$BIN" -e "$expression" >/dev/null 2>&1; then
    echo "accepted invalid time expression: $expression" >&2
    exit 1
  fi
done

prepared=$("$BIN" -e '
calls := 0
values := [0, 0]
fn(next()) { old := calls; calls += 1; return old }
fn(block_once()) { return sleep(values[next()]) }
i := 0
while(i < 1) { result := block_once(); i += 1 }
print(calls)
')
[ "$prepared" = 1 ]

echo 'PASS v4.6 time runtime'
