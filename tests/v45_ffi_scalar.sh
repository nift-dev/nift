#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}; mkdir -p .build
cc -std=c99 -Wall -Wextra -fPIC -shared tests/ffi/fixture.c -o .build/libnift_ffi_fixture.so
cat > .build/ffi-scalar.f <<'NIFT'
lib := ffi_open(".build/libnift_ffi_fixture.so")
print(ffi_call(lib, "nift_ffi_add_i64", "i64(i64,i64)", 2, 3))
print(ffi_call(lib, "nift_ffi_strlen", "i32(cstr)", "hello"))
print(ffi_call(lib, "nift_ffi_greeting", "cstr()"))
print(ffi_call(lib, "nift_ffi_add_f64", "f64(f64,f64)", 1.25, 2.5))
ffi_close(lib)
NIFT
out=$($NIFT .build/ffi-scalar.f)
[[ "$out" == $'5\n5\nhello from ffi\n3.75' ]]
cat > .build/ffi-bad.f <<'NIFT'
lib := ffi_open(".build/libnift_ffi_fixture.so")
ffi_call(lib, "missing_symbol", "i64()")
NIFT
if $NIFT .build/ffi-bad.f >/dev/null 2>.build/ffi-bad.err; then exit 1; fi
grep -q 'symbol lookup failed\|symbol not found' .build/ffi-bad.err
cat > .build/ffi-close.f <<'NIFT'
lib := ffi_open(".build/libnift_ffi_fixture.so")
ffi_close(lib)
ffi_close(lib)
NIFT
if $NIFT .build/ffi-close.f >/dev/null 2>.build/ffi-close.err; then exit 1; fi
grep -q 'invalid or closed library handle' .build/ffi-close.err
echo 'v4.5 ffi scalar smoke: PASS'
