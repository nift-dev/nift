#!/usr/bin/env bash
# CP13B Phase B mutation and stress gate (certification tier; expensive).
#   mutation_gate.sh /abs/path/to/nift
# Builds every E1–E8 exception mutant from a clean tree with
# mutant_build.py, including the checked-build E2 and the ASan E7 variants,
# and runs run.py's mutant modes against the unmutated oracle. Then it runs the
# 1M-catch stress and the Error lifecycle unit (whose leak probe must fail).
# A build failure, missing binary, crash, timeout or missing statistics is a
# gate failure, never a detection (run.py and mutant_build.py enforce this).
set -u -o pipefail
ORACLE="${1:?usage: mutation_gate.sh /abs/path/to/nift}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
PY="${PYTHON:-python3}"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/cp13b-mutation-gate-XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
BASE_FLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread"
ASAN_FLAGS="-std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all -pthread"
fail=0
ran=0

build() {  # build <label> <mutant> <cxxflags>
    local label=$1 n=$2 flags=$3
    if ! CXXFLAGS="$flags" "$PY" "$HERE/mutant_build.py" --root "$ROOT" --mutant "$n" \
            --output "$OUT/nift-$label" >"$OUT/build-$label.log" 2>&1 || [ ! -x "$OUT/nift-$label" ]; then
        echo "FAIL CP13B mutation gate: build of mutant $label failed (gate failure, not a detection)"
        tail -20 "$OUT/build-$label.log"
        return 1
    fi
}

check() {  # check <label> <run.py mode flag> <n>
    local label=$1 mode=$2 n=$3
    ran=$((ran + 1))
    if "$PY" "$HERE/run.py" --nift "$OUT/nift-$label" --oracle "$ORACLE" "$mode" "$n" >"$OUT/run-$label.log" 2>&1; then
        grep -E '^PASS' "$OUT/run-$label.log" | tail -1
    else
        echo "FAIL CP13B mutation gate: $mode $n ($label)"
        tail -20 "$OUT/run-$label.log"
        fail=1
    fi
}

for n in 1 2 3 4 5 6 7 8; do
    build "$n" "$n" "$BASE_FLAGS" || { fail=1; continue; }
    check "$n" --mutant "$n"
done
build 2-checked 2 "$BASE_FLAGS -DNIFT_EXC_CHECKED=1" && check 2-checked --checked-mutant 2 || fail=1
build 7-asan 7 "$ASAN_FLAGS" && check 7-asan --asan-mutant 7 || fail=1

# 1M-catch stress (accounted heap N-invariant within the 8 MB budget) and the
# Error lifecycle unit (normal must pass; its leak probe must be detected).
if "$PY" "$HERE/stress/stress.py" --nift "$ORACLE" >"$OUT/stress.log" 2>&1; then
    tail -1 "$OUT/stress.log"
else
    echo "FAIL CP13B stress"; tail -20 "$OUT/stress.log"; fail=1
fi
if bash "$HERE/stress/run_error_lifecycle.sh" >"$OUT/lifecycle.log" 2>&1; then
    tail -1 "$OUT/lifecycle.log"
else
    echo "FAIL CP13B Error lifecycle"; tail -20 "$OUT/lifecycle.log"; fail=1
fi

[ "$ran" -eq 10 ] || { echo "FAIL CP13B mutation gate: ran $ran of 10 mutant checks"; fail=1; }
[ "$fail" -eq 0 ] && echo "PASS CP13B mutation gate: 10 mutant checks (E1-E8, E2 checked, E7 ASan), stress and lifecycle"
exit "$fail"
