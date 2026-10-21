#!/usr/bin/env bash
# R1 negative control: a build without the module-host-op refusal
# (-DNIFT_R1_MUTANT) must fail tests/runtime2/module_host_order.py. A build
# failure is a harness failure, never a detection.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
git -C "$ROOT" archive HEAD | tar -x -C "$TMP"
if ! make -C "$TMP" -j"${JOBS:-4}" nift \
    CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread -DNIFT_R1_MUTANT" >"$TMP/build.log" 2>&1; then
    echo "FAIL R1 mutant: build failed (harness failure)" >&2
    tail -20 "$TMP/build.log" >&2
    exit 1
fi
if python3 "$ROOT/tests/runtime2/module_host_order.py" --nift "$TMP/nift" >"$TMP/test.log" 2>&1; then
    echo "FAIL R1 mutant: module_host_order passed without the refusal (not caught)" >&2
    cat "$TMP/test.log" >&2
    exit 1
fi
grep -c '^FAIL' "$TMP/test.log" | sed 's/^/R1 mutant: failing cases: /'
echo "PASS R1 mutant: module_host_order rejected the build without the refusal"
