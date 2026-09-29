#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-$(pwd)/nift}; TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
FIXTURE="$(bash tests/ffi/build_fixture.sh "$TMP" libfixture)"
printf 'lib := ffi_open("./%s")\n' "${FIXTURE##*/}" > "$TMP/ffi_math.f"
cat >> "$TMP/ffi_math.f" <<'NIFT'
add_i64 := (a, b) => ffi_call(lib, "nift_ffi_add_i64", "i64(i64,i64)", a, b)
add_f64 := (a, b) => ffi_call(lib, "nift_ffi_add_f64", "f64(f64,f64)", a, b)
pair_sum := (a, b) => ffi_call(lib, "nift_ffi_pair_sum_ptr", "i32(buffer)", ffi_struct("i32,i32", [a, b]))
export(add_i64)
export(add_f64)
export(pair_sum)
NIFT
cat > "$TMP/use.f" <<'NIFT'
@import("ffi_math.f")
print(add_i64(40, 2))
print(add_f64(1.5, 2.25))
print(pair_sum(9, 6))
NIFT
out=$(cd "$TMP" && "$NIFT" use.f)
[[ "$out" == $'42\n3.75\n15' ]]
echo 'v4.5 ffi package ergonomics: PASS'
