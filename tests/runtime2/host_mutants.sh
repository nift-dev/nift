#!/usr/bin/env bash
# CP16 host-builtin negative controls: each mutant must fail the VM host suite
# (or, for the dependency-recording mutant, the native observer unit). A build
# failure is a harness failure, never a successful detection.
# B15 (combined-b3-b5.md section 75): each mutant is built by tests/runtime2/sanctioned_mutant.py
# (only the units the define changes, nift's own compile line, into .build/mutant-nift; every
# mutant object differs from production), linked as nift or as the unit test named, not by
# `make nift` with mutant CXXFLAGS in an archive of HEAD (16 full builds), which the production
# build guard refuses. Run through make (test-rt-host-mutants), after nift and the unit objects.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
ORACLE=${1:-"$ROOT/nift"}
TMP=$(mktemp -d)
MUTANTS="$ROOT/.build/mutant-nift/host"
trap 'rm -rf "$TMP" "$MUTANTS" "$ROOT"/.build/rt/n2-rebuild-mutants/1[56] "$ROOT"/.build/rt/n2-rebuild-decision-m1[56]' EXIT
# mutant N [sanctioned_mutant.py arguments]: build mutant N into $MUTANTS/mN (BUILD)
mutant() {
    local n=$1; shift
    BUILD="$MUTANTS/m$n"
    if ! python3 -B "$ROOT/tests/runtime2/sanctioned_mutant.py" --out "$BUILD" --define=-DNIFT_HOST_MUTANT=$n "$@" \
        >"$TMP/build-$n.log" 2>&1; then
        echo "FAIL host mutant $n: build failed (harness failure)" >&2
        tail -20 "$TMP/build-$n.log" >&2
        exit 1
    fi
    grep '^built mutant' "$TMP/build-$n.log"
}

for mutant in 1 2 3 5; do
    mutant $mutant
    if [ "$mutant" = "3" ]; then
        if ! python3 "$ROOT/tests/runtime2/host_m3_order.py" --nift "$BUILD/nift" --oracle "$ORACLE"             >"$TMP/test-$mutant.log" 2>&1; then
            echo "FAIL host mutant 3: deterministic M3 ordering witness failed" >&2
            cat "$TMP/test-$mutant.log" >&2
            exit 1
        fi
        echo "PASS host mutant 3: deterministic sorted-ls witness rejected the mutant"
        if NIFT_M3_TEST_SORTED=1 python3 "$ROOT/tests/runtime2/host_m3_order.py" \
            --nift "$BUILD/nift" --oracle "$ORACLE" >"$TMP/test-3-sorted.log" 2>&1; then
            echo "FAIL host mutant 3: sorted-order control was accepted as established" >&2
            cat "$TMP/test-3-sorted.log" >&2
            exit 1
        fi
        if ! grep -q "NOT ESTABLISHED" "$TMP/test-3-sorted.log"; then
            echo "FAIL host mutant 3: sorted-order control failed for an unrelated reason" >&2
            cat "$TMP/test-3-sorted.log" >&2
            exit 1
        fi
        echo "PASS host mutant 3: unestablished sorted-order witness is rejected"
        continue
    fi
    if env TMPDIR=/tmp python3 "$ROOT/tests/runtime2/vm_host.py" --nift "$BUILD/nift" --oracle "$ORACLE" >"$TMP/test-$mutant.log" 2>&1; then
        echo "FAIL host mutant $mutant: vm_host tests passed (mutant not caught)" >&2
        cat "$TMP/test-$mutant.log" >&2
        exit 1
    fi
    echo "PASS host mutant $mutant: vm_host tests rejected the mutant"
done

mutant 4 --objects-env NIFT_TC_RT_UNIT_OBJECTS --unit tests/rt/host_observer_native.cpp
if "$BUILD/unit" >"$TMP/test-4.log" 2>&1; then
    echo "FAIL host mutant 4: observer test passed (mutant not caught)" >&2
    cat "$TMP/test-4.log" >&2
    exit 1
fi
echo "PASS host mutant 4: observer test rejected the mutant"

mutant 6
if env TMPDIR=/tmp python3 "$ROOT/tests/runtime2/vm_host_memory.py" --nift "$BUILD/nift" >"$TMP/test-6.log" 2>&1; then
    echo "FAIL host mutant 6: memory test passed (mutant not caught)" >&2
    cat "$TMP/test-6.log" >&2
    exit 1
fi
echo "PASS host mutant 6: late open budget check rejected"

mutant 7
if env TMPDIR=/tmp python3 "$ROOT/tests/runtime2/vm_host_memory.py" --nift "$BUILD/nift" >"$TMP/test-7.log" 2>&1; then
    echo "FAIL host mutant 7: memory test passed (mutant not caught)" >&2
    cat "$TMP/test-7.log" >&2
    exit 1
fi
echo "PASS host mutant 7: unbounded glob traversal rejected"

for mutant in 8 9 10 12; do
    mutant $mutant
    if env TMPDIR=/tmp python3 "$ROOT/tests/runtime2/vm_host_memory.py" --nift "$BUILD/nift" >"$TMP/test-$mutant.log" 2>&1; then
        echo "FAIL host mutant $mutant: memory test passed (mutant not caught)" >&2
        cat "$TMP/test-$mutant.log" >&2
        exit 1
    fi
    # Rejected for the intended defect, not for something else.
    case $mutant in
        10) want="stream-trim-charge" ;;
        12) want="stream-monotonic-" ;;
        *) want="" ;;
    esac
    if [ -n "$want" ] && ! grep -q "$want" "$TMP/test-$mutant.log"; then
        echo "FAIL host mutant $mutant: rejected, but not by the $want regression" >&2
        cat "$TMP/test-$mutant.log" >&2
        exit 1
    fi
    if [ "$mutant" = 10 ]; then
        echo "PASS host mutant 10: accounting-formula control (NIFT_TRIM probe); behaviourally equivalent under the 9/17 cap"
    else
        echo "PASS host mutant $mutant: memory test rejected the mutant"
    fi
done
mutant 11
if env TMPDIR=/tmp python3 "$ROOT/tests/runtime2/vm_host.py" --nift "$BUILD/nift" --oracle "$ORACLE" >"$TMP/test-11.log" 2>&1; then
    echo "FAIL host mutant 11: vm_host tests passed (mutant not caught)" >&2
    cat "$TMP/test-11.log" >&2
    exit 1
fi
echo "PASS host mutant 11: symlink parity tests rejected the mutant"
# M13: secure_random_bytes checks the budget only after allocating its buffer.
mutant 13 --objects-env NIFT_TC_RANDOM_BUDGET_OBJECTS --unit tests/rt/host_random_budget.cpp
if "$BUILD/unit" >"$TMP/test-13.log" 2>&1; then
    echo "FAIL host mutant 13: random budget test passed (mutant not caught)" >&2
    cat "$TMP/test-13.log" >&2
    exit 1
fi
if ! grep -q "random-budget-before-alloc" "$TMP/test-13.log"; then
    echo "FAIL host mutant 13: rejected, but not by the random-budget-before-alloc regression" >&2
    cat "$TMP/test-13.log" >&2
    exit 1
fi
echo "PASS host mutant 13: allocation before the random-bytes budget check rejected"
# M14 (CP16 N1): restore the per-built-in recoverable code.
# Require a passing unmutated baseline first, then the structured predicate:
# exactly the three supported read-code mismatches, all other assertions
# correct, and no harness/legacy failure.
if ! python3 "$ROOT/tests/runtime2/cp16_n1.py" --nift "$ORACLE" >"$TMP/n1-baseline.log" 2>&1; then
    echo "FAIL host mutant 14: unmutated N1 baseline does not pass" >&2
    cat "$TMP/n1-baseline.log" >&2
    exit 1
fi
mutant 14
set +e
python3 "$ROOT/tests/runtime2/cp16_n1.py" --nift "$BUILD/nift" --expect-mutant >"$TMP/test-14.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    echo "FAIL host mutant 14: structured M14 predicate rejected the mutant" >&2
    cat "$TMP/test-14.log" >&2
    exit 1
fi
if ! python3 "$ROOT/tests/runtime2/cp16_n1_result.py" --log "$TMP/test-14.log" --require-mutant; then
    echo "FAIL host mutant 14: structured result validation rejected the intended M14 record" >&2
    cat "$TMP/test-14.log" >&2
    exit 1
fi
echo "PASS host mutant 14: per-built-in mapping rejected by the validated structured record"
echo "PASS host negative controls: 13 behavioural mutants rejected (1-9, 11-14); M10 accounting-formula control detected by the NIFT_TRIM probe"

# N2: cp source-read and mv source-absence observations. The host-native unit
# has the direct observer checks; the mutant must make that unit fail.
for mutant in 15 16; do
    mutant $mutant --objects-env NIFT_TC_RT_UNIT_OBJECTS --unit tests/rt/host_observer_native.cpp
    if "$BUILD/unit" >"$TMP/test-$mutant.log" 2>&1; then
        echo "FAIL N2 mutant $mutant: observer checks passed (mutant not caught)" >&2
        cat "$TMP/test-$mutant.log" >&2
        exit 1
    fi
    if ! grep -q "N2 cp/mv observer" "$TMP/test-$mutant.log" && ! grep -q "N2 cp source" "$TMP/test-$mutant.log" \
        && ! grep -q "N2 mv absence" "$TMP/test-$mutant.log"; then
        echo "FAIL N2 mutant $mutant: rejected, but not by the N2 observer case" >&2
        cat "$TMP/test-$mutant.log" >&2
        exit 1
    fi
    echo "PASS N2 mutant $mutant: host observer checks rejected the mutant"
    # The rebuild-decision witness has its own private mutant rule (Makefile, N2_REBUILD_MUTANT_DIR).
    if ! make -C "$ROOT" N2_REBUILD_MUTANT=$mutant ".build/rt/n2-rebuild-decision-m$mutant" \
        >"$TMP/rebuild-$mutant-build.log" 2>&1; then
        echo "FAIL N2 mutant $mutant: rebuild-decision build failed (harness failure)" >&2
        tail -20 "$TMP/rebuild-$mutant-build.log" >&2
        exit 1
    fi
    if ! NIFT_N2_MUTANT=$mutant "$ROOT/.build/rt/n2-rebuild-decision-m$mutant" \
        >"$TMP/rebuild-$mutant.log" 2>&1; then
        echo "FAIL N2 mutant $mutant: rebuild-decision witness rejected the intended mutant" >&2
        cat "$TMP/rebuild-$mutant.log" >&2
        exit 1
    fi
    if ! grep -q "rebuild-decision" "$TMP/rebuild-$mutant.log"; then
        echo "FAIL N2 mutant $mutant: rebuild test produced no decision banner" >&2
        cat "$TMP/rebuild-$mutant.log" >&2
        exit 1
    fi
    echo "PASS N2 mutant $mutant: production rebuild-decision witness detected the missing observation"
done
