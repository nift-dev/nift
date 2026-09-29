# CP20 bytes/FFI copying bridges

## Contract

- `ffi_buffer(bytes)` copies the complete immutable byte sequence into a new
  parser-owned mutable FFI buffer.
- `ffi_snapshot_bytes(buffer)` copies current buffer contents into first-class
  immutable bytes.
- Neither direction aliases storage. Existing `ffi_buffer(string)`,
  `ffi_buffer(array)` and integer-array-returning `ffi_bytes()` remain intact.
- FFI buffers remain mutable, aliasing, parser-owned and non-transferable;
  immutable bytes are never passed directly as writable native buffers.

## Focused evidence

`tests/cp20_bytes_ffi.sh` covers empty values, embedded NUL/high octets, all
values 0..255, 64 KiB payloads, native mutation isolation in both directions,
multiple snapshots, snapshot independence after handle rebinding, once-only
prepared evaluation, legacy helpers, invalid
types/arity and rejection of bytes by `ffi_call` buffer arguments.

The durable `test-cp20-bytes` target is included in `make test`, Gate 6B on
Linux GCC/Clang, macOS Clang and Windows MinGW, and the retained Gate 6A-R FFI
wall. The existing bytes sanitizer targets also execute CP20 under ASan/UBSan
and TSan.

## Scope exclusions

No C/C++ embedding ABI, maintained binding, Jsonic, mutable general bytes,
zero-copy view, pinning, retained-pointer or proof-consumer API is added.
