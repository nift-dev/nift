#!/usr/bin/env bash
# Memory investigation C2, end to end (permanent regression). Each round of
# c2_atoms2.f / c2_atoms3.f builds an object with a new generated key, which
# interns a new atom; before the fix the atom table grew outside the VM heap
# budget (memcheck2: 3,000,000 rounds under a 16 MiB budget, exit 0 at
# 511 MB RSS). c2_control2.f reuses 1,000 keys.
#   * 1,000 rounds of each script and 3,000,000 rounds of the control run
#     under a 16 MiB budget;
#   * 1,000,000 rounds of each atom script are refused under the same budget
#     ("memory budget exceeded", exit 70), with RSS far below the old 165 MB.
# Light (seconds, < 100 MB); also part of the worker job.
set -uo pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
DIR="$(cd "$(dirname "$0")" && pwd)/memory"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
run() {  # script rounds -> sets rc, out, err
  sed "s/ROUNDS/$2/" "$DIR/$1.f" >"$T/p.f"
  ( cd "$T" && ulimit -v 4000000 && ulimit -f 100000 &&
    NIFT_VM=require NIFT_VM_HEAP_BUDGET=16777216 exec timeout 300 "$NIFT" p.f >"$T/out" 2>"$T/err" )
  rc=$?
}
for s in c2_atoms2 c2_atoms3; do
  run $s 1000
  if [ "$rc" = 0 ] && [ "$(cat "$T/out")" = 1000 ]; then echo "PASS $s 1000 rounds run"; else echo "FAIL $s 1000: exit $rc $(head -c 300 "$T/err")"; fail=1; fi
  run $s 1000000
  if [ "$rc" = 70 ] && grep -q "memory budget exceeded" "$T/err" && [ ! -s "$T/out" ]; then
    echo "PASS $s 1000000 rounds refused (memory budget exceeded)"
  else echo "FAIL $s 1000000: exit $rc out=$(head -c 50 "$T/out") err=$(head -c 300 "$T/err")"; fail=1; fi
done
run c2_control2 3000000
if [ "$rc" = 0 ] && [ "$(cat "$T/out")" = 3000000 ]; then echo "PASS c2_control2 3000000 rounds run (1,000 keys reused)"; else echo "FAIL c2_control2: exit $rc $(head -c 300 "$T/err")"; fail=1; fi
exit $fail
