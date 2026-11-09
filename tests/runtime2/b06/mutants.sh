#!/usr/bin/env bash
# B06 mutation controls (combined-b3-b5.md section 37): each mutant (tests/runtime2/b06/
# build_mutant.sh) restores one refusal and must fail tests/runtime2/b06/run.py (exit 1, a fixture
# failure; exit 2 is a harness failure, never a detection).
#   1 infer_collection.cpp  family 1: `a := []; return a.pop()` refused (type:script-return) again
#   2 infer_callable.cpp    family 2: `a.sort(mk())` refused (type:<) again
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$ROOT"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
fail=0
# negative control (C46): the unmutated build passes the fixtures, so a failure is the mutant's
make -s .build/b4b-guardoff/nift-guardoff >/dev/null
python3 tests/runtime2/b06/run.py --nift "$ROOT/nift" --probe "$ROOT/.build/b4b-guardoff/nift-guardoff" >"$TMP/base.log" 2>&1 \
    || { tail -5 "$TMP/base.log"; echo "FAIL b06 mutants: the unmutated build fails the fixtures (a mutant cannot be judged)"; exit 1; }
echo "PASS b06 mutants negative control: the unmutated build passes every fixture"
for n in 1 2; do
    bash tests/runtime2/b06/build_mutant.sh "$n" "$TMP/nift-m$n"
    rc=0
    python3 tests/runtime2/b06/run.py --nift "$TMP/nift-m$n" --probe "$TMP/nift-m$n" >"$TMP/test$n.log" 2>&1 || rc=$?
    case $rc in
        1) echo "PASS b06 mutant $n: caught, $(grep -c '^FAIL b06 [a-z]' "$TMP/test$n.log") failing fixture(s), first: $(grep -m1 '^FAIL b06 [a-z]' "$TMP/test$n.log" | cut -c1-110)" ;;
        0) echo "FAIL b06 mutant $n: the fixtures passed (not caught)"; fail=1 ;;
        *) echo "FAIL b06 mutant $n: run.py exited $rc (harness failure)"; tail -5 "$TMP/test$n.log"; exit 1 ;;
    esac
    rm -f "$TMP/nift-m$n"
done
[ $fail = 0 ] && echo "PASS b06 mutants: all caught"
exit $fail
