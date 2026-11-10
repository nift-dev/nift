# Sourced by smoke scripts (owner 2026-11-10 13:26, item 5; review of C83-C99, P1-1 and P2-2;
# combined-b3-b5.md sections 85 and 89).
#   approved_refusal BIN NAME REASON DECISION PROGRAM-FILE LEGACY-STDOUT [LEGACY-EXIT [LEGACY-STDERR]]
# REASON is the case's pinned refusal. It is accepted only when its CP25-R inventory status
# (classification.tsv, read by approved_refusal.py; never hard-coded) is approved or deferred-named.
# Under the VM default (NIFT_VM unset) the program must be refused with exactly REASON before any
# part of it runs: exit 1, no stdout, the refusal's own "no part of it ran" note, and a statistics
# row with no VM or legacy execution. The legacy oracle is mandatory: the legacy channel
# (NIFT_VM=0) must print exactly LEGACY-STDOUT on stdout and exit LEGACY-EXIT (default 0); its stderr
# must contain LEGACY-STDERR, or be empty when none is given (review of C100-C102, L-1: an error
# oracle can be pinned). Prints a PASS or FAIL line naming the check; returns 0 or 1.
# Self-test: tests/runtime2/approved_refusal_selftest.sh.
APPROVED_REFUSAL_PY=${APPROVED_REFUSAL_PY:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/approved_refusal.py}
approved_refusal() {
  if [ $# -lt 6 ] || [ $# -gt 8 ]; then
    echo "FAIL ${2:-approved_refusal}: no legacy oracle (approved_refusal needs 6 to 8 arguments, got $#)" >&2
    return 1
  fi
  local want_exit=${7:-0} want_err=${8:-}
  local bin=$1 name=$2 reason=$3 decision=$4 file=$5 dir rc=0 out err stats status
  status=$(python3 -B "$APPROVED_REFUSAL_PY" status "$reason")
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
  local lout lrc=0 lerr ldir
  ldir=$(mktemp -d)
  lout=$(NIFT_VM=0 "$bin" "$file" 2>"$ldir/err") || lrc=$?
  lerr=$(cat "$ldir/err"); rm -rf "$ldir"
  if [ "$lrc" != "$want_exit" ] || [ "$lout" != "$6" ] \
     || { [ -z "$want_err" ] && [ -n "$lerr" ]; } || { [ -n "$want_err" ] && ! grep -qF -- "$want_err" <<<"$lerr"; }; then
    echo "FAIL $name: legacy channel (NIFT_VM=0): exit $lrc (want $want_exit), stdout [$lout], stderr [$lerr]" >&2
    return 1
  fi
  echo "PASS $name: refused before execution ($reason, $status, $decision); legacy channel exact"
}
