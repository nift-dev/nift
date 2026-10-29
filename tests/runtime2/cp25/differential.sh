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
run --cases "$D/const/cases.txt" --values "$D/const/values.txt" --contexts "$D/const/refused-contexts.txt" --expect-refusal
# A const that is only read (here: passed by value) runs on the VM in every context, loops included.
run --cases "$D/const/read-cases.txt" --contexts "$D/const/contexts.txt"
run --cases "$D/const/read-cases.txt" --contexts "$D/const/refused-contexts.txt"
run --cases "$D/const/refused-cases.txt" --expect-refusal
# Legacy computes a compound assignment's or increment's operation (and its
# errors) before rejecting the const write. Operand kinds the compiler refuses
# for any binding (type:*) stay refused here too.
run --cases "$D/const/operation-cases.txt" --values "$D/const/values.txt" --contexts "$D/const/contexts.txt" \
    --or-refused type:
