# Sourced by smoke scripts (owner 2026-11-10 13:26, item 5; combined-b3-b5.md section 81).
#   approved_refusal BIN NAME REASON DECISION PROGRAM-FILE [LEGACY-EXPECTED-STDOUT]
# An already-approved VM-default refusal: under the VM default (NIFT_VM unset) the program must be
# refused with exactly REASON before any part of it runs (exit 1, no stdout, the refusal's own
# "no part of it ran" note, and a statistics row with no VM or legacy execution). When
# LEGACY-EXPECTED-STDOUT is given, the legacy channel (NIFT_VM=0) must still print exactly it, so
# the case keeps checking legacy. Prints a PASS or FAIL line naming the check; returns 0 or 1.
approved_refusal() {
  local bin=$1 name=$2 reason=$3 decision=$4 file=$5 dir rc=0 out err stats
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
    echo "FAIL $name: not the approved refusal ($reason, $decision) before any execution: exit $rc, stdout [$out], stderr [$err], stats [$row]" >&2
    return 1
  fi
  if [ $# -ge 6 ]; then
    local lout lrc=0
    lout=$(NIFT_VM=0 "$bin" "$file" 2>&1) || lrc=$?
    if [ "$lrc" != 0 ] || [ "$lout" != "$6" ]; then
      echo "FAIL $name: legacy channel (NIFT_VM=0): exit $lrc, got [$lout]" >&2
      return 1
    fi
  fi
  echo "PASS $name: refused before execution ($reason, $decision)"
}
