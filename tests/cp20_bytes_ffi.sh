#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT=${NIFT:-$ROOT/nift}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FIXTURE=$(bash "$ROOT/tests/ffi/build_fixture.sh" "$TMP")

cat >"$TMP/cp20.f" <<EOF
lib := ffi_open("$FIXTURE")
all := []
i := 0
while(i < 256) { all.push(i); i += 1 }
source := bytes(all)
buffer := ffi_buffer(source)
before := ffi_snapshot_bytes(buffer)
print(type(before)); print(before == source); print(ffi_bytes(buffer) == all)
ffi_call(lib, "nift_ffi_buffer_xor", "void(buffer,u64,u8)", buffer, 256, 255)
after := ffi_snapshot_bytes(buffer)
print(source[0]); print(source[255]); print(before[0]); print(before[255]); print(after[0]); print(after[255])
print(source == bytes(all)); print(before == source); print(after != before)
buffer = ffi_buffer(bytes([7,0,255]))
print(before == source); print(after[0]); print(ffi_snapshot_bytes(buffer) == bytes([7,0,255]))
empty_source := bytes()
empty_buffer := ffi_buffer(empty_source)
empty_snapshot := ffi_snapshot_bytes(empty_buffer)
print(type(empty_snapshot)); print(empty_snapshot.length()); print(empty_source == empty_snapshot)
large_values := []
i = 0
while(i < 65536) { large_values.push(i % 256); i += 1 }
large_source := bytes(large_values)
large_buffer := ffi_buffer(large_source)
large_snapshot := ffi_snapshot_bytes(large_buffer)
large_buffer = ffi_buffer([])
large_source = bytes()
print(large_snapshot.length()); print(large_snapshot[0]); print(large_snapshot[65535])
legacy_string := ffi_buffer("A")
legacy_array := ffi_buffer([0,128,255])
print(ffi_bytes(legacy_string) == [65]); print(ffi_bytes(legacy_array) == [0,128,255]); print(type(ffi_bytes(legacy_array)))
calls := 0
make_source := () => { calls += 1; return bytes([9]) }
fn(prepared_once()) { p := ffi_buffer(make_source()); return ffi_snapshot_bytes(p) }
print(prepared_once() == bytes([9])); print(calls)
EOF

actual=$(cd "$TMP" && "$NIFT" cp20.f)
expected=$'bytes\ntrue\ntrue\n0\n255\n0\n255\n255\n0\ntrue\ntrue\ntrue\ntrue\n255\ntrue\nbytes\n0\ntrue\n65536\n0\n255\ntrue\ntrue\narray\ntrue\n1'
[[ "$actual" == "$expected" ]] || { printf 'unexpected CP20 output:\n%s\n' "$actual" >&2; exit 1; }

reject() {
    local name=$1 source=$2 pattern=$3
    if "$NIFT" -e "$source" >"$TMP/$name.out" 2>"$TMP/$name.err"; then
        echo "CP20 expected rejection: $name" >&2
        exit 1
    fi
    grep -q "$pattern" "$TMP/$name.err"
}

reject buffer-type 'ffi_buffer(1)' 'expected string, bytes, or byte array'
reject snapshot-arity 'ffi_snapshot_bytes()' 'expected buffer handle'
reject snapshot-type 'ffi_snapshot_bytes(bytes([1]))' 'expected buffer handle'
reject no-direct-buffer "lib := ffi_open(\"$FIXTURE\"); ffi_call(lib, \"nift_ffi_buffer_xor\", \"void(buffer,u64,u8)\", bytes([1]), 1, 1)" 'buffer argument must be FFI buffer handle'

echo 'CP20 bytes/FFI copying bridges: PASS'
