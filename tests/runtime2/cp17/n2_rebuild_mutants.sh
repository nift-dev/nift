#!/usr/bin/env bash
# CP17 N2 rebuild-decision mutation gate (CP17 rework, F4).
#
# The certification edge test-rt-host-mutants -> test-runtime2-cp17-n2-rebuild-mutant
# previously built N2_REBUILD_MUTANT=0 (no mutation). Here, for each N2 control
# (15: cp source observation removed; 16: mv source absence removed,
# src/rt/natives/host_fs_observe.h):
#   provenance  the mutant object is compiled with -DNIFT_HOST_MUTANT=N in this
#               run (the make log shows the command) and differs from the
#               control object compiled with -DNIFT_HOST_MUTANT=0;
#   witness     the mutant binary, run with production expectations (no
#               NIFT_N2_MUTANT), fails, and fails at the named decision check:
#               the cp change must trigger a rebuild (n2_rebuild_decision.cpp,
#               first `CHECK(!reasons.empty())`) for 15, the mv one (second)
#               for 16, and not the other;
#   controls    the control binary (NIFT_HOST_MUTANT=0) and the production
#               unit pass.
# Usage: n2_rebuild_mutants.sh <make>   (run from the repository root)
set -u -o pipefail
MAKE_CMD=${1:-make}
TEST=tests/rt/n2_rebuild_decision.cpp
LOG=$(mktemp -d)
trap 'rm -rf "$LOG"' EXIT
fail() { echo "FAIL n2 rebuild mutants: $*" >&2; exit 1; }

mapfile -t LINES < <(grep -n 'CHECK(!reasons.empty())' "$TEST" | cut -d: -f1)
[ "${#LINES[@]}" -eq 2 ] || fail "expected two decision checks in $TEST, found ${#LINES[@]}"
CP_LINE=${LINES[0]}
MV_LINE=${LINES[1]}

build() {  # build <mutant>
    local m=$1
    rm -f ".build/rt/n2-rebuild-mutants/$m/host_fs_write.o" ".build/rt/n2-rebuild-decision-m$m"
    if ! $MAKE_CMD N2_REBUILD_MUTANT="$m" ".build/rt/n2-rebuild-decision-m$m" >"$LOG/build-$m.log" 2>&1; then
        tail -20 "$LOG/build-$m.log" >&2
        fail "mutant $m: build failed (harness failure)"
    fi
    grep -q -- "-DNIFT_HOST_MUTANT=$m -c src/rt/natives/host_fs_write.cpp" "$LOG/build-$m.log" ||
        fail "mutant $m: the make log does not show its object compiled with -DNIFT_HOST_MUTANT=$m"
}

build 0
if ! ./.build/rt/n2-rebuild-decision-m0 >"$LOG/run-0.log" 2>&1; then
    cat "$LOG/run-0.log" >&2
    fail "control (NIFT_HOST_MUTANT=0) failed"
fi
echo "PASS n2 rebuild control: NIFT_HOST_MUTANT=0 passes the production witness"

for m in 15 16; do
    build "$m"
    cmp -s ".build/rt/n2-rebuild-mutants/$m/host_fs_write.o" ".build/rt/n2-rebuild-mutants/0/host_fs_write.o" &&
        fail "mutant $m: its object is identical to the control object (no compiled mutation)"
    if ./.build/rt/n2-rebuild-decision-m"$m" >"$LOG/run-$m.log" 2>&1; then
        cat "$LOG/run-$m.log" >&2
        fail "mutant $m: survived the production rebuild-decision witness"
    fi
    if [ "$m" = 15 ]; then want=$CP_LINE; other=$MV_LINE; else want=$MV_LINE; other=$CP_LINE; fi
    grep -q "n2_rebuild_decision.cpp:$want: CHECK failed: !reasons.empty()" "$LOG/run-$m.log" ||
        { cat "$LOG/run-$m.log" >&2; fail "mutant $m: not killed by its named witness (line $want)"; }
    grep -q "n2_rebuild_decision.cpp:$other: CHECK failed" "$LOG/run-$m.log" &&
        { cat "$LOG/run-$m.log" >&2; fail "mutant $m: also failed the other operation's check (line $other)"; }
    echo "PASS n2 rebuild mutant $m: killed by n2_rebuild_decision.cpp:$want (no rebuild after the source changed)"
done
rm -f .build/rt/n2-rebuild-decision-m0 .build/rt/n2-rebuild-decision-m15 .build/rt/n2-rebuild-decision-m16
echo "PASS n2 rebuild mutants: 2 of 2 killed by named witnesses; control passes"
