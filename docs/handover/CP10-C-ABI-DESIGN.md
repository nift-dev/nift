# CP10 — Nift Embed C ABI design

Status: design review (implementation follows once this surface is challenged).

Goal: a small, stable, ownership-explicit C ABI representing the frozen Nift
Embed contract, suitable as the long-lived foundation for Go/Node/Python/C#
production bindings. It is a translation boundary, not a C++ convenience API.

## Non-goals (never exposed)

`build`, `.unfinished`, tracked persistence, `.info.json`, `repair`, hashes,
`watch`, filesystem output mutation, `init`/deployment. Those belong to the Nift
CLI/build orchestrator (regression-suite layer 1) and are deliberately absent.

## 1. Opaque handles

```c
typedef struct nift_engine nift_engine;            /* open standalone or project-aware */
typedef struct nift_context nift_context;          /* per-render request state */
typedef struct nift_render_result nift_render_result; /* owning render outcome */
typedef struct nift_source nift_source;            /* caller-owned input (text or path) */
```

No STL type, exception, reference, `std::string`, `std::vector`, Jsonic type, or
C++ class layout crosses the ABI. The header compiles as C (`extern "C"` guards;
`tests/c_abi_smoke.c` proves a pure-C consumer builds).

## 2. Ownership / lifetime table

| Object | Allocates | Owns | Valid until | Freed by | Caller retains | Invalidated by next call | Thread-safe |
|---|---|---|---|---|---|---|---|
| `nift_engine*` | library | caller | `nift_engine_free` | `nift_engine_free` | yes | no | render: yes; mutation: no |
| `nift_context*` | library | caller | `nift_context_free` | `nift_context_free` | yes | no | no (per-thread) |
| `nift_render_result*` | library | caller | `nift_render_result_free` | `nift_render_result_free` | yes | no | yes (own storage) |
| `nift_string` from result | library (borrowed) | owning result | result freed | none (copy to retain) | copy only | no | yes (immutable) |
| `nift_string` from engine diagnostic | library (borrowed) | engine | next diagnostic call or engine free | none | copy only | yes | no |
| `nift_source*` | caller | caller | call duration | caller | n/a (input) | n/a | input only |
| callback `nift_string` output | callback | callback | callback return | callback | n/a (copied by engine) | n/a | callbacks may run on render threads |

Every returned string is a borrowed `{data,length}` view; callers copy to
retain. The library never hands out pointers that must be passed back to
`free()`. Result objects are independently owned and outlive any engine call.

## 3. Strings

- UTF-8. `nift_string { const char* data; size_t length; }`.
- Pointer + length is the contract; NUL termination is not assumed for inputs
  and not guaranteed for outputs. Embedded NULs are carried by length.
- Input semantics: `data == NULL && length == 0` is the empty string; `data ==
  NULL && length > 0` is `NIFT_ERROR_INVALID_ARGUMENT`.

## 4. Status / error model

```c
typedef enum {
    NIFT_OK = 0,
    NIFT_ERROR_INVALID_ARGUMENT, /* null handle, bad length, invalid binding name, malformed JSON argument */
    NIFT_ERROR_PROJECT,          /* project open/reload failed; diagnostic in nift_string out */
    NIFT_ERROR_NOT_FOUND,        /* callback supplied no value (loader miss / env unset) */
    NIFT_ERROR_CALLBACK,         /* loader/env callback returned an error status */
    NIFT_ERROR_INTERNAL          /* an unexpected C++ exception was contained */
} nift_status;
```

These categories map to real mechanical failure modes of the translation layer.
The C++ `RenderError` has no kind enum, so page-level render semantics are NOT
invented here: every render function returns `NIFT_OK` when the call was
mechanically valid, and the outcome is carried by the returned
`nift_render_result` (`ok` + diagnostic message/source/line/column) exactly like
the frozen C++ contract. Every exported function contains C++ exceptions
(`try/catch(...)` -> `NIFT_ERROR_INTERNAL`) so no exception crosses the ABI.

## 5. Context / bindings

Context carries per-render page identity, current output (for `@pathto`),
title, and string/int/bool/JSON bindings. Engine carries long-lived defaults
and bindings. `set_json` accepts UTF-8 JSON text (pointer+length); malformed
JSON is `NIFT_ERROR_INVALID_ARGUMENT`. No Jsonic type is exposed — a host
supplies JSON text or primitives.

## 6. Render

```c
nift_engine_render_page(engine, ctx, page_name, len, &result)   /* project-aware; complete pagination */
nift_engine_render(engine, page, page_template, ctx, &result)   /* page+template composition */
nift_engine_render_partial(engine, partial, ctx, &result)       /* fragment render */
```

`nift_source` is `{kind: text|path, data, length, logical_name?, logical_name_length?}`;
a path source is resolved against the engine root.

## 7. Result surface

- `output()` = page 1 (borrowed view).
- `pagination()` = pages 2..N ascending, each with explicit 1-based page number
  and rendered output: `pagination_count` + `pagination_get(index, &page, &out)`.
- `dependencies()` / `requirements()` = counts + per-index borrowed views.
- `ok()` + error message/source/line/column.

Filenames/paths are NOT render semantics and are not exposed.

## 8. Host callbacks

```c
typedef nift_status (*nift_loader_callback)(void* user_data, const char* path,
                                            size_t path_len, nift_string* out);
typedef nift_status (*nift_environment_callback)(void* user_data, const char* name,
                                                 size_t name_len, nift_string* out);
```

- `user_data` is passed through unchanged; callback `out` is borrowed from
  callback-owned storage and copied by the engine before the callback returns.
- `NIFT_OK` with a value = found; `NIFT_ERROR_NOT_FOUND` = source unset/miss
  (maps to the C++ `nullopt`); any other status = `NIFT_ERROR_CALLBACK` and the
  render fails with a controlled error.
- Callbacks may be invoked concurrently by render threads (the C++ contract),
  so they must be thread-safe.
- Callbacks must not re-enter Nift (no nested render/set on the same engine).

## 9. Thread-safety model (mirrors the frozen Engine contract)

- Concurrent `nift_engine_render*` on one engine: safe.
- Engine mutation (`set*`, `set_loader`, `set_environment_provider`,
  `set_root`, `open`): NOT safe concurrently with renders. Configure before
  serving.
- `reload` safe concurrently with render (snapshot publication); diagnostic
  accessors (`open_error`, reload error string) must not race with each other —
  they borrow engine-scratch storage.
- Context is per-thread; do not share a context across threads or mutate it
  during a render that uses it.
- Results are independently owned and safe to read from any thread.

The complete-pagination-under-concurrent-reload guarantee is unchanged: the C
ABI sits above the same immutable snapshot publication.

## 10. Versioning

- Header constant `NIFT_ABI_VERSION "1.0"` + `nift_abi_version()` returning the
  same string; `nift_abi_version_major()/minor()`.
- Opaque handles keep the surface extensible: structs are never public, so a
  future ABI can add functions without breaking callers. New capabilities are
  additive function additions; incompatible changes bump the ABI version.
- Symbol names are `nift_*` (no mangling). The shared library exports only the
  ABI symbols.

## 11. Deliberate simplifications

- No `nift_value` construction API in the initial ABI: bindings are primitives
  + JSON text, which is enough for every shared-corpus case and keeps the
  surface small. A structured value API can be added additively if a binding's
  ergonomics justify it.
- Result strings are borrowed, not copy-on-get: bindings copy what they keep.
  This avoids an allocator contract across the boundary.

## 12. Adapter participation

`embed/adapters/c-abi` consumes `libnift_c.so` through its public C symbols
only (no C++ linkage) and implements the neutral JSON request -> JSON result
protocol, including the loader/env seams via the C callback API. The shared
runner then requires C++ == frozen expectation, nift-rs == frozen expectation,
and C ABI == frozen expectation for all 26 cases.

## Open questions to resolve before implementation

- Confirm the borrowed-string model is acceptable for the first production
  binding (vs a copy-out API). Decision: borrowed; bindings copy.
- Confirm `NIFT_ERROR_NOT_FOUND` doubles as "env unset" and "loader miss".
  Decision: yes, documented per callback.
- Confirm the status enum is mechanical-only (render semantics live in the
  result). Decision: yes — matches the frozen C++ contract.

## Implementation outcomes (2026-08-25)

Implemented `include/nift/c_abi.h` + `src/c_abi.cpp` (opaque handles completed
at global scope matching the header forward declarations), built as
`libnift_c.a` / `libnift_c.so` (PIC objects). Every exported function contains
exceptions (`try/catch -> NIFT_ERROR_INTERNAL`).

### Shared corpus participation

`embed/adapters/c-abi` consumes `libnift_c.so` through its public C symbols via
ctypes (a foreign consumer, exercising the callback boundary through the C
callback API) and implements the neutral JSON request -> JSON result protocol.
`embed/run-embed.py` now runs three adapters: **C++ API == frozen expectation,
nift-rs == frozen expectation, C ABI == frozen expectation** (plus
all-equal secondary invariant). Result: **26/26**, self-test passes.

### Adversarial / lifetime battery

`tests/c_abi_adversarial.cpp` (C++ consumer of the C header) covers null/invalid
arguments, empty/large/Unicode strings, malformed JSON, binding validation,
loader/env callbacks (user_data, NOT_FOUND, callback error -> NIFT_ERROR_CALLBACK),
result lifetime across subsequent renders / reload / engine destruction,
repeated create/destroy, concurrent renders, pagination/dependency/requirement
iteration bounds, and the ABI version. `tests/c_abi_smoke.c` is a pure-C
consumer proof. Both pass. Undefined misuse (double-free, freed-handle use) is
documented, not detected.

### Sanitizers

Targeted ASan/UBSan on the adversarial battery: clean, no leaks/UB. The ctypes
adapter path also runs clean under ASan (LD_PRELOAD) on a project-aware
pagination render.

### Overhead

Representative repeated render (binding + loop), 20k iterations, median of 7:
direct C++ 4438 ns/render vs C ABI 4551 ns/render = **+113 ns (+2.5%)** — a
thin translation layer.

## CP10.1 repairs (2026-08-25)

- **Callback-error attribution is per-render, concurrency-safe.** Removed the
  engine-global `pending_callback_status` slot (two concurrent renders could
  steal each other's callback error). Each ABI render now establishes a
  thread_local RAII `RenderCallbackScope`; loader/env callbacks write their
  hard-error status into the scope active on the thread that invoked them, and
  the render consumes only its own scope. Deterministic concurrent tests force
  render A's loader to hard-fail and render B's loader to succeed with a
  guaranteed overlap (B waits until A's loader ran); both completion orders and
  two-simultaneous-failures are covered. Loader callbacks run only on the
  calling thread; the C++ project render may invoke the ENVIRONMENT provider on
  its own pagination worker threads, which have no render scope, so a hard
  env-callback failure there degrades to "unset" (NIFT_ERROR_NOT_FOUND) - the
  C++ callback interface (`std::optional<std::string>`) has no error channel.
  This is documented; no cross-render state is shared.
- **Empty vs missing callback values are distinct.** `NIFT_OK` + length 0 is a
  valid empty value (empty source / variable set to ""); `NIFT_ERROR_NOT_FOUND`
  is absent; `NIFT_OK` + `data==NULL` + `length>0` is invalid callback output
  (`NIFT_ERROR_CALLBACK`). Regression cases cover empty loader source, missing
  loader source, empty env value, missing env value.
- **Integer width is exact.** `nift_engine_set_int` / `nift_context_set_int`
  now take `int32_t` (no silent long-long->int truncation); added
  `nift_engine_set_number` / `nift_context_set_number` (`double`) for the
  complete scalar number model. INT32_MIN/INT32_MAX round-trip exactly.
- **Source kind is validated.** `nift_source_kind` must be
  `NIFT_SOURCE_TEXT`/`NIFT_SOURCE_PATH`; any other value is
  `NIFT_ERROR_INVALID_ARGUMENT` (adversarial case added).

Re-verified after the repairs: C ABI adversarial battery (incl. deterministic
concurrent attribution) PASS 5/5, pure-C consumer PASS, targeted ASan/UBSan
clean, shared corpus **26/26** (C++ API / nift-rs / C ABI) + negative
self-test PASS, direct C++ ~4.1 us vs C ABI ~4.1 us (~+1%) overhead.

## CP10.2 — host callback failure is a first-class Embed contract (2026-08-25)

The CP10.1 worker-thread finding showed the C ABI could not reliably
manufacture a third callback state over the C++ provider seam (which only had
value / absent). The underlying Embed host-resource contract was therefore
upgraded to value / absent / **error** for both loader and environment
providers:

- `nift::HostResult` (public `include/nift/host_result.h`): `Found(value)` /
  `NotFound` / `Error(diagnostic)`.
- `Engine::set_loader` / `set_environment_provider` gain HostResult overloads;
  the `std::optional<std::string>` forms remain as convenience overloads
  (value -> Found, nullopt -> NotFound).
- `RenderHost::environment` returns `HostResult`; `RenderHost::read_shared_source`
  returns a `HostSource` with a status + error channel. A host Error travels
  through the render computation: the @getenv handler and every source read
  fail the render with the diagnostic, **including pagination worker threads**
  (the pagination page loop already fails the overall result when any page
  fails, so a worker host error fails the whole render - no silent "unset").
- The C ABI maps `NIFT_OK`/`NIFT_ERROR_NOT_FOUND`/other directly onto
  Found/NotFound/Error; **the thread_local callback-error side channel is
  removed entirely**. A host failure is a rendering outcome: the render calls
  return `NIFT_OK` (mechanically valid) and the `RenderResult` carries
  `ok=false` with the diagnostic, identically whether the callback ran on the
  caller thread or a pagination worker. Malformed callback output
  (`NIFT_OK` + NULL + positive length) is a controlled render error.

Tests: C++ `tests/host_seam.cpp` (standalone + paginated env failure, NotFound
= unset, concurrent blog-fails/other-succeeds attribution, both orders);
C ABI paginated env-failure tests; Rust `nr15_host_seam.rs` with the idiomatic
`HostResult` enum + `set_*_result` provider forms (Rust pagination is
sequential, so propagation is inherent). C++ conformance 9/9, CLI 25/25, Rust
218/218, NR6/NR12, embed corpus 26/26 (C++/Rust/C ABI), ASan/UBSan clean,
overhead ~+1.2%.

## CP10.3 — no hidden "Error means optional absence" exceptions (2026-08-25)

- C++ pagination separator: `HostSource` status is now checked — `NotFound` ->
  no separator, `Error` -> the render FAILS with the host diagnostic (Found
  empty is a valid empty separator). The old `.content`-without-status was a
  latent contract violation.
- Rust: the pagination separator no longer `.ok()`s away the error; a
  MissingSource read means "no separator", a host Error fails the render. The
  standalone/render_tracked read sites no longer rewrite non-MissingSource
  errors to "not readable", so host diagnostics survive (C++ parity).
- Audit: all C++ `read_shared_source` sites check status; Rust `read_source`
  sites propagate via `map_err`/`?` (only MissingSource gets the reference
  message); `environment` handles Error everywhere.
- Regression tests: C++ `tests/host_seam.cpp` (separator Found/NotFound/Error
  via a custom host; deterministic page-order error selection) and Rust
  `nr15_host_seam.rs` equivalents.
- Shared corpus: three new frozen host-error cases
  (`host-env-error-standalone`, `host-env-error-pagination`,
  `host-loader-error`) with `env-error`/`loader-error` adapter seams. The
  pagination-separator loader-error path is covered by the parser-level custom
  host tests: neither the C++ nor the Rust project render routes the pagination
  separator through the engine loader seam (both read the project's sources
  from the snapshot/filesystem), so it is not expressible through the neutral
  corpus. Corpus total 29/29 across C++ API / nift-rs / C ABI.

## CP10.4 — Rust pagination-template host-error preservation (2026-08-25)

The last error-rewriting survivor in Rust was the pagination-template read
(`map_err(|_| ...)`), which discarded every underlying error. It now preserves
non-`MissingSource` host errors (NotFound keeps the canonical
missing/unreadable pagination-template diagnostic), matching the C++ `HostSource`
status handling. C++/Rust parity tests probe the pagination template with
Error ("pagination template backend failed" survives) and NotFound (canonical
diagnostic). Final semantic audit: all 6 Rust `read_source` sites + `environment`
and all 7 C++ `read_shared_source` sites + `environment` preserve host errors;
the only intentional transformations are MissingSource/NotFound -> canonical
missing/optional diagnostics and boolean existence probes that let the
subsequent authoritative read surface the real error. Rust workspace 221/221.

## CP11 — first production binding: Go (2026-08-25)

`bindings/go` is a thin, idiomatic, ownership-safe cgo binding over the frozen
C ABI, living with nift-embed (one compatibility unit; not a separate repo).
It translates types, ownership and callbacks only — no Nift semantics are
reimplemented in Go. Covers engine/context/bindings, render sources
(text/path/mixed), RenderPage with complete pagination, dependencies/
requirements, loader/environment providers (Found/NotFound/Error), panic
containment at the exported cgo boundary, ABI-version compatibility, and full
Go-owned result conversion.

Go unit/lifetime/callback/concurrency tests pass under `go test -race`,
including `TestPaginationWorkerEnvCallback`: C++ pagination worker threads
invoke the Go environment provider and the failure/success crosses back
correctly (the critical CP10 boundary). The neutral-protocol harness
(`cmd/embed-harness`) plus `embed/adapters/go-embed` drive the shared corpus:
**29/29 with C++ API / nift-rs / C ABI / Go all equal to the frozen
expectations**, negative anti-agreement self-test passes. cgo render overhead
~1.8us/render over the direct C ABI (~5.9us vs ~4.1us). Documented limitation:
host-provider callback buffers are retained until engine Close (safe under
concurrent pagination-worker callbacks); a per-render pool is deferred to the
hardening campaign.

## CP11.1 — C ABI callback diagnostic transport (2026-08-25)

The frozen C ABI callback could express FOUND/NOT_FOUND/ERROR but not
ERROR(diagnostic): `callback_result()` ignored `out` on a hard status and
returned the generic "host callback failed". The semantic completion (no
signature/enum/struct/symbol change; no ABI version bump) reuses the existing
`nift_string* out` as the failure diagnostic channel:

```text
NIFT_OK              out = value (length 0 = valid empty)
NIFT_ERROR_NOT_FOUND out ignored (absent/unset)
hard callback status out = diagnostic (non-empty)
                     out empty -> "host callback failed"
```

The Go binding now places `HostResult.Error` into `out` on `HostError`, so
`Error("host exploded")` reaches the failed RenderResult exactly. The shared
corpus host-error cases (env standalone, env pagination, loader standalone)
now expect the specific supplied diagnostic ("host exploded" / "getenv: host
exploded"), 29/29 across C++ / nift-rs / C ABI / Go. Exact-preservation tests
added in the C ABI adversarial battery, the pure-C consumer, and the Go binding
(including the C++ pagination-worker -> C -> Go callback path under -race).
