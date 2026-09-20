#!/usr/bin/env bash
set -euo pipefail
cxx=${CXX:-g++}
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
"$cxx" -std=c++17 -O2 -Isrc -Ijsonic/include tests/ast_expression_unit.cpp src/Ast.cpp -o "$t/ast-test"
"$t/ast-test"

nift=${NIFT:-"$(cd "$(dirname "$0")/.." && pwd)/nift"}

# Differential correctness of the CP16 hot path (AST-prepared while conditions
# and simple assignment/compound/increment bodies) against the legacy evaluator.

run_script() {
    cat > "$t/probe.f"
    timeout 30 "$nift" run "$t/probe.f" 2>&1 | head -1
}

fail=0
check() {
    local name="$1" expected="$2" actual="$3"
    if [ "$expected" != "$actual" ]; then
        echo "FAIL: $name: expected [$expected] got [$actual]" >&2
        fail=1
    fi
}

# CP17.3 hot loop: sum 0..99999 with a prepared body must be exact.
out=$(run_script <<'F'
i := 0
total := 0
while(i < 100000) { total += i; i++ }
print(total)
F
)
check "hot-loop sum" "4999950000" "$out"

# Big integer literals must NOT lose precision in an AST condition (the legacy
# evaluator treats them exactly; strtod would make them equal and hang).
out=$(run_script <<'F'
x := 0
while(9007199254740993 == 9007199254740992) { x += 1 }
print(x)
F
)
check "big-int equality" "0" "$out"

# Compound arithmetic beyond 2^53 must not round (legacy int64/StrNumber path).
out=$(run_script <<'F'
x := 9007199254740992
i := 0
while(i < 10) { x += 1; i += 1 }
print(x)
F
)
check "large-value compound" "9007199254741002" "$out"

# Live bindings: the prepared condition must observe the current value.
out=$(run_script <<'F'
x := 1
i := 0
while(i < 5) { x += 1; i += 1 }
print(x)
F
)
check "live binding" "6" "$out"

# Precedence/associativity through the prepared condition.
out=$(run_script <<'F'
x := 0
while(1 + 2 * 3 == 7 && x < 1) { x += 1 }
print(x)
F
)
check "precedence" "1" "$out"

if [ "$fail" -ne 0 ]; then
    echo "v4.4 AST expression smoke: FAIL" >&2
    exit 1
fi
echo 'v4.4 AST expression smoke: PASS'