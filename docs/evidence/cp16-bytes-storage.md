# CP16 immutable bytes storage

Date: 2026-09-29

Status: **complete candidate** at the CP16 implementation tree.

## Contract implemented

- `RuntimeType::Bytes` is a distinct runtime tag. Its contiguous byte sequence
  is held by `shared_ptr<const vector<uint8_t>>`, so copies and nested values may
  share backing without exposing mutation.
- Empty bytes are false and non-empty bytes are true. Equality compares complete
  byte sequences and never equates bytes with strings or integer arrays.
- Runtime fingerprints frame the bytes tag, length and exact binary payload,
  including embedded NUL and high bytes.
- Arrays, objects, Engine defaults, Context overlays and host-callable arguments
  and results carry bytes through the existing recursive value seam.
- Checked runtime-to-JSON conversion rejects top-level and nested bytes with
  `bytes values are not JSON serializable` and does not modify the output on
  failure. The legacy return-by-value helper and public `Value::json()` throw the
  same deterministic error rather than silently producing JSON null.
- Direct text rendering rejects bytes with a controlled `RenderResult` or
  `ScriptResult` error. General value serialization rejects bytes recursively.
- Public C++ exposes `Value::Bytes`, `Value::Type::Bytes`, the bytes constructor,
  `is_bytes()` and a const `bytes()` accessor. Existing enum ordinals are
  preserved by appending the new type.

## Focused evidence

- `make -j2 test-runtime-value`: pass. Covers binary payloads, empty/non-empty
  truthiness, immutable shared backing, equality, nesting, fingerprints and
  atomic top-level/nested JSON rejection.
- `make -j2 test-engine-bindings`: pass. Covers public type/accessors, copy and
  nesting, Engine/Context transport, host-callable return, bytes equality and
  controlled render, concatenation, print and validation rejection.
- `make -j2 test-public-header`: pass. The public-only consumer compiles and uses
  the bytes surface; public headers are now explicit target prerequisites.
- `make -j2 test`: pass.
- `make -j2 test-embed`: pass, including C/C++ consumers and C ABI regression.
- `make -j2 test-gate6ar-ffi`: pass.
- `make -j2 test-sanitize`: pass with GCC ASan/UBSan instrumentation.
- `git diff --check`: pass.

The defect-focused review found and repaired scripting exception leaks, unchecked
schema/validation JSON conversion, an empty-buffer fingerprint edge and a stale
public-header probe dependency. Its final pass found no blocking, high or medium
defect.

## Deferred by scope

CP16 does not add language construction, indexing, slicing, concatenation,
UTF-8 conversion, introspection, binary file/stream I/O, immutable/mutable FFI
buffer conversion, C ABI bytes functions, binding exposure or proof-consumer
integration. CP17 begins the language semantics and exhaustive text-boundary
audit. Jsonic remains strict JSON and has no bytes representation.
