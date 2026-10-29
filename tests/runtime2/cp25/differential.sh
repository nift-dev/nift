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
