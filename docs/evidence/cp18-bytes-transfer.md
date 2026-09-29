# CP18 bytes value transfer

Date: 2026-09-29

Status: **complete candidate** at the CP18 implementation tree.

## Contract certified

- Public `Value` copy construction and assignment create independent value
  shells while retaining the same immutable non-empty byte backing.
- Language assignment, `copy`/`deepcopy`, prepared direct returns, legacy
  aggregate returns, escaped closure captures and nested aggregate extraction
  preserve exact byte contents and backing identity.
- `mutex` initial values and `get()` snapshots, thread arguments/results, async
  function and async-lambda arguments/results preserve the backing. Parallel
  worker reads are safe and leave contents unchanged.
- Deep-copied object/array shells remain independently mutable: replacing or
  rebinding members in a copy does not alter the source aggregate, while byte
  members that were not replaced continue to share immutable storage.
- Engine bindings retain bytes after all host-side source values and fan-out
  copies are rebound or destroyed.
- CP17 remains authoritative at boundaries: direct and nested transferred bytes
  are still rejected by text rendering and strict JSON/value serialization.

## Deterministic sharing and amplification evidence

`tests/cp18_bytes.cpp` creates one patterned 8 MiB public byte value, copy
constructs 256 values, copy-assigns another and nests copies in independently
mutable objects. Every copy must expose the exact 8 MiB pattern and the same
non-null `Value::bytes().data()` pointer. A registered host callback repeats the
pointer and complete-content check after every language, callable, closure,
mutex, thread and async transfer, including concurrent worker reads.

This is a portable deterministic guard against byte-backing copy amplification:
duplicating the backing would necessarily produce a different live allocation
and pointer. RSS was deliberately not used because allocator, sanitizer and
platform noise would make a memory threshold weaker and less portable than the
identity proof.

## Focused evidence

- `make -j2 test-cp18-bytes`: pass. Runs the linked public Engine/pointer fixture
  and `tests/cp18_bytes.sh` language transfer/rejection coverage.
- `make -j2 test-engine-bindings`: pass.
- `make -j2 test`: pass, including the registered CP18 target.
- `make -j2 test-cp18-bytes-sanitize`: pass under ASan/UBSan for both fixtures.
- `make -j2 test-cp18-bytes-tsan`: pass under TSan for both fixtures; TSan was
  available on the certification host and reported no races.
- `git diff --check`: pass.

## Defect review

No production defect was exposed. Existing `RuntimeValue` shell copies retain
their `shared_ptr<const RuntimeBytes>` through public conversion, recursive
aggregate cloning, parser scopes, mutex state and worker snapshots. CP18 adds
only durable certification, Make integration and campaign evidence.

## Deferred by scope

CP18 adds no binary file/stream I/O, mutable-buffer or FFI bridge, C ABI or
maintained-binding byte surface. It does not enter CP19-CP22 or produce the
Review Gate 6B report. Review Gate 6B is the next checkpoint.
