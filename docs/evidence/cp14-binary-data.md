# CP14 generic binary-data evidence

Date: 2026-09-29

This checkpoint is a design/evidence spike. It changes no runtime, language or
HTTP behavior. `tests/cp14_binary_evidence.py` is the reproducible Linux probe.

## Current representations

| Surface | Arbitrary bytes today | Semantics and gap |
|---|---|---|
| String storage | `json::Document` stores `std::string`, so NUL and invalid UTF-8 fit physically. | Semantically text. `length()`/`split("")` parse UTF-8, while `substr()`/search use byte offsets. No indexing exists. |
| String equality/order/map key | Complete `std::string` equality, code-unit ordering and hashing preserve bytes. | Values beginning with `\x1fnift:` enter opaque-handle dispatch or slow paths instead of ordinary string behavior. |
| Copy/function/thread | Ordinary strings copy by value and transfer through functions/threads. | Marker-looking byte strings can clone/resolve resources or be rejected as non-transferable. |
| JSON | Escaped NUL round-trips. Strict parsing rejects invalid UTF-8. | Dump escapes controls but emits invalid high bytes unchanged, producing invalid JSON. JSON has no binary type. |
| `open()` | Binary whole-file read into a string. | Unbounded whole-file allocation and text/marker semantics afterward. |
| `ifstream`/`ofstream` | Binary incremental reads/writes preserve bytes. | Reads return strings; output construction always truncates; no typed byte result. |
| Managed `file()` | Byte offsets and binary save preserve data. | Entire file is held in `saved` and `working` strings; text `read_line()` normalizes CRLF; normal use is about 2x file storage. |
| Filesystem `copy()` | Filesystem-level byte-exact copy. | No application value is produced. |
| POSIX `run()`/pipelines | Capture and redirection preserve bytes. | Capture is unbounded and returned as a string. Windows capture removes CR from every CRLF pair. |
| FFI buffer | Parser-owned contiguous mutable `vector<unsigned char>`. | Opaque marker handle, no indexing/size/file API, non-transferable, shared by `deepcopy`, no explicit release, length is passed separately to native code. |
| `ffi_bytes()` | Copies a buffer into an integer array. | One generic `Document` per byte; no direct binary writer and very high memory cost. |
| Public C++ `Value` | `Value(std::string)` physically preserves bytes. | Only JSON-shaped types exist; `Value(const char*)` truncates at NUL and `json()` can emit invalid UTF-8. |
| C ABI | Pointer-plus-length string views physically support NUL. | Contract calls them UTF-8; there is no bytes type or typed non-JSON result channel. |

Relevant implementation points:

- `json::Document` has only null, bool, number, string, array and object storage:
  `jsonic/include/json.h:23-59`.
- Public `Value` exposes the same closed set: `include/nift/value.h:60-102`.
- UTF-8-looking string length/split and byte-based search/substr coexist at
  `src/Parser.cpp:2591-2632`.
- Strict JSON parsing is at `jsonic/include/json.h:489-578`; non-validating dump
  is at `jsonic/include/json.h:655-675`.
- Runtime resource IDs are in-band strings throughout `src/Parser.cpp`; broad
  marker handling begins in string methods at `src/Parser.cpp:2591` and transfer
  checks at `src/Parser.cpp:1985-2001` and `2117-2136`.
- FFI buffer construction/extraction is at `src/Parser.cpp:2175-2179`; native
  pointer exposure without an associated length is at `src/Parser.cpp:2204`.
- Binary file/stream surfaces are at `src/Parser.cpp:2233`, `2372`, `2570-2587`
  and `2999-3005`; POSIX/Windows process capture is in `src/Process.cpp`.

## Reproducible probe

Command:

```text
python3 tests/cp14_binary_evidence.py /home/nick/Repositories/nift/nift/nift
```

Retained Linux observations:

- A repeated all-octet 1 MiB fixture round-tripped byte-exactly through
  `open()`/`ofstream`, 16 explicit 64 KiB `ifstream` reads, managed-file
  `read_all()`, filesystem copy and a language thread.
- `A FF B` failed `.length()` with `split: invalid UTF-8`, while byte-oriented
  `.substr(0, 3)` preserved it exactly.
- `A NUL B` serialized as valid escaped JSON and round-tripped. `A FF B` dumped
  with raw `FF` and was rejected by Python's strict JSON decoder.
- `\x1fnift:file:not-a-handle` loaded from a file failed ordinary stream write as
  “value is not directly renderable,” proving user-byte/resource-marker collision.
- POSIX `run()` capture preserved the all-octet 1 MiB fixture exactly.
- Mutating `deepcopy(ffi_buffer([1,2,3]))` through FFI also mutated the original,
  proving shared resource identity rather than byte-value copy semantics.
- Passing an FFI buffer to `thread()` failed as a non-transferable resource.

Single-run `/usr/bin/time` probes are architectural scale indicators, not
competitive benchmarks:

| Probe | Elapsed | Peak RSS | Baseline-adjusted RSS |
|---|---:|---:|---:|
| Nift baseline | 0.00 s | 6,220 KiB | - |
| `open()` then `ofstream.write()` 16 MiB | 0.12 s | 54,820 KiB | 48,600 KiB |
| Managed-file `read_all()` then stream write 16 MiB | 0.11 s | 71,260 KiB | 65,040 KiB |
| 1 MiB string -> FFI buffer -> integer array | 0.26 s | 221,412 KiB | 215,192 KiB |

The FFI-array result is enough to reject generic integer arrays as the canonical
binary representation: a 1 MiB payload required about 210 MiB above baseline.
The measurements do not establish release thresholds or cross-platform costs.

## Candidate comparison

| Candidate | Advantages | Disqualifying or material costs |
|---|---|---|
| First-class immutable `bytes` | Clear text/binary split; compact contiguous storage; byte length/index/slice; stable equality; safe value copy and thread transfer; natural file, network, crypto and BLOB value. | Requires a real runtime tag, public `Value`/C ABI policy and explicit non-JSON behavior. |
| General mutable byte buffer | Efficient building and writable native calls. | Identity, copy, aliasing, pinning, resize, lifetime and cross-thread synchronization substantially enlarge v1. |
| `bytes` plus a new general `byte_buffer` | Clean immutable/mutable split. | Two new general types are not yet justified because the existing FFI buffer already covers the demonstrated mutable native-resource case. |
| Existing integer array | No new syntax/type and JSON-compatible. | Heterogeneous, non-contiguous, mutable to non-bytes, O(n) validation/conversion and approximately 200x RSS in the retained 1 MiB probe. |
| Enhanced FFI buffer only | Small implementation change and native mutation remains zero-copy. | Parser-local opaque resource; unsafe as an ordinary value, non-transferable, aliasing copies, no JSON/public `Value` representation. Does not solve files/net/WebSocket/SQLite values. |
| Binary mode on string | Reuses compact storage and current I/O. | Preserves mixed units, UTF-8 failures, invalid JSON, text-operation leakage and marker collisions. A mode bit is effectively a second tagged type with less clarity. |

## Gate 6 decision

**A - ADD A GENERIC BYTES VALUE.**

Add one immutable/value-oriented binary type. Do not add a second general
mutable buffer in v1. Keep the existing FFI buffer as an explicitly mutable,
parser-local native resource and bridge it with copies.

This is generic infrastructure rather than HTTP accommodation: the same value
is required by dynamic HTTP/WebSocket payloads, TCP/binary protocols, digest and
key material, compression/archive packages, SQLite BLOBs, FFI snapshots and
ordinary binary file I/O.

## Minimal v1 contract

Required now:

- A real `bytes` runtime tag, never an in-band string prefix.
- `bytes()` and checked `bytes([0, 1, ..., 255])` construction.
- Immutable byte length, integer indexing (`0..255`), byte-offset slice,
  concatenation and byte-sequence equality.
- Explicit strict UTF-8 boundaries: `text.encode("utf-8")` and
  `data.decode("utf-8")`; invalid decode fails without replacement.
- Additive binary I/O: `open_bytes()`, input-stream `read_bytes()` /
  `read_all_bytes()`, and output/managed-file writes accepting bytes. Existing
  string-returning APIs remain unchanged initially.
- Immutable copy/deepcopy/function/thread/async transfer semantics. Shared
  immutable backing storage may make copies O(1); v1 slices may copy.
- Explicit JSON/Nift-value serialization failure. No implicit base64, hex,
  tagged object or text reinterpretation.
- Text-only rendering also fails explicitly: interpolation, `print`, direct
  script/template output and string concatenation require a deliberate UTF-8
  decode. Binary stream/file writes are the only raw-byte output path in v1.
- Additive FFI bridges: bytes may copy into `ffi_buffer`; a distinctly named
  snapshot operation returns bytes. Existing `ffi_bytes()` array behavior must
  remain for compatibility.
- `Value::Type::Bytes`, length-aware C++ construction/access,
  `nift_context_set_bytes`, and top-level script-result byte data/size accessors.
  A borrowed result-byte view remains valid until its script result is freed.
  Existing UTF-8 string APIs remain text APIs. The JSON result accessor fails
  deterministically for a top-level or nested bytes value; nested typed C ABI
  traversal remains deferred with the generic typed-result channel.

Useful later:

- Hex/base64 codecs, byte-pattern search/split, map/set byte keys, efficient
  builders, constant-time comparison where a crypto package needs it, and
  process-capture APIs returning bytes.
- A typed embed-result channel that avoids JSON for every value category.
- Zero-copy read-only FFI arguments and package/native views with scoped
  ownership.

Explicitly deferred:

- A general mutable `byte_buffer`, shared mutation, cross-thread writable
  buffers, mmap, zero-copy slices/views, native pointer retention, implicit
  string/bytes coercion, automatic JSON encoding and changing existing file or
  process return types.

## Compatibility and ownership

The change should be additive. Existing strings, `open()`, stream reads,
managed-file reads, process results, FFI buffers and `ffi_bytes()` retain their
current behavior. Packages can migrate deliberately to binary APIs.

Immutable bytes use shared immutable contiguous backing storage so normal
assignment, function calls, `copy()` and thread snapshots do not necessarily
copy large payloads. Concatenation and v1 slices may allocate. This avoids the
ownership/lifetime complexity of views while not requiring every value pass to
copy. Mutable FFI conversion remains an explicit O(n) copy in each direction.
Native pointer retention is unsupported and no post-call pointer lifetime is
guaranteed.

## Consumer fit

| Consumer | Effect of minimal bytes |
|---|---|
| HTTP | `http.stream(write)` can accept text or bytes without changing its public transport model; FIFO paths remain private. Existing file responses remain preferable for completed files. |
| WebSockets / `net` | Binary frames and TCP reads gain a natural payload distinct from UTF-8 messages. No socket package is approved here. |
| OpenSSL/crypto | Digests, random data, keys and signatures stop pretending to be text or integer arrays. No OpenSSL package is approved here. |
| FFI | Immutable snapshots cross package/thread boundaries; explicit copies bridge to the existing mutable native buffer. |
| SQLite | Future BLOB parameters/results gain a backend-neutral value rather than file paths or integer arrays. |
| File I/O | Additive byte reads/writes remove mixed text semantics without breaking existing scripts. |
| Compression/archive | Compressed blocks and archive entries gain compact arbitrary-byte values. |

## Runtime impact and risk

The current evaluator uses `json::Document` as both JSON DOM and universal
runtime value. A genuine bytes tag therefore affects:

- Jsonic/Nift value storage or a new runtime wrapper;
- parser type classification, equality, operators, methods, copy/deepcopy,
  function transfer and thread/async snapshots;
- rendering and serialization rejection paths;
- public `Value`, Engine/Context host callables, C ABI and language bindings;
- file/stream methods and FFI conversion boundaries;
- test fixtures for nested bytes in arrays/objects and error propagation.

Adding a non-JSON type directly to Jsonic would blur its strict JSON role and is
high risk; another marker string is unacceptable. The implementation checkpoint
must first choose between a Nift runtime wrapper and a carefully isolated
non-JSON document variant whose parser/dumper/schema behavior remains strict.
This is a moderate-to-high regression surface despite the deliberately small
language API.

## Proposed implementation sequence

Only after explicit Gate 6 acceptance:

1. **Representation checkpoint:** introduce the real internal tag plus public
   `Value`/C ABI shape, strict serialization rejection, copy/equality/type and
   nested-value tests. No file, FFI or HTTP dependency yet.
2. **Language/I/O checkpoint:** add construction, byte operations, UTF-8
   encode/decode and additive file/stream APIs; certify all-octet behavior,
   ownership and bounded errors.
3. **Interop checkpoint:** add explicit FFI snapshot/copy bridges and binding
   exposure; run cross-platform and performance walls.
4. **Review Gate 7:** assess semantics, compatibility, memory/copy cost and host
   parity. HTTP must not depend on bytes before this gate.

No HTTP, WebSocket, net, OpenSSL, process-handle, cancellation, future, await,
callback or compiled-helper work is approved by CP14.
