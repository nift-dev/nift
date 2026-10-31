#!/usr/bin/env bash
# Mutation controls for tests/runtime2/coro_module/coro_module.py.
#   mutants.sh NIFT MUTANT_DIR [N...]
# Each mutant is the normal build with the module-admission objects
# (src/rt/compiler_coro_module.cpp, src/rt/compiler_coro_admit.cpp) compiled
# -DNIFT_CORO_MODULE_MUTANT=n (Makefile rule $(CORO_MODULE_MUTANT_DIR)/nift-n).
# It must differ from the normal binary, and the suite must fail on it, with
# the mutant's named witness among the failures. A build failure is a harness
# failure, never a detection. Each mutant binary is removed after its run.
set -euo pipefail
NIFT=$1
DIR=$2
shift 2
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
MAKE=${MAKE:-make}
# mutant -> defect, witness (a FAIL label prefix that must appear)
declare -A WHAT=(
  [1]="init-task check off"                                  [2]="module-state reads/writes not seen"
  [3]="capture: the task's direct body only (no transitive calls)"
  [4]="init-task: module top level only (no transitive calls)" [5]="calls through a value not followed"
  [6]="a task's arguments ignored (function values passed in)" [7]="module handle state not seen"
  [8]="cooperative model admits linked units (no coro-module)" [9]="init-task also applied to the entry"
  [10]="arguments passed by value counted as task code"
)
declare -A WITNESS=(
  [1]="init-thread:" [2]="capture-thread-state:" [3]="capture-nested-transitive:" [4]="init-transitive:"
  [5]="capture-through-value-call:" [6]="capture-function-argument:" [7]="capture-module-handle:"
  [8]="cooperative model:" [9]="clean-module-worker" [10]="shared-globals-importer"
)
LIST=("$@")
[ ${#LIST[@]} -gt 0 ] || LIST=(1 2 3 4 5 6 7 8 9 10)
normal=$(sha256sum "$NIFT" | cut -d' ' -f1)
failed=0
for n in "${LIST[@]}"; do
  bin="$DIR/nift-$n"
  if ! $MAKE -C "$ROOT" "$bin" >"$DIR.build-$n.log" 2>&1; then
    echo "FAIL mutant $n: build failed (harness failure)" >&2; tail -5 "$DIR.build-$n.log" >&2; exit 1
  fi
  rm -f "$DIR.build-$n.log"
  if [ "$(sha256sum "$bin" | cut -d' ' -f1)" = "$normal" ]; then
    echo "FAIL mutant $n (${WHAT[$n]}): identical to the normal binary"; failed=1; rm -f "$bin"; continue
  fi
  log=$(mktemp)
  if python3 "$ROOT/tests/runtime2/coro_module/coro_module.py" --nift "$bin" >"$log" 2>&1; then
    echo "FAIL mutant $n (${WHAT[$n]}): the suite passed (not caught)"; failed=1
  elif ! grep -q "^FAIL ${WITNESS[$n]}" "$log"; then
    echo "FAIL mutant $n (${WHAT[$n]}): caught, but not by its witness ${WITNESS[$n]}"; grep '^FAIL' "$log" | head -5
    failed=1
  else
    echo "PASS mutant $n (${WHAT[$n]}): $(grep -c '^FAIL ' "$log") failing checks, witness ${WITNESS[$n]}"
  fi
  rm -f "$log" "$bin"
done
exit $failed
