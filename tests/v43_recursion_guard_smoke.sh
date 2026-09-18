#!/usr/bin/env bash
# Recursion depth guard regression (release hardening): unbounded lambda and
# block-function recursion must produce a clean bounded error, never a stack
# overflow crash.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="${NIFT_BIN:-$ROOT/nift}"
TMP=$(mktemp -d); trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP"

# shallow recursion still works
cat > ok.nift <<'NIFT'
f := (n => n == 0 ? 0 : f(n - 1))
print(f(50))
fn(fact(n)) { if(n <= 1) { return 1 }
return n * fact(n - 1) }
print(fact(10))
NIFT
[ "$("$NIFT" run ok.nift)" = $'0\n3628800' ] && echo "PASS  shallow-recursion" || { echo "FAIL  shallow-recursion" >&2; exit 1; }

# deep lambda recursion -> clean bounded error, no crash (rc != 139/134)
cat > deep.nift <<'NIFT'
f := (n => n == 0 ? 0 : f(n - 1))
print(f(5000))
NIFT
set +e
"$NIFT" run deep.nift >deep.out 2>deep.err
rc=$?
set -e
[ $rc -ne 0 ] || { echo "FAIL  deep-lambda expected error" >&2; exit 1; }
[ $rc -eq 139 ] || [ $rc -eq 134 ] && { echo "FAIL  deep-lambda crashed rc=$rc" >&2; exit 1; }
grep -q 'callable recursion depth exceeded' deep.err && echo "PASS  deep-lambda-guard" || { echo "FAIL  deep-lambda-guard: $(head -1 deep.err)" >&2; exit 1; }

# deep block-function recursion -> clean bounded error
cat > deepfn.nift <<'NIFT'
fn(inf(x)) { return inf(x) }
print(inf(0))
NIFT
set +e
"$NIFT" run deepfn.nift >df.out 2>df.err
rc=$?
set -e
[ $rc -ne 0 ] || { echo "FAIL  deep-fn expected error" >&2; exit 1; }
[ $rc -eq 139 ] || [ $rc -eq 134 ] && { echo "FAIL  deep-fn crashed rc=$rc" >&2; exit 1; }
grep -q 'callable recursion depth exceeded' df.err && echo "PASS  deep-fn-guard" || { echo "FAIL  deep-fn-guard: $(head -1 df.err)" >&2; exit 1; }

# huge integer literal diagnostic (release hardening): out-of-int64 literals
# produce the precise range error, and int64 boundaries still work
cat > big.nift <<'NIFT'
print(9223372036854775807)
NIFT
[ "$("$NIFT" run big.nift)" = "9223372036854775807" ] && echo "PASS  int64-max" || { echo "FAIL  int64-max" >&2; exit 1; }
cat > toobig.nift <<'NIFT'
print(9223372036854775808)
NIFT
set +e
"$NIFT" run toobig.nift >tb.out 2>tb.err
rc=$?
set -e
[ $rc -ne 0 ] || { echo "FAIL  int64-overflow expected error" >&2; exit 1; }
grep -q 'integer literal outside signed 64-bit range' tb.err && echo "PASS  int64-overflow-diagnostic" || { echo "FAIL  int64-overflow-diagnostic: $(head -1 tb.err)" >&2; exit 1; }

echo "recursion + integer-boundary guard smoke passed"

# huge integer literal diagnostic (release hardening): out-of-int64 literals
# produce the precise range error, and int64 boundaries still work
cat > big.nift <<'NIFT'
print(9223372036854775807)
NIFT
[ "$("$NIFT" run big.nift)" = "9223372036854775807" ] && echo "PASS  int64-max" || { echo "FAIL  int64-max" >&2; exit 1; }
cat > toobig.nift <<'NIFT'
print(9223372036854775808)
NIFT
set +e
"$NIFT" run toobig.nift >tb.out 2>tb.err
rc=$?
set -e
[ $rc -ne 0 ] || { echo "FAIL  int64-overflow expected error" >&2; exit 1; }
grep -q 'integer literal outside signed 64-bit range' tb.err && echo "PASS  int64-overflow-diagnostic" || { echo "FAIL  int64-overflow-diagnostic: $(head -1 tb.err)" >&2; exit 1; }

echo "recursion + integer-boundary guard smoke passed"
