# CP17 bytes language semantics

Date: 2026-09-29

Status: **complete candidate** at the CP17 implementation tree.

## Contract implemented

- `bytes()` and checked `bytes(array)` construct immutable byte values; every
  element must be an integer in `0..255`.
- Bytes expose byte length, integer indexing, copying slices, bytes-only `+`,
  sequence equality/inequality, and empty/non-empty truthiness. Ordering and
  mutation are rejected.
- `type(value)` reports `bytes` and `is_bytes(value)` provides the predicate.
- Text supports `encode("utf-8")` and bytes support `decode("utf-8")`. The
  codec name is exact and decoding rejects invalid starts/continuations,
  truncation, overlong forms, surrogates and values above `U+10FFFF`.
- Interpolation, direct/template/script output, `print`, string concatenation,
  shell interpolation, `nift eval`, text file/stream writes and value
  serialization reject bytes. `stringify`, `write_val` and `read_val` reject
  recursively nested bytes where values can be nested.
- Existing `cmd`/`run` rejection remains unchanged. Binary I/O, FFI bridges,
  transfer certification and public binding work remain deferred.

## Focused evidence

- `make -j2 test-cp17-bytes`: pass. Runs a C++ strict UTF-8 validator test and
  shell coverage for construction, operations, truthiness, introspection,
  conversion and all CP17 language-facing rejection boundaries.
- `make -j2 test-runtime-value`: pass.
- `make -j2 test`: pass.
- `make -j2 test-sanitize`: pass; the resulting ASan/UBSan binary also passes
  `tests/cp17_bytes.sh`.
- `git diff --check`: pass.

## Defect review

The independent review found and the implementation repaired three boundary
gaps: nested bytes in shell interpolation and legacy collection-directive
output could reach a throwing renderer, and priority queues admitted values
containing bytes. Focused regressions now require controlled rejection for all
three cases before output or collection mutation.

## Deferred by scope

CP17 adds no binary file or stream APIs, no bytes/FFI conversion, no C ABI or
maintained-binding bytes surface, and no CP18 transfer certification. CP18 is
the next checkpoint.
