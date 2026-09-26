#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}; mkdir -p .build
cc -std=c99 -Wall -Wextra -fPIC -shared tests/ffi/fixture.c -o .build/libnift_ffi_fixture.so
cat > .build/ffi-memory.f <<'NIFT'
lib := ffi_open(".build/libnift_ffi_fixture.so")
b := ffi_buffer([1, 2, 3])
ffi_call(lib, "nift_ffi_buffer_xor", "void(buffer,u64,u8)", b, 3, 255)
bytes := ffi_bytes(b)
print(bytes[0]); print(bytes[1]); print(bytes[2])
s := ffi_struct("i32,i32", [7, 8])
print(ffi_sizeof("i32,i32"))
print(ffi_call(lib, "nift_ffi_pair_sum_ptr", "i32(buffer)", s))
fn(double_it(x)) { return x * 2 }
cb := ffi_callback(double_it, "i64(i64)")
print(ffi_call(lib, "nift_ffi_call_cb_simple", "i64(callback_i64,i64)", cb, 21))
NIFT
out=$($NIFT .build/ffi-memory.f)
[[ "$out" == $'254\n253\n252\n8\n15\n42' ]]
cat > .build/ffi-cb-bad.f <<'NIFT'
fn(f(x)) { return x }
ffi_callback(f, "f64(f64)")
NIFT
if $NIFT .build/ffi-cb-bad.f >/dev/null 2>.build/ffi-cb-bad.err; then exit 1; fi
grep -q 'supports only i64(i64)' .build/ffi-cb-bad.err
echo 'v4.5 ffi memory/callback smoke: PASS'
