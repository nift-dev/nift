#!/bin/bash
# C140b/C140c/C141: the async-memory gate (D-082, D-085). Run with the harness toolchain environment (make's
# HARNESS_TOOLCHAIN_ENV). Exit 0: every case passes and every mutant is caught; otherwise a failure (1). The mutants
# run either way. (The C140b exit 3 for the known defect R1 is gone: C141 repaired R1 and R2, D-084.)
#   async_memory_gate.sh NIFT [PYTHON]
nift=$1 python=${2:-python3}
rc=0
"$python" -B tests/runtime2/async_memory.py --nift "$nift" || rc=1
"$python" -B tests/runtime2/async_memory.py --mutants || rc=1
exit "$rc"
