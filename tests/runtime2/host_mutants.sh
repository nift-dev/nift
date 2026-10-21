#!/usr/bin/env bash
# CP16 host-builtin negative controls: each mutant must fail the VM host suite
# (or, for the dependency-recording mutant, the native observer unit). A build
# failure is a harness failure, never a successful detection.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
ORACLE=${1:-"$ROOT/nift"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

for mutant in 1 2 3 5; do
    BUILD="$TMP/m$mutant"
    mkdir -p "$BUILD"
    git -C "$ROOT" archive HEAD | tar -x -C "$BUILD"
    if ! make -C "$BUILD" -j"${JOBS:-4}" nift \
        CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread -DNIFT_HOST_MUTANT=$mutant" \
        >"$TMP/build-$mutant.log" 2>&1; then
        echo "FAIL host mutant $mutant: build failed (harness failure)" >&2
        tail -20 "$TMP/build-$mutant.log" >&2
        exit 1
    fi
    if python3 "$BUILD/tests/runtime2/vm_host.py" --nift "$BUILD/nift" >"$TMP/test-$mutant.log" 2>&1; then
        echo "FAIL host mutant $mutant: vm_host tests passed (mutant not caught)" >&2
        cat "$TMP/test-$mutant.log" >&2
        exit 1
    fi
    echo "PASS host mutant $mutant: vm_host tests rejected the mutant"
done

BUILD="$TMP/m4"
mkdir -p "$BUILD"
git -C "$ROOT" archive HEAD | tar -x -C "$BUILD"
if ! make -C "$BUILD" -j"${JOBS:-4}" .build/rt/host-native-unit \
    CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread -DNIFT_HOST_MUTANT=4" \
    >"$TMP/build-4.log" 2>&1; then
    echo "FAIL host mutant 4: build failed (harness failure)" >&2
    tail -20 "$TMP/build-4.log" >&2
    exit 1
fi
if "$BUILD/.build/rt/host-native-unit" >"$TMP/test-4.log" 2>&1; then
    echo "FAIL host mutant 4: observer test passed (mutant not caught)" >&2
    cat "$TMP/test-4.log" >&2
    exit 1
fi
echo "PASS host mutant 4: observer test rejected the mutant"

BUILD="$TMP/m6"
mkdir -p "$BUILD"
git -C "$ROOT" archive HEAD | tar -x -C "$BUILD"
if ! make -C "$BUILD" -j"${JOBS:-4}" nift \
    CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread -DNIFT_HOST_MUTANT=6" \
    >"$TMP/build-6.log" 2>&1; then
    echo "FAIL host mutant 6: build failed (harness failure)" >&2
    tail -20 "$TMP/build-6.log" >&2
    exit 1
fi
if python3 "$BUILD/tests/runtime2/vm_host_memory.py" --nift "$BUILD/nift" >"$TMP/test-6.log" 2>&1; then
    echo "FAIL host mutant 6: memory test passed (mutant not caught)" >&2
    cat "$TMP/test-6.log" >&2
    exit 1
fi
echo "PASS host mutant 6: late open budget check rejected"
echo "PASS host negative controls: 6/6 mutants rejected"
