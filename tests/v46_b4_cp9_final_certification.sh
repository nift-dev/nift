#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
NIFT=${NIFT_BIN:-${NIFT:-$ROOT/nift}}
case "$NIFT" in /*) ;; *) NIFT="$ROOT/$NIFT" ;; esac

echo '=== CP9 1/7: recoverable registry / producer audit ==='
# Every Recoverable registry code must have at least one real producer that
# constructs an Error through fail_recoverable, and fail_recoverable/fail_fatal
# must never be passed a code of the wrong disposition (both helpers throw
# std::logic_error on a disposition mismatch, so this is enforced mechanically).
recoverable_codes=$(sed -n '127,149p' src/Diagnostic.h | grep -oE '"[a-z_.]+"' | tr -d '"')
count=0
for code in $recoverable_codes; do
    enum=$(sed -n '127,149p' src/Diagnostic.h | grep "\"$code\"" | grep -oE 'NIFT_DIAGNOSTIC_CASE\([A-Za-z]+' | sed 's/NIFT_DIAGNOSTIC_CASE(//' | head -1)
    if [ -z "$enum" ]; then
        echo "CP9 FAIL: could not map Recoverable code $code to enum" >&2
        exit 1
    fi
    if [ "$enum" = "UserRaised" ]; then
        # UserRaised is produced by the @__throw handler (active_recoverable_
        # Error construction + make_diagnostic(UserRaised)) and by the error()
        # constructor; not via fail_recoverable.
        if ! grep -q 'runtime_error_with_origin' src/ParserTemplate.cpp || \
           ! grep -q 'DiagnosticCode::UserRaised' src/ParserTemplate.cpp; then
            echo "CP9 FAIL: UserRaised throw producer missing" >&2
            exit 1
        fi
    else
        producers=$(grep -rn "fail_recoverable(.*DiagnosticCode::$enum\b" src/ | wc -l)
        if [ "$producers" -lt 1 ]; then
            echo "CP9 FAIL: Recoverable code $code has no fail_recoverable producer" >&2
            exit 1
        fi
    fi
    count=$((count+1))
done
echo "  all $count Recoverable registry codes have fail_recoverable producers"

# fail_recoverable / fail_fatal disposition validation
if ! grep -q 'fail_recoverable requires a recoverable diagnostic code' src/Parser.cpp || \
   ! grep -q 'fail_fatal requires a fatal diagnostic code' src/Parser.cpp; then
    echo 'CP9 FAIL: disposition validation missing in fail_recoverable/fail_fatal' >&2
    exit 1
fi

echo '=== CP9 2/7: fatal-boundary audit (broad classes stay non-catchable) ==='
TMP_CP9=$(mktemp -d)
for probe in \
    'x := 1 +' \
    'missing_binding_xyz()' \
    'v := 1 / 0' \
    'fn(f()) { return 1 }; f(1, 2)' \
    'f := file("no.txt"); f.close()' \
    'throw 42' \
    'import(42)'; do
    if "$NIFT" -e "try { $probe } catch(e) { print(\"CAUGHT\") }" >"$TMP_CP9/out" 2>"$TMP_CP9/err"; then
        echo "CP9 FAIL: fatal class unexpectedly succeeded: $probe" >&2
        exit 1
    fi
    if grep -q CAUGHT "$TMP_CP9/out" 2>/dev/null; then
        echo "CP9 FAIL: fatal class became catchable: $probe" >&2
        exit 1
    fi
done
rm -rf "$TMP_CP9"
echo '  syntax/name/arity/div0/lifecycle/type fatal classes bypass catch'

echo '=== CP9 3/7: resource / lifetime final wall ==='
bash tests/v44_package_language_smoke.sh >/dev/null 2>&1
bash tests/v44_relative_import_ownership_smoke.sh >/dev/null 2>&1
bash tests/v46_import_worker_ownership_smoke.sh >/dev/null 2>&1
echo '  package language / import ownership / worker ownership walls PASS'

echo '=== CP9 4/7: concurrency + FFI + memory walls ==='
NIFT="$NIFT" bash tests/v45_threads.sh
NIFT="$NIFT" bash tests/v45_async.sh
NIFT="$NIFT" bash tests/v45_concurrency_hardening.sh
NIFT="$NIFT" bash tests/v45_mutex.sh
NIFT="$NIFT" bash tests/v45_atomics.sh
echo '  v4.5 concurrency walls PASS'

echo '=== CP9 5/7: full Batch 4 aggregate gate (CP1..CP8) ==='
make test-v46-b4-cp8
echo '  CP1..CP8 aggregate gate PASS'

echo '=== CP9 6/7: embedding / C ABI / bindings / staged consumer ==='
bash tests/v46_b4_cp8_embedding_abi.sh
echo '  CP8 embedding/C ABI + bindings + staged consumer PASS'

echo '=== CP9 7/7: sanitizer gates (ASan/UBSan + TSan concurrency) ==='
make test-v45-concurrency-sanitize
make test-v45-concurrency-tsan
echo '  ASan/UBSan + TSan concurrency gates PASS'

echo 'v4.6 Batch 4 CP9 final certification: PASS'