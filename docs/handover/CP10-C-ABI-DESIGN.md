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
