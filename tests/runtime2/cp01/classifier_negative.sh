#!/usr/bin/env bash
# B3 control: a binary that misses every first rebuild but repairs on a
# follow-up must fail the classifiers because misses must be zero.
set -u -o pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NIFT="${NIFT_BIN:?set NIFT_BIN}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
for c in classify atomic_classify; do
  if [ "$c" = classify ]; then
    bash "$HERE/classify.sh" "$HERE/fake_eventual.py" 3 3 fake >"$T/$c.log" 2>&1
  else
    bash "$HERE/atomic_classify.sh" "$HERE/fake_eventual.py" 3 fake >"$T/$c.log" 2>&1
  fi
  rc=$?
  if [ $rc -eq 0 ]; then echo "FAIL classifier control: $c accepted a missed initial rebuild"; cat "$T/$c.log"; fail=1
  else echo "PASS classifier control: $c rejects missed initial rebuilds"; fi
done
exit $fail
