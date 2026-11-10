# Sourced by smoke scripts (owner 2026-11-10 13:26, item 5; review of C83-C99, P1-1 and P2-2;
# combined-b3-b5.md sections 85 and 89).
#   approved_refusal BIN NAME REASON DECISION PROGRAM-FILE LEGACY-EXPECTED-STDOUT
# REASON is the case's pinned refusal. It is accepted only when its CP25-R inventory status
# (classification.tsv, read by approved_refusal.py; never hard-coded) is approved or deferred-named.
# Under the VM default (NIFT_VM unset) the program must be refused with exactly REASON before any
# part of it runs: exit 1, no stdout, the refusal's own "no part of it ran" note, and a statistics
# row with no VM or legacy execution. The legacy oracle is mandatory: the legacy channel
# (NIFT_VM=0) must exit 0 and print exactly LEGACY-EXPECTED-STDOUT. Prints a PASS or FAIL line
# naming the check; returns 0 or 1. Self-test: tests/runtime2/approved_refusal_selftest.sh.
approved_refusal() {
  if [ $# -ne 6 ]; then
    echo "FAIL ${2:-approved_refusal}: no legacy oracle (approved_refusal needs 6 arguments, got $#)" >&2
    return 1
  fi
  local bin=$1 name=$2 reason=$3 decision=$4 file=$5 dir rc=0 out err stats status
  status=$(python3 -B "${APPROVED_REFUSAL_PY:-$(dirname "${BASH_SOURCE[0]}")/approved_refusal.py}" status "$reason")
  if [ "$status" != approved ] && [ "$status" != deferred-named ]; then
    echo "FAIL $name: refused with $reason, whose inventory status is $status (only approved or deferred-named may be accepted)" >&2
    return 1
  fi
  dir=$(mktemp -d)
  stats="$dir/stats.jsonl"
  out=$(env -u NIFT_VM NIFT_VM_STATS="$stats" "$bin" "$file" 2>"$dir/err") || rc=$?
  err=$(cat "$dir/err")
  local row; row=$(tail -n 1 "$stats" 2>/dev/null || true)
  rm -rf "$dir"
  if [ "$rc" != 1 ] || [ -n "$out" ] \
     || ! grep -qF "not compiled for the VM ($reason)" <<<"$err" \
     || ! grep -qF "no part of it ran" <<<"$err" \
     || ! grep -qF "\"reason\":\"$reason\"" <<<"$row" \
     || ! grep -qF '"vm_executions":0' <<<"$row" || ! grep -qF '"legacy_executions":0' <<<"$row"; then
    echo "FAIL $name: not the refusal $reason ($status, $decision) before any execution: exit $rc, stdout [$out], stderr [$err], stats [$row]" >&2
    return 1
  fi
  local lout lrc=0
  lout=$(NIFT_VM=0 "$bin" "$file" 2>&1) || lrc=$?
  if [ "$lrc" != 0 ] || [ "$lout" != "$6" ]; then
    echo "FAIL $name: legacy channel (NIFT_VM=0): exit $lrc, got [$lout]" >&2
    return 1
  fi
  echo "PASS $name: refused before execution ($reason, $status, $decision); legacy channel exact"
}
