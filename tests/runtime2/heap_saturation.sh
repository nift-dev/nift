#!/usr/bin/env bash
# The VM heap budget sees the real size of large values (memory investigation
# concern 3; L7 step 2.1 accounting). The heap header holds 1 GiB of granules;
# larger objects are counted exactly in a side table. Under ulimit -v and
# NIFT_VM=require:
#   1. a 512 MiB string within a 700 MiB budget runs;
#   2. a 1 GiB string under a 700 MiB budget is refused (memory budget exceeded);
#   3. a 1 GiB string within a 1088 MiB budget runs (counted exactly, ~1 GiB);
#   4. growing it past 1 GiB (capacity 2 GiB) under the same budget is refused;
#      a count saturating at the 1 GiB field (1 GiB - 64) would let it through.
# Touches up to ~2.5 GiB: run on a worker (REMOTE JOB), not in `make test`.
set -uo pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
script() {  # doublings, extra append (0/1)
  printf 's := "abcdefgh"\ni := 0\nwhile (i < %d) { s += s; i += 1 }\n' "$1" >"$T/p.f"
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

script 26 0; rc=$(run 734003200)
if [ "$rc" = 0 ] && [ "$(cat "$T/out")" = 536870912 ]; then ok "512 MiB string within a 700 MiB budget"; else bad "512 MiB control" "$rc"; fi
script 27 0; rc=$(run 734003200)
if refused "$rc"; then ok "1 GiB string refused by a 700 MiB budget"; else bad "1 GiB under 700 MiB" "$rc"; fi
script 27 0; rc=$(run 1140850688)
if [ "$rc" = 0 ] && [ "$(cat "$T/out")" = 1073741824 ]; then ok "1 GiB string within a 1088 MiB budget"; else bad "1 GiB control" "$rc"; fi
script 27 1; rc=$(run 1140850688)
if refused "$rc"; then ok "growth past 1 GiB refused by a 1088 MiB budget (counted at its real size)"; else bad "past 1 GiB" "$rc"; fi
exit $fail
