# CP14 — Node/JavaScript binding design

Status: 2026-08-26, sixth shared-corpus adapter.

## Architecture

```text
idiomatic Node/JavaScript API   (bindings/node/lib/nift.js)
        ↓
N-API native addon              (bindings/node/native/nift_node.cc)
        ↓
frozen Nift C ABI               (include/nift/c_abi.h)
        ↓
canonical C++ Nift Embed
```

No Nift semantics are reimplemented in JavaScript. The addon is a thin N-API
adapter over the C ABI; the JS wrapper is an idiomatic `Engine` / `Context`
surface over the addon. The addon statically links the C ABI's PIC objects, so
the `.node` is self-contained.

## Threading model — the deliberate answer

Nift host callbacks (loader / environment provider) are **synchronous from the
C++ caller's perspective** and can fire from **C++ pagination worker threads**,
while JavaScript normally runs on the single Node event-loop thread. A
synchronous render called from the JS thread would deadlock: the JS thread
would block inside the native render while worker-thread callbacks wait for the
JS thread to service them.

The binding therefore makes renders **asynchronous by design**:

```text
engine.render(...)            JS thread
        |  returns a Promise
        v
napi_async_work               libuv worker thread
        |  runs the C ABI render (synchronous C++)
        |    ├─ loader/env callback fires (render thread or pagination worker)
        |    │     napi_call_threadsafe_function(blocking)
        |    │        └─ queued onto the JS event loop
        |    │             call_js_cb runs the user JS callback on the JS thread
        |    │             result (value / NotFound / Error) signalled back via
        |    │             a condition variable
        |    └─ result built; complete_cb resolves the Promise on the JS thread
        v
JS receives RenderResult
```

Key properties:

- The JS event loop stays free during renders, so threadsafe-function callbacks
  are always serviceable. No deadlock.
- The user's loader/env callback runs on the JS thread and MUST return
  synchronously (a string = Found, null/undefined = NotFound, throw = Error
  whose `.message` is the diagnostic).
- Concurrent renders are supported: each runs on its own libuv worker; each
  callback carries its own request, so results route correctly even when
  multiple renders interleave on the JS thread.

### Callback `out` buffer lifetime

The C ABI copies the borrowed `out` synchronously immediately after the
callback returns (`c_abi.cpp callback_result`). Each native render thread owns a
per-thread scratch `CallbackScratch` reused only after the previous same-thread
use is provably complete (same-thread sequentiality + synchronous copy) and
freed at thread exit. This is bounded (per thread) and is NOT the cross-thread
free-on-next-callback pattern that CP11 ruled out.

### Resource / lifetime rules (enforced, tested)

Renders are async, and an Engine/Context **cannot** be freed while a render is
in flight. This is an enforced binding invariant, not a caller obligation:

```text
render begins
    ↓
Engine/Context native lifetime retained (render_count incremented)
    ↓
close() called
    ↓
new operations rejected immediately (disposed)
but native resources NOT freed while renders use them
    ↓
render settles
    ↓
last in-flight reference released (render_count -> 0)
    ↓
native resource destroyed safely
```

- `BeginRender` stores the Engine/Context **wraps** and increments a per-wrapper
  `render_count`; `RenderComplete` decrements it. `close()` sets `disposed`
  immediately (new operations throw "Engine/Context has been disposed") and
  destroys the native resource immediately if no render is in flight, otherwise
  defers destruction to the last `RenderComplete`. All of this happens on the
  JS thread, so no locking is required.
- The TSFNs stay alive while any render is in flight, so in-flight
  loader/environment callbacks remain serviceable even after `close()`.
- `napi_ref`s root the JS objects for the render duration (GC cannot collect
  an Engine/Context with an outstanding render); the GC finalizer destroys the
  native resource only when `render_count == 0` and is otherwise idempotent.
- `close()` is idempotent. The HTTP dogfood's original await-before-close
  mistake is no longer a hazard; the dogfood keeps awaiting simply because the
  result is a Promise.
- Shutdown while callbacks are quiescent is safe.

## Shared semantic surface

Engine defaults / Context bindings / context-over-engine precedence; string,
int, number, bool, JSON values; composed / source ({path}|{text}) / partial /
page-pagination renders; dependencies; requirements; loader + environment
providers; Found / NotFound / Error(diagnostic) with exact host diagnostics;
the malformed-JSON `error_prefix` semantic family (parser wording flows through
unmodified); invalid binding/setup failures ("invalid binding name: <name>");
ABI compatibility via the frozen C ABI.

## Building

`bash build.sh` locates Node's N-API headers (`NIFT_NODE_INCLUDE`, then
`/usr/include/node`, `/usr/local/include/node`, or the tarball include dir),
builds the C ABI PIC objects (`make libnift_c.so`) and links the addon.

## Testing

- `test/nift.test.js` — focused tests: bindings, precedence, invalid bindings,
  malformed-JSON family, loader/env Found/NotFound/Error, page/pagination,
  partial, 64-way concurrent renders with callbacks, pagination callbacks from
  C++ worker threads, callbacks surviving GC, repeated create/dispose, GC
  pressure, disposed-object rejection, quiescent shutdown, exception
  containment, long-lived engine.
- `test/embed-harness.js` — the shared-corpus adapter (sixth); the corpus runs
  36/36 across C++, nift-rs, C ABI, Go, C# and Node plus the negative
  anti-agreement self-test.
- `app/server.js` + `app/smoke.sh` — real HTTP dogfood: long-lived Engine,
  repeated + concurrent requests (24/24 external + 32 in-request), request
  Context, engine defaults + precedence, loader seam, environment callback,
  Error(diagnostic) -> 500 with verbatim diagnostic, malformed-JSON family,
  pagination, graceful disposal.
