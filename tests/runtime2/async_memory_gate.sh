#!/bin/bash
# C140b/C140c: the async-memory gate (D-082). Run with the harness toolchain environment (make's
# HARNESS_TOOLCHAIN_ENV). Exit 0: every case passes; 3: every case passes except the known defect R1; otherwise a
# failure (1). The mutants run either way, and their failure is a failure (1).
#   async_memory_gate.sh NIFT [PYTHON]
nift=$1 python=${2:-python3}
rc=0
"$python" -B tests/runtime2/async_memory.py --nift "$nift" || rc=$?
"$python" -B tests/runtime2/async_memory.py --mutants || exit 1
if [ "$rc" -eq 3 ]; then echo "test-runtime2-async-memory: every case passes except KNOWN DEFECT R1 (P1, D-082): exit 3"; fi
exit "$rc"
