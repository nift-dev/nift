#!/usr/bin/env bash
# CP01 simplification gates: v4.12 modified-mode semantics, immediate edits,
# settle, hash-mode repair, syscall guard and the crash/ordinary scenarios.
set -u -o pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
fail=0
HERE="$(cd "$(dirname "$0")" && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
SYSCALLS_ONLY=0
[ "${1:-}" = "--syscalls-only" ] && SYSCALLS_ONLY=1
if [ "$SYSCALLS_ONLY" -eq 0 ]; then
run_optional77() { "$@" >/dev/null 2>&1; local rc=$?; if [ "$rc" -eq 77 ]; then echo "SKIP $*"; elif [ "$rc" -ne 0 ]; then "$@"; fail=1; fi; }
run_optional77 bash "$HERE/cp01/syscall_harness_negative.sh"
run_optional77 bash "$HERE/cp01/classifier_negative.sh"
run_optional77 bash "$HERE/cp01/crash_harness_negative.sh"
run_optional77 bash "$HERE/cp01/failed_status_controls.sh"
run_optional77 bash "$HERE/cp01/missing_tracer_caller.sh"
run_optional77 bash "$HERE/cp01/syscall_depth_control.sh"

ONLY=basic,interrupt,crash CYCLES=0 python3 "$HERE/cp01/ordinary.py" ours="$NIFT" || fail=1
bash "$HERE/cp01/classify.sh" "$NIFT" 1000 3 ours || fail=1
bash "$HERE/cp01/atomic_classify.sh" "$NIFT" 1000 ours || fail=1
for mode in hash hybrid; do
  bash "$HERE/cp01/shared_template_midbuild.sh" "$NIFT" ours "$mode" 100 0.1 0.3 >"$T/shared-$mode.log" 2>&1 || fail=1
  grep -q 'next1:0' "$T/shared-$mode.log" || { echo "FAIL shared $mode repair: $(cat "$T/shared-$mode.log")"; fail=1; }
done
bash "$HERE/cp01/settle.sh" "$T" ours="$NIFT" >"$T/settle.log" 2>&1 || fail=1
grep -q 'after-edit-all:\[ 20 0 0 0 0 0 0 0 0 0 0 0 \] outputs-with-edit=20/20' "$T/settle.log" || { echo "FAIL settle: $(cat "$T/settle.log")"; fail=1; }
fi

unmeasured=0
bash "$HERE/cp01/syscalls.sh" 1000 0 ours="$NIFT" >"$T/sys1000.log" 2>&1; rc=$?
if [ "$rc" -eq 77 ]; then unmeasured=1; else [ "$rc" -eq 0 ] || fail=1; fi
bash "$HERE/cp01/syscalls.sh" 200 10 ours="$NIFT" >"$T/sys200.log" 2>&1; rc=$?
if [ "$rc" -eq 77 ]; then unmeasured=1; else [ "$rc" -eq 0 ] || fail=1; fi
if [ "$unmeasured" -eq 1 ]; then
  if [ "$fail" -ne 0 ]; then exit 1; fi
  echo "SKIP CP01 syscall measurements: tracer unavailable; performance unmeasured"
  exit 77
fi
check_line() {
  local file=$1 phase=$2 limit=$3
  local line; line=$(grep " $phase total=" "$file" | head -1)
  [ -n "$line" ] || { echo "FAIL syscalls: missing $phase"; fail=1; return; }
  local total; total=$(sed -n 's/.* total=\([0-9]*\).*/\1/p' <<<"$line")
  [ "$total" -le "$limit" ] || { echo "FAIL syscalls $phase total=$total > $limit"; fail=1; }
  grep -q 'path.clock=0' <<<"$line" || { echo "FAIL syscalls $phase accessed .nift/clock"; fail=1; }
  local lock; lock=$(sed -n 's/.* path.lock=\([0-9]*\).*/\1/p' <<<"$line")
  [ "$lock" -le 1 ] || { echo "FAIL syscalls $phase .nift/.lock accesses=$lock > ownership-only 1"; fail=1; }
}
check_line "$T/sys1000.log" noop 19146
check_line "$T/sys1000.log" full 41215
check_line "$T/sys1000.log" edit 19243
check_line "$T/sys1000.log" tpl 71252
check_line "$T/sys200.log" noop 25944
grep -q 'files-under-.nift=1003' "$T/sys1000.log" || { echo "FAIL .nift file count"; fail=1; }
[ "$fail" -eq 0 ] && echo "PASS CP01 simplification"
exit "$fail"
