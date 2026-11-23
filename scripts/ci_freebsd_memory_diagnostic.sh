#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C.UTF-8 LANG=C.UTF-8 PYTHONDONTWRITEBYTECODE=1
out="$PWD/.build/freebsd-ci"
mkdir -p "$out"
gmake -j2 nift > "$out/build.log" 2>&1
result=0
(
    export NIFT_VM=require NIFT_READ_LIMIT=65536
    dd if=/dev/zero bs=65536 count=1 2>/dev/null | "$PWD/nift" -e 'print(open("/dev/stdin").length())'
) > "$out/read-limit-exact.log" 2>&1 || result=1
grep -qx '65536' "$out/read-limit-exact.log" || result=1
if (
    export NIFT_VM=require NIFT_READ_LIMIT=65536
    { dd if=/dev/zero bs=65536 count=1 2>/dev/null && printf x; } |
        "$PWD/nift" -e 'print(open("/dev/stdin").length())'
) > "$out/read-limit-over.log" 2>&1; then
    result=1
fi
grep -q 'exceeds the read limit' "$out/read-limit-over.log" || result=1
for attempt in 1 2 3 4; do
    python3 -u tests/runtime2/vm_host_memory_previous_probe.py --nift "$PWD/nift" \
        2>&1 | tee "$out/previous-$attempt.log" || result=$?
done
for attempt in 1 2 3 4; do
    python3 -u tests/runtime2/vm_host_memory.py --nift "$PWD/nift" \
        2>&1 | tee "$out/streamed-$attempt.log" || result=$?
done
printf '%s\n' "$result" > "$out/guest-status.txt"
