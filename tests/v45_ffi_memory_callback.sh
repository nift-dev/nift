#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}; mkdir -p .build
cc -std=c99 -Wall -Wextra -fPIC -shared tests/ffi/fixture.c -o .build/libnift_ffi_fixture.so
cat > .build/ffi-struct-integers.json <<'JSON'
{"i8":-128,"u8":255,"i16":-32768,"u16":65535,"i32":-2147483648,"u32":4294967295,"i64":-9223372036854775808,"u64":18446744073709551615,"u64_bad":18446744073709551616}
JSON
cat > .build/ffi-memory.f <<'NIFT'
@json(n, "ffi-struct-integers.json")
lib := ffi_open(".build/libnift_ffi_fixture.so")
b := ffi_buffer([1, 2, 3])
ffi_call(lib, "nift_ffi_buffer_xor", "void(buffer,u64,u8)", b, 3, 255)
bytes := ffi_bytes(b)
print(bytes[0]); print(bytes[1]); print(bytes[2])
s := ffi_struct("i32,i32", [7, 8])
print(ffi_sizeof("i32,i32"))
print(ffi_call(lib, "nift_ffi_pair_sum_ptr", "i32(buffer)", s))
bounds := ffi_struct("i8,u8,i16,u16,i32,u32,i64,u64,bool", [n.i8, n.u8, n.i16, n.u16, n.i32, n.u32, n.i64, n.u64, true])
print(ffi_call(lib, "nift_ffi_check_integer_bounds_ptr", "bool(buffer)", bounds))
fn(double_it(x)) { return x * 2 }
cb := ffi_callback(double_it, "i64(i64)")
print(ffi_call(lib, "nift_ffi_call_cb_simple", "i64(callback_i64,i64)", cb, 21))
NIFT
out=$($NIFT .build/ffi-memory.f)
[[ "$out" == $'254\n253\n252\n8\n15\ntrue\n42' ]]

for values in \
  '[128, 0, 0, 0, 0, 0, 0, 0, true]' \
  '[0, 256, 0, 0, 0, 0, 0, 0, true]' \
  '[0, 0, -32769, 0, 0, 0, 0, 0, true]' \
  '[0, 0, 0, 0, 0, 0, 0, n.u64_bad, true]' \
  '[0, 0, 0, 0, 0, 0, 0, 0, 1]'; do
  printf '@json(n, "ffi-struct-integers.json")\nffi_struct("i8,u8,i16,u16,i32,u32,i64,u64,bool", %s)\n' "$values" >.build/ffi-struct-range.f
  if $NIFT .build/ffi-struct-range.f >/dev/null 2>.build/ffi-struct-range.err; then exit 1; fi
  grep -q 'integer field out of range\|bool field must be bool' .build/ffi-struct-range.err
done
cat > .build/ffi-cb-bad.f <<'NIFT'
fn(f(x)) { return x }
ffi_callback(f, "f64(f64)")
NIFT
if $NIFT .build/ffi-cb-bad.f >/dev/null 2>.build/ffi-cb-bad.err; then exit 1; fi
grep -q 'supports only i64(i64)' .build/ffi-cb-bad.err
echo 'v4.5 ffi memory/callback smoke: PASS'
