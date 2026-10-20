#!/usr/bin/env bash
# Runtime 2.0: fixture completeness for the golden corpora vm_corpus.sh runs
# (tests/runtime2/vm-corpus/*/ and tests/runtime2/semantics/). Every *.f must
# have its .stdout, .stderr and .exit golden. Existence only; this script
# never runs nift and never writes a golden (generating one could turn a
# behaviour change into a new expectation). Runs in well under a second, so
# it gates every target that runs the corpus and `make test`, before
# certification (cp12b: a .f committed without goldens reached Linode).
#
#   vm_corpus_complete.sh [ROOT]   check ROOT (default: this directory)
#   vm_corpus_complete.sh --selftest   negative control: a copy with one
#                                      golden removed must fail, naming it
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"

check() {
  local root=$1 missing=0 n=0 f b k
  for f in "$root"/vm-corpus/*/*.f "$root"/semantics/*.f; do
    [ -e "$f" ] || continue
    n=$((n+1)); b="${f%.f}"
    for k in stdout stderr exit; do
      if [ ! -f "$b.$k" ]; then
        echo "MISSING golden: ${b#"$root"/}.$k (for ${f#"$root"/})"; missing=$((missing+1))
      fi
    done
  done
  if [ "$n" -eq 0 ]; then echo "FAIL vm corpus completeness: no .f files under $root"; return 1; fi
  if [ "$missing" -ne 0 ]; then
    echo "FAIL vm corpus completeness: $missing golden file(s) missing for $n scripts (record them from legacy, NIFT_VM=0; never from the VM)"
    return 1
  fi
  echo "PASS vm corpus completeness: $n scripts, every .stdout/.stderr/.exit present"
}

if [ "${1:-}" = "--selftest" ]; then
  W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
  cp -r "$HERE/vm-corpus" "$HERE/semantics" "$W/"
  check "$W" >/dev/null || { echo "FAIL selftest: the unmodified copy does not pass"; exit 1; }
  victim="$(ls "$W"/vm-corpus/*/*.stderr | head -1)"
  rm "$victim"
  out="$(check "$W")"; rc=$?
  if [ "$rc" -ne 0 ] && grep -qF "MISSING golden: ${victim#"$W"/}" <<<"$out"; then
    echo "PASS vm corpus completeness selftest: removing ${victim#"$W"/} fails and names it"
    exit 0
  fi
  echo "FAIL selftest: removing ${victim#"$W"/} was not detected (rc=$rc): $out"; exit 1
fi
check "${1:-$HERE}"
