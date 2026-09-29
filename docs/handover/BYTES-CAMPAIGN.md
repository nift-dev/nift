# Generic bytes implementation campaign

Status: approved 2026-09-29. Review Gate 6 selected A: add a genuine immutable
first-class `bytes` value. This document is the authoritative checkpoint
sequence. Earlier generic-bytes planning summaries with different CP18-CP23
numbering are superseded and must not be used for commit names or gate reports;
unrelated historical campaigns are unaffected.

CP15 implementation status: complete candidate on 2026-09-29. The evaluator,
AST, Engine defaults/results and public C++ `Value` internals now use the
Nift-owned recursive `RuntimeValue`; strict JSON, schema and persisted project
data remain Jsonic documents with explicit recursive boundary conversion. No
bytes type or API was added. Evidence is in `docs/evidence/cp15-runtime-value.md`.
Review Gate 6A closed with a pass on 2026-09-29 after Gate 6A-R replaced the
pre-existing generic function-pointer dispatcher with exact-token vendored
libffi dispatch. Hosted Linux GCC/Clang sanitizer, macOS Clang, Windows MinGW,
packaging, exact staged-payload audits and integrity walls passed at `7f6fc58`;
the defect-focused review found no remaining blocker. Evidence is in
`docs/evidence/gate6ar-libffi.md`.

CP16 implementation status: complete candidate on 2026-09-29. Runtime values
now have a genuine bytes tag backed by shared immutable contiguous storage;
truthiness, sequence equality, nesting and fingerprints preserve bytes identity.
Checked JSON conversion rejects top-level or nested bytes atomically, text and
value serialization reject bytes, and public C++ exposes `Value::Type::Bytes`
and `Value::Bytes`. No language construction/operations, binary I/O, FFI bridge,
C ABI or maintained-binding bytes API was added. Evidence is in
`docs/evidence/cp16-bytes-storage.md`.

CP17 implementation status: complete candidate on 2026-09-29. Checked language
construction, immutable byte operations, strict UTF-8 conversion, introspection
and controlled language-facing text/value serialization rejection are in place.
Evidence is in `docs/evidence/cp17-bytes-language.md`.

CP18 implementation status: complete candidate on 2026-09-29. Assignment,
public and language copy/deepcopy, prepared and legacy callable paths, escaped
closures, nested aggregates, mutex values, threads and async values retain
shared immutable byte backing with independent aggregate shells. Concurrent
reads, source lifetime, large-value fan-out and transferred CP17 text/JSON
rejection are certified without production changes. Evidence is in
`docs/evidence/cp18-bytes-transfer.md`.

Review Gate 6B result: **pass** on 2026-09-29. Generic bytes value semantics,
prepared/legacy evaluator parity, text and serialization boundaries, transfer,
shared-backing amplification behavior, sanitizers and native Linux/macOS/Windows
execution are certified. Evidence is in `docs/evidence/gate6b-bytes.md`. This
cleared CP19 to begin.

CP19 implementation status: complete candidate on 2026-09-29. `open_bytes`,
stream and managed-file byte reads, and exact raw byte writes are additive;
existing text I/O remains unchanged. Checked read failures, managed-file cursor
semantics and atomic replacement on Windows are covered. Evidence is in
`docs/evidence/cp19-bytes-io.md`. CP20 is next.

Strict JSON remains a separate ingress/egress boundary. The implementation must
introduce a Nift-owned runtime value model for null, bool, number, string, bytes,
array and object rather than adding a non-JSON type to Jsonic, using marker
strings or relying on an out-of-enum JSON value.

## Authoritative checkpoints

| Checkpoint | Scope | Commit subject |
|---|---|---|
| CP15 | Migrate evaluator/Engine/public-Value internals to a Nift `RuntimeValue` substrate without exposing bytes or changing behavior. | `cp15: separate runtime values from json` |
| Review Gate 6A | Certify Jsonic isolation, ownership, recursive values and compatibility of the representation migration. | `review: record bytes gate 6a` |
| CP16 | Add immutable shared bytes storage, runtime identity, equality, nesting, low-level checked JSON/text-render rejection and public C++ `Value::Type::Bytes`/`Value::Bytes`. | `cp16: add immutable bytes storage` |
| CP17 | Add checked construction, byte operations, strict UTF-8 conversion, introspection and enforce rejection at every language-facing direct-text/value-serialization call site. | `cp17: add bytes language semantics` |
| CP18 | Certify assignment, copy/deepcopy, function/closure, nested, mutex, thread and async transfer with shared immutable backing. | `cp18: certify bytes value transfer` |
| Review Gate 6B | Certify generic value semantics, evaluator parity, text boundaries, transfer and large-value memory behavior before I/O. | `review: record bytes gate 6b` |
| CP19 | Add `open_bytes`, stream/managed-file byte reads and exact raw byte writes without changing existing text APIs. | `cp19: add binary file and stream io` |
| CP20 | Add explicit copying bridges between immutable bytes and mutable FFI buffers while preserving `ffi_bytes()`. | `cp20: bridge bytes and ffi buffers` |
| CP21 | Add `nift_engine_set_bytes`, `nift_context_set_bytes`, result-owned C ABI byte views, lazy checked JSON extraction and embedding lifetime/error contracts. | `cp21: expose bytes through embedding` |
| Review Gate 6C | Freeze public C++/C ABI names, versioning, pointer rules, ownership, JSON errors and copied headers. | `review: record bytes gate 6c` |
| CP22 | Expose top-level bytes through maintained Python, Node, Go and C# bindings; nested typed traversal remains deferred. | `cp22: expose bytes in maintained bindings` |
| CP23 | Complete documentation, conformance, compatibility, sanitizer, platform and performance certification. | `cp23: certify generic bytes support` |
| Review Gate 7 | Record final certification and proof-consumer fit. Stop before any package consumes bytes. | `review: record bytes gate seven` |

## Fixed v1 contract

- Bytes use a real runtime tag and shared immutable contiguous storage.
- Assignment, `copy`, `deepcopy`, function calls, threads and async values may
  share immutable backing while preserving value semantics.
- Empty bytes are false; non-empty bytes are true.
- Equality compares complete byte sequences. Bytes are unequal to strings and
  integer arrays. Relational ordering is unsupported.
- Bytes may be values nested in arrays/objects, but are not v1 map/set keys or
  members of ordered collections.
- Construction from integers is checked to `0..255`. Length, integer indexing,
  byte slicing and bytes-only concatenation are supported.
- UTF-8 encode/decode is explicit and strict. No implicit string/bytes coercion,
  replacement decoding, base64 or hex convention exists.
- Direct text rendering, interpolation, `print`, string concatenation, command
  arguments, templates, `stringify`, `write_val` and `read_val` serialization
  reject bytes. Strict JSON rejects top-level and nested bytes deterministically.
- Existing text-returning file, stream and process APIs remain unchanged. New
  binary reads are additive; raw `write(bytes)` is exact.
- Mutable FFI buffers remain parser-owned, aliasing and non-transferable.
  Bytes/buffer conversion is explicit and copies; `ffi_bytes()` remains an
  integer-array compatibility API. Native pointer retention remains unsupported.
- Public C++ uses `Value::Type::Bytes` with `Value::Bytes` as the byte-container
  alias. C ABI input includes `nift_engine_set_bytes` and
  `nift_context_set_bytes`; result byte views are result-owned and top-level
  only. Nested bytes are valid runtime values but typed nested-result traversal
  is deferred.
- Mutable byte buffers, mmap, zero-copy slices/views, pinning, retained pointers,
  process API redesign and all proof-consumer integration remain deferred.

## Checkpoint discipline

Each checkpoint contains only its scoped implementation, focused tests, durable
Make registration, relevant historical walls, a defect-focused review, repairs,
handover/evidence updates and one independent commit. Review Gate 6A follows
CP15, Gate 6B follows CP18, and Gate 6C follows CP21. Clean gates do not require
a manual pause.

Gate 7 is the hard stop. HTTP, WebSockets, `net`, OpenSSL/crypto, SQLite and all
other proof consumers remain unchanged until a later explicit decision.

## Genuine blockers

Stop before making a new architectural decision if implementation would require
changing accepted ordinary string/JSON/file/stream/process/FFI semantics,
extending Jsonic with bytes, using a marker/sentinel representation, weakening
immutability or transfer safety, changing existing text APIs, exposing immutable
storage to native mutation, broadening the approved C ABI into recursive typed
traversal, redesigning a Gate-6C-frozen API, accepting a required binding or
cross-platform semantic failure, exceeding the certified non-bytes regression
wall, or modifying a proof consumer before Gate 7.
