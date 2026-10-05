#!/usr/bin/env bash
# Very-loose scripting-runtime performance guard. The maintained website-build
# performance guards do not exercise Nift's interpreter hot path, so a pure
# interpreter regression (e.g. a per-operation slowdown) would go unnoticed.
# This guard runs representative numeric-loop, array-index, and function-call
# workloads and asserts each completes under a generous multiple of the current
# baseline (median of 3 runs). The ceiling is intentionally loose: it is robust
# under shared-CI load while still catching order-of-magnitude scripting
# regressions. It is not a tight microbenchmark.
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-script-perf.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

loop() {
    cat > "$TMP/loop.f" <<'EOF'
fn(main(n)) { s := 0; i := 0; while(i < n) { s += i; i += 1 } print(s) }
main(2000000)
EOF
}
array_index() {
    cat > "$TMP/array_index.f" <<'EOF'
fn(main(n)) { arr := []; i := 0; while(i < 10000) { arr.push(i); i += 1 } total := 0; i = 0; while(i < n) { total += arr[i % 10000]; i += 1 } print(total) }
main(2000000)
EOF
}
fn_calls() {
    cat > "$TMP/fn_calls.f" <<'EOF'
fn(noop()) { return 0 }
fn(main(n)) { total := 0; i := 0; while(i < n) { total += noop(); i += 1 } print(total) }
main(2000000)
EOF
}

run_median() {
    local name="$1"
    local times=()
    for _ in 1 2 3; do
        local t0
        t0=$(date +%s%N)
        "$NIFT_BIN" "$TMP/$name.f" > /dev/null 2>&1 || return 1
        times+=($(( ($(date +%s%N) - t0) / 1000000 )))
    done
    printf '%s\n' "${times[@]}" | sort -n | sed -n '2p'
}

check() {
    local name="$1" ceiling="$2" baseline="$3"
    local med
    med=$(run_median "$name")
    if [ "$med" -gt "$ceiling" ]; then
        echo "FAIL: $name took ${med}ms (baseline ~${baseline}ms, ceiling ${ceiling}ms)" >&2
        exit 1
    fi
    echo "PASS: $name ${med}ms (baseline ~${baseline}ms)"
}

loop
array_index
fn_calls
# Baselines on a development host: loop ~1.3s, array_index ~1.9s, fn_calls ~1.7s.
# The effective ceiling multiple is ~3-6x of the dev-host baseline; this is a
# robust catastrophic-regression tripwire (it catches order-of-magnitude
# scripting blowups, not a ~40% linear regression).
check loop 8000 1300
check array_index 8000 1900
check fn_calls 8000 1700
echo "scripting-runtime performance guard: PASS"