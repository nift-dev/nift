#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C.UTF-8 LANG=C.UTF-8 PYTHONDONTWRITEBYTECODE=1
out="$PWD/.build/freebsd-ci"
mkdir -p "$out"
gmake -j2 nift > "$out/build.log" 2>&1
for attempt in 1 2 3; do
    python3 -u tests/runtime2/vm_host_memory_previous_probe.py --nift "$PWD/nift" \
        2>&1 | tee "$out/previous-$attempt.log" || true
done
result=0
for attempt in 1 2 3 4 5 6; do
    python3 -u tests/runtime2/vm_host_memory.py --nift "$PWD/nift" \
        2>&1 | tee "$out/streamed-$attempt.log" || result=$?
done
printf '%s\n' "$result" > "$out/guest-status.txt"
