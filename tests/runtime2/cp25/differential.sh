#!/usr/bin/env bash
# CP25-R legacy/VM differentials (G3): every corrected refusal family runs on
# the VM with byte-identical results to legacy; contexts where legacy's report
# differs stay refused and are asserted as refusals.
set -euo pipefail
NIFT=${1:?usage: differential.sh NIFT}
D=$(cd "$(dirname "$0")" && pwd)
run() { python3 "$D/legacy_vm_diff.py" --nift "$NIFT" "$@"; }
run --cases "$D/print/cases.txt" --values "$D/print/values.txt" --contexts "$D/print/contexts.txt"
run --cases "$D/arity/cases.txt" --contexts "$D/arity/contexts.txt"
run --cases "$D/arity/cases.txt" --contexts "$D/arity/refused-contexts.txt" --expect-refusal
run --cases "$D/arity/refused-cases.txt" --expect-refusal
run --cases "$D/const/cases.txt" --values "$D/const/values.txt" --contexts "$D/const/contexts.txt"
run --cases "$D/const/captured-read.txt" --values "$D/const/values.txt" --contexts "$D/const/top-context.txt"
run --cases "$D/const/captured-read.txt" --values "$D/const/values.txt" --contexts "$D/const/function-context.txt" --expect-refusal
# D-009 #2 / D-058: const is immutable in loop-evaluator code too, and an
# assignment to it fails before its value is evaluated. Expected behaviour:
# legacy's own result for the same layout outside a loop (the oracle); legacy
# in the loop must differ (approved difference, recorded).
run --cases "$D/const/loop-inside-cases.txt" --values "$D/const/values.txt" --contexts "$D/const/loop-contexts.txt" \
    --oracle-contexts "$D/const/loop-oracle.txt" --or-refused type:
run --cases "$D/const/loop-outside-assign.txt" --values "$D/const/values.txt" --contexts "$D/const/loop-contexts.txt" \
    --oracle-contexts "$D/const/loop-oracle.txt"
run --cases "$D/const/loop-outside-increment.txt" --values "$D/const/values.txt" \
    --contexts "$D/const/loop-direct-contexts.txt" --or-refused type:
run --cases "$D/const/loop-outside-increment.txt" --values "$D/const/values.txt" \
    --contexts "$D/const/loop-callee-contexts.txt" --oracle-contexts "$D/const/loop-callee-oracle.txt" --or-refused type:
run --cases "$D/const/loop-outside-decrement.txt" --contexts "$D/const/loop-contexts.txt"
# Compound assignment to a const in loop code stays refused: whether it rejects
# before its value at top level too (legacy: value and operation first) is
# open (D-058).
run --cases "$D/const/loop-compound.txt" --values "$D/const/values.txt" --contexts "$D/const/loop-contexts.txt" \
    --expect-refusal
# A const that is only read (here: passed by value) runs on the VM in every context, loops included.
run --cases "$D/const/read-cases.txt" --contexts "$D/const/contexts.txt"
run --cases "$D/const/read-cases.txt" --contexts "$D/const/loop-contexts.txt"
run --cases "$D/const/refused-cases.txt" --expect-refusal
# Legacy computes a compound assignment's or increment's operation (and its
# errors) before rejecting the const write. Operand kinds the compiler refuses
# for any binding (type:*) stay refused here too.
run --cases "$D/const/operation-cases.txt" --values "$D/const/values.txt" --contexts "$D/const/contexts.txt" \
    --or-refused type:
