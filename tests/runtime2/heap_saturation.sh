#!/usr/bin/env bash
# The VM heap budget sees the real size of large values (memory investigation
# concern 3; L7 step 2.1 accounting). The heap header holds 1 GiB of granules;
# larger objects are counted exactly in a side table.
#
# The budget counts a string's capacity, not its length. The string is built
# from a 15-character literal by `s += s`, which keeps capacity == length
# (15 * 2^k bytes) under libstdc++'s growth rule (new capacity = max(needed,
# 2 * old)); starting from 8 characters gives capacity 1.875 * length, which is
# why the first version's controls (512 MiB in 700 MiB, 1 GiB in 1088 MiB) did
# not fit (worker run l7-value-layer-2). The self-append's temporary copy is
# not counted (bounded: one copy of the operand, released before the next
# instruction). Under ulimit -v and NIFT_VM=require:
#   1. a 480 MiB string within a 512 MiB budget runs;
#   2. a 960 MiB string under a 512 MiB budget is refused (memory budget exceeded);
#   3. a 960 MiB string within a 1088 MiB budget runs (below the 1 GiB field);
#   4. appending one byte (capacity 1920 MiB) under the same budget is refused:
#      the side table counts 1920 MiB; a count saturating at the 1 GiB field
#      (1 GiB - 64) would be under 1088 MiB and let it through.
# Peak address space ~2.9 GiB (case 4: 960 MiB + 1920 MiB): run on a worker
# (REMOTE JOB), not in `make test`.
set -uo pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
script() {  # doublings, extra append (0/1)
  printf 's := "abcdefghijklmno"\ni := 0\nwhile (i < %d) { s += s; i += 1 }\n' "$1" >"$T/p.f"
  [ "$2" = 1 ] && printf 's += "x"\n' >>"$T/p.f"
  printf 'print(s.length())\n' >>"$T/p.f"
}
run() {  # budget
  ( cd "$T" && ulimit -v 3800000 && ulimit -f 100000 &&
    NIFT_VM=require NIFT_VM_HEAP_BUDGET="$1" timeout 900 "$NIFT" p.f >"$T/out" 2>"$T/err" ); echo $?
}
ok() { echo "PASS $1"; }
bad() { echo "FAIL $1: exit $2 out=$(head -c 100 "$T/out") err=$(head -c 300 "$T/err")"; fail=1; }
refused() { [ "$1" != 0 ] && grep -q "memory budget exceeded" "$T/err" && [ ! -s "$T/out" ]; }

script 25 0; rc=$(run 536870912)
if [ "$rc" = 0 ] && [ "$(cat "$T/out")" = 503316480 ]; then ok "480 MiB string within a 512 MiB budget"; else bad "480 MiB control" "$rc"; fi
script 26 0; rc=$(run 536870912)
if refused "$rc"; then ok "960 MiB string refused by a 512 MiB budget"; else bad "960 MiB under 512 MiB" "$rc"; fi
script 26 0; rc=$(run 1140850688)
if [ "$rc" = 0 ] && [ "$(cat "$T/out")" = 1006632960 ]; then ok "960 MiB string within a 1088 MiB budget"; else bad "960 MiB control" "$rc"; fi
script 26 1; rc=$(run 1140850688)
if refused "$rc"; then ok "growth to capacity 1920 MiB refused by a 1088 MiB budget (counted at its real size)"; else bad "past 1 GiB" "$rc"; fi
exit $fail
