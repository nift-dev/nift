#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
mkdir -p .build
FIXTURE="$(bash tests/ffi/build_fixture.sh .build)"

cat > .build/gate6ar-values.json <<'JSON'
{"i64min":-9223372036854775808,"u64max":18446744073709551615}
JSON
printf 'lib := ffi_open("%s")\n' "$FIXTURE" > .build/gate6ar-ffi.f
cat >> .build/gate6ar-ffi.f <<'NIFT'
@json(n, "gate6ar-values.json")
print(ffi_call(lib,"nift_ffi_not_bool","bool(bool)",true))
print(ffi_call(lib,"nift_ffi_i8","i8(i8)",-128)); print(ffi_call(lib,"nift_ffi_u8","u8(u8)",255))
print(ffi_call(lib,"nift_ffi_i16","i16(i16)",-32768)); print(ffi_call(lib,"nift_ffi_u16","u16(u16)",65535))
print(ffi_call(lib,"nift_ffi_i32","i32(i32)",-2147483648)); print(ffi_call(lib,"nift_ffi_u32","u32(u32)",4294967295))
print(ffi_call(lib,"nift_ffi_i64","i64(i64)",n.i64min)); print(ffi_call(lib,"nift_ffi_u64","u64(u64)",n.u64max))
print(ffi_call(lib,"nift_ffi_add_f32","f32(f32,f32)",1.25,2.5)); print(ffi_call(lib,"nift_ffi_add_f64","f64(f64,f64)",1.25,2.5))
print(ffi_call(lib,"nift_ffi_strlen","i32(cstr)",null)); print(ffi_call(lib,"nift_ffi_greeting","cstr()")); print(ffi_call(lib,"nift_ffi_null_cstr","cstr()")); print(ffi_call(lib,"nift_ffi_null_ptr","ptr()"))
b := ffi_buffer([1]); p := ffi_call(lib,"nift_ffi_identity_ptr","ptr(buffer)",b); print(ffi_call(lib,"nift_ffi_identity_ptr","ptr(ptr)",p) != null)
mut := ffi_buffer([1,2]); ffi_call(lib,"nift_ffi_buffer_xor","void(buffer,u64,u8)",mut,2,255); bytes := ffi_bytes(mut); print(bytes[0]); print(bytes[1])
ffi_call(lib,"nift_ffi_set_void","void(i32)",-77); print(ffi_call(lib,"nift_ffi_get_void","i32()"))
print(ffi_call(lib,"nift_ffi_arity0","i64()")); print(ffi_call(lib,"nift_ffi_arity1","i64(i8)",-1)); print(ffi_call(lib,"nift_ffi_arity2","i64(i8,u16)",-1,2)); print(ffi_call(lib,"nift_ffi_arity3","i64(i8,u16,i32)",-1,2,-3)); print(ffi_call(lib,"nift_ffi_arity4","i64(i8,u16,i32,u64)",-1,2,-3,4)); print(ffi_call(lib,"nift_ffi_arity5","i64(i8,u16,i32,u64,ptr)",-1,2,-3,4,p)); print(ffi_call(lib,"nift_ffi_arity6","i64(i8,u16,i32,u64,ptr,cstr)",-1,2,-3,4,p,"abc"))
print(ffi_call(lib,"nift_ffi_f32_0","f32()")); print(ffi_call(lib,"nift_ffi_f32_1","f32(f32)",1)); print(ffi_call(lib,"nift_ffi_f32_2","f32(f32,f32)",1,2)); print(ffi_call(lib,"nift_ffi_f32_3","f32(f32,f32,f32)",1,2,3)); print(ffi_call(lib,"nift_ffi_f32_4","f32(f32,f32,f32,f32)",1,2,3,4))
print(ffi_call(lib,"nift_ffi_f64_0","f64()")); print(ffi_call(lib,"nift_ffi_f64_1","f64(f64)",1)); print(ffi_call(lib,"nift_ffi_f64_2","f64(f64,f64)",1,2)); print(ffi_call(lib,"nift_ffi_f64_3","f64(f64,f64,f64)",1,2,3)); print(ffi_call(lib,"nift_ffi_f64_4","f64(f64,f64,f64,f64)",1,2,3,4))
print(ffi_sizeof("i8,u32,i64,f64,bool")); print(ffi_call(lib,"nift_ffi_layout_size","u64()"))
layout := ffi_struct("i8,u32,i64,f64,bool", [-7,4294967295,n.i64min,3.5,true])
print(ffi_call(lib,"nift_ffi_check_layout_ptr","bool(buffer)",layout))
print(ffi_sizeof("bool,i8,u8,i16,u16,i32,u32,i64,u64,f32,f64,ptr,cstr")); print(ffi_call(lib,"nift_ffi_all_scalars_size","u64()"))
all := ffi_struct("bool,i8,u8,i16,u16,i32,u32,i64,u64,f32,f64", [true,-128,255,-32768,65535,-2147483648,4294967295,n.i64min,n.u64max,1.25,2.5])
print(ffi_call(lib,"nift_ffi_check_all_scalars_ptr","bool(buffer)",all))
fn(twice(x)) { return x * 2 }
cb := ffi_callback(twice,"i64(i64)")
print(ffi_call(lib,"nift_ffi_call_cb_simple","i64(callback_i64,i64)",cb,21))
NIFT

out=$($NIFT .build/gate6ar-ffi.f)
expected=$'false\n-128\n255\n-32768\n65535\n-2147483648\n4294967295\n-9223372036854775808\n18446744073709551615\n3.75\n3.75\n-1\nhello from ffi\nnull\nnull\ntrue\n254\n253\n-77\n10\n-1\n1\n-2\n2\n3\n6\n0.5\n1\n3\n6\n10\n0.25\n1\n3\n6\n10\n32\n32\ntrue\n64\n64\ntrue\n42'
[[ "$out" == "$expected" ]] || { printf 'unexpected output:\n%s\n' "$out" >&2; exit 1; }

# A callback activated before a later conversion failure must not poison the
# next call's thread-local activation scope.
printf 'lib := ffi_open("%s")\n' "$FIXTURE" > .build/gate6ar-callback-cleanup.f
cat >> .build/gate6ar-callback-cleanup.f <<'NIFT'
fn(id(x)) { return x }
cb := ffi_callback(id,"i64(i64)")
ffi_call(lib,"nift_ffi_call_cb_simple","i64(callback_i64,i64)",cb,"bad")
NIFT
if $NIFT .build/gate6ar-callback-cleanup.f >/dev/null 2>.build/gate6ar-callback-cleanup.err; then exit 1; fi
grep -q 'integer argument out of range' .build/gate6ar-callback-cleanup.err

# The interactive shell retains one Parser after controlled errors, proving the
# activation guard restores scope rather than relying on Parser destruction.
printf 'lib := ffi_open("%s")\n' "$FIXTURE" > .build/gate6ar-callback-shell.in
cat >> .build/gate6ar-callback-shell.in <<'NIFT'
fn(id(x)) { return x }
good := ffi_callback(id,"i64(i64)")
ffi_call(lib,"nift_ffi_call_cb_simple","i64(callback_i64,i64)",good,"bad")
print(ffi_call(lib,"nift_ffi_call_cb_simple","i64(callback_i64,i64)",good,9))
fn(bad_result(x)) { return "bad" }
bad := ffi_callback(bad_result,"i64(i64)")
ffi_call(lib,"nift_ffi_call_cb_simple","i64(callback_i64,i64)",bad,1)
print(ffi_call(lib,"nift_ffi_call_cb_simple","i64(callback_i64,i64)",good,10))
fn(nested(x)) { return ffi_call(lib,"nift_ffi_call_cb_simple","i64(callback_i64,i64)",good,x) }
outer := ffi_callback(nested,"i64(i64)")
ffi_call(lib,"nift_ffi_call_cb_simple","i64(callback_i64,i64)",outer,1)
print(ffi_call(lib,"nift_ffi_call_cb_simple","i64(callback_i64,i64)",good,11))
NIFT
$NIFT <.build/gate6ar-callback-shell.in >.build/gate6ar-callback-shell.out 2>.build/gate6ar-callback-shell.err
[[ "$(<.build/gate6ar-callback-shell.out)" == $'9\n10\n11' ]]
grep -q 'integer argument out of range' .build/gate6ar-callback-shell.err
grep -q 'callback result must be an integer' .build/gate6ar-callback-shell.err
grep -q 'nested native callback activation is not supported' .build/gate6ar-callback-shell.err

for sig in 'buffer()' 'callback_i64()'; do
  printf 'lib := ffi_open("%s")\nffi_call(lib,"nift_ffi_null_ptr","%s")\n' "$FIXTURE" "$sig" > .build/gate6ar-bad-return.f
  if $NIFT .build/gate6ar-bad-return.f >/dev/null 2>.build/gate6ar-bad-return.err; then exit 1; fi
  grep -q 'unsupported return type' .build/gate6ar-bad-return.err
done

for spec in \
  'nift_ffi_add_f64|f64(f64,i64)|1,2|mixed floating/integer signatures' \
  'nift_ffi_add_f64|f64(f32,f64)|1,2|mixed floating/integer signatures' \
  'nift_ffi_f64_4|f64(f64,f64,f64,f64,f64)|1,2,3,4,5|supports up to 4 arguments' \
  'nift_ffi_arity6|i64(i8,u16,i32,u64,ptr,cstr,i8)|1,2,3,4,null,"x",5|supports at most 6 arguments' \
  'nift_ffi_pair_sum|i32(struct)|1|unsupported argument type'; do
  IFS='|' read -r symbol sig values message <<<"$spec"
  printf 'lib := ffi_open("%s")\nffi_call(lib,"%s","%s",%s)\n' "$FIXTURE" "$symbol" "$sig" "$values" > .build/gate6ar-restriction.f
  if $NIFT .build/gate6ar-restriction.f >/dev/null 2>.build/gate6ar-restriction.err; then exit 1; fi
  grep -q "$message" .build/gate6ar-restriction.err
done

for layout in ptr cstr; do
  printf 'ffi_struct("%s", [null])\n' "$layout" > .build/gate6ar-struct-pointer.f
  if $NIFT .build/gate6ar-struct-pointer.f >/dev/null 2>.build/gate6ar-struct-pointer.err; then exit 1; fi
  grep -q 'pointer fields are not supported' .build/gate6ar-struct-pointer.err
done
echo 'Gate 6A-R FFI ABI wall: PASS'
