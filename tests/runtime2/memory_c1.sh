#!/usr/bin/env bash
# Memory investigation C1, end to end (permanent regression; worker run
# memcheck2 before the fix: SIGABRT in 18 of 24 cells). c1_push.f grows an
# array of heap strings until either the VM heap budget or the address-space
# limit stops it; the VM then unwinds and releases the whole array, which
# used to allocate a worklist and abort ("terminate called after throwing an
# instance of 'std::bad_alloc'") when memory was exhausted.
#
# Every cell (12 address-space limits x {default budget, budget beyond the
# limit}) must end with exit 70, no signal, and the original reason intact:
# "memory budget exceeded" (default budget, the larger limits) or
# "out of memory (std::bad_alloc)" (when the address space runs out first).
# Touches up to 2 GiB: run on a worker (REMOTE JOB), not in `make test`.
set -uo pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
SCRIPT="$(cd "$(dirname "$0")" && pwd)/memory/c1_push.f"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cp "$SCRIPT" "$T/p.f"
fail=0 cells=0
for budget in default 100000000000; do
  for v in 400000 500000 600000 700000 800000 900000 1000000 1100000 1200000 1400000 1600000 2097152; do
    cells=$((cells + 1))
    ( cd "$T" && ulimit -v "$v" && ulimit -f 100000 &&
      if [ "$budget" = default ]; then unset NIFT_VM_HEAP_BUDGET; else export NIFT_VM_HEAP_BUDGET="$budget"; fi &&
      NIFT_VM=require exec timeout 300 "$NIFT" p.f >"$T/out" 2>"$T/err" )
    rc=$?
    reason=$(grep -o "memory budget exceeded\|out of memory (std::bad_alloc)" "$T/err" | head -1)
    if [ "$rc" = 70 ] && [ -n "$reason" ] && ! grep -q "terminate called" "$T/err"; then
      echo "PASS ulimit -v $v budget $budget: exit 70, $reason"
    else
      echo "FAIL ulimit -v $v budget $budget: exit $rc err=$(tail -c 300 "$T/err" | tr '\n' ' ')"; fail=1
    fi
  done
done
echo "$cells cells"
exit $fail
