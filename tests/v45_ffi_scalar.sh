#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}; mkdir -p .build
FIXTURE="$(bash tests/ffi/build_fixture.sh .build)"
cat > .build/ffi-integers.json <<'JSON'
{"u64":18446744073709551615,"u8_bad":256,"i8_bad":-129,"u16_bad":65536,"i16_bad":-32769,"u32_bad":4294967296,"i32_bad":-2147483649,"u64_bad":18446744073709551616,"i64_bad":9223372036854775808,"negative":-1,"fraction":1.5}
JSON
printf 'lib := ffi_open("%s")\n' "$FIXTURE" > .build/ffi-scalar.f
cat >> .build/ffi-scalar.f <<'NIFT'
@json(n, "ffi-integers.json")
print(ffi_call(lib, "nift_ffi_add_i64", "i64(i64,i64)", 2, 3))
print(ffi_call(lib, "nift_ffi_strlen", "i32(cstr)", "hello"))
print(ffi_call(lib, "nift_ffi_greeting", "cstr()"))
print(ffi_call(lib, "nift_ffi_add_f64", "f64(f64,f64)", 1.25, 2.5))
print(ffi_call(lib, "nift_ffi_xor_u64", "u64(u64,u64)", n.u64, 0))
print(ffi_call(lib, "nift_ffi_xor_u64", "u64(u64,u64)", n.u64, 0) / 2)
ffi_close(lib)
NIFT
out=$($NIFT .build/ffi-scalar.f)
[[ "$out" == $'5\n5\nhello from ffi\n3.75\n18446744073709551615\n9.223372036854776e+18' ]]

for call in \
  'ffi_call(lib, "nift_ffi_xor_u64", "u64(u8,u64)", n.u8_bad, 0)' \
  'ffi_call(lib, "nift_ffi_xor_u64", "u64(i8,u64)", n.i8_bad, 0)' \
  'ffi_call(lib, "nift_ffi_xor_u64", "u64(u16,u64)", n.u16_bad, 0)' \
  'ffi_call(lib, "nift_ffi_xor_u64", "u64(i16,u64)", n.i16_bad, 0)' \
  'ffi_call(lib, "nift_ffi_xor_u64", "u64(u32,u64)", n.u32_bad, 0)' \
  'ffi_call(lib, "nift_ffi_xor_u64", "u64(i32,u64)", n.i32_bad, 0)' \
  'ffi_call(lib, "nift_ffi_xor_u64", "u64(u64,u64)", n.u64_bad, 0)' \
  'ffi_call(lib, "nift_ffi_add_i64", "i64(i64,i64)", n.i64_bad, 0)' \
  'ffi_call(lib, "nift_ffi_xor_u64", "u64(u64,u64)", n.negative, 0)' \
  'ffi_call(lib, "nift_ffi_xor_u64", "u64(u64,u64)", n.fraction, 0)'; do
  printf '@json(n, "ffi-integers.json")\nlib := ffi_open("%s")\n%s\n' "$FIXTURE" "$call" >.build/ffi-range.f
  if $NIFT .build/ffi-range.f >/dev/null 2>.build/ffi-range.err; then exit 1; fi
  grep -q 'integer argument out of range' .build/ffi-range.err
done
printf 'lib := ffi_open("%s")\n' "$FIXTURE" > .build/ffi-bad.f
cat >> .build/ffi-bad.f <<'NIFT'
ffi_call(lib, "missing_symbol", "i64()")
NIFT
if $NIFT .build/ffi-bad.f >/dev/null 2>.build/ffi-bad.err; then exit 1; fi
grep -q 'symbol lookup failed\|symbol not found' .build/ffi-bad.err
printf 'lib := ffi_open("%s")\n' "$FIXTURE" > .build/ffi-close.f
cat >> .build/ffi-close.f <<'NIFT'
ffi_close(lib)
ffi_close(lib)
NIFT
if $NIFT .build/ffi-close.f >/dev/null 2>.build/ffi-close.err; then exit 1; fi
grep -q 'invalid or closed library handle' .build/ffi-close.err
echo 'v4.5 ffi scalar smoke: PASS'
