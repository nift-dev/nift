# CP15 — Python binding design

Status: 2026-08-26, seventh shared-corpus adapter (final planned initial
production binding).

## Architecture

```text
idiomatic Python API      (bindings/python/nift/__init__.py)
        ↓
CPython C extension       (bindings/python/src/nift_module.cc)
        ↓
frozen Nift C ABI         (include/nift/c_abi.h)
        ↓
canonical C++ Nift Embed
```

No Nift semantics are reimplemented in Python. The C extension is a thin
adapter over the C ABI; the Python `Engine` / `Context` / `RenderResult`
classes are the idiomatic surface over the extension. The extension statically
links the C ABI's PIC objects, so it is self-contained.

## GIL / threading model — the deliberate answer

Nift host callbacks (loader / environment provider) are **synchronous from the
C++ caller's perspective** and can fire from **C++ pagination worker threads**,
while Python callbacks require the GIL. A render that held the GIL during the
C++ call would deadlock: pagination workers would block on the GIL held by the
calling thread.

The binding therefore renders **synchronously with the GIL released**:

```text
python render() call
        |
        v
Py_BEGIN_ALLOW_THREADS (GIL released)
        |
        v
C ABI render runs (synchronous C++, this thread + C++ pagination workers)
        |
        +---> loader/env callback fires on a render thread
        |        (this thread or a C++ pagination worker)
        |        |
        |        v
        |     PyGILState_Ensure() re-acquires the GIL
        |        |
        |        v
        |     Python callback runs (str/bytes/None/raise)
        |     result -> nift_status + borrowed nift_string; GIL released
        |
        v
Py_END_ALLOW_THREADS (GIL re-acquired); result object built
```

Key properties:

- The calling thread releases the GIL during the render, so callbacks from any
  render/pagination thread can always acquire it. No deadlock.
- User callbacks run under the GIL and MUST return synchronously: str/bytes =
  Found, None = NotFound, raise = Error whose message is the diagnostic.
- Renders remain synchronous from the caller's perspective (idiomatic Python).
- Concurrent renders from multiple Python threads each release the GIL during
  their C++ call; callbacks serialize on the GIL.

### Callback `out` buffer lifetime

The C ABI copies the borrowed `out` synchronously immediately after the
callback returns (`c_abi.cpp callback_result`). Each native render thread owns
a `thread_local std::string` scratch reused only after the previous same-thread
use is provably complete (same-thread sequentiality + synchronous copy) and
freed at thread exit. This is NOT the cross-thread free-on-next-callback
pattern CP11 ruled out.

## Lifetime model (enforced, same invariant as the Node binding)

- A synchronous render holds a strong reference (Py_INCREF) to the Engine and
  Context for its duration, so the objects can never be deallocated mid-render.
- `close()` marks an object disposed immediately (new operations raise
  "has been disposed") and destroys the native resource immediately when no
  render is in flight, otherwise defers destruction until the last in-flight
  render completes (render_count -> 0). All state transitions happen under the
  GIL, so no additional locking is needed.
- The loader/environment callbacks keep the Engine alive through the render's
  strong reference; the C ABI's user_data points at the Engine object, which is
  never freed while a render can call back.

## Shared semantic surface

Engine defaults / Context bindings / context-over-engine precedence; string,
int, number, bool, JSON values; composed / source ({path}|{text} str/bytes/
tuple/dict) / partial / page-pagination renders; dependencies; requirements;
loader + environment providers; Found / NotFound / Error(diagnostic) with exact
host diagnostics; the malformed-JSON `error_prefix` semantic family; invalid
binding/setup failures ("invalid binding name: <name>"); ABI compatibility via
the frozen C ABI.

## Building

`bash build.sh` locates Python headers via `python3-config` (override with
`PYTHON=`), builds the C ABI PIC objects (`make libnift_c.so`) and links the
extension to `nift/_nift<EXT_SUFFIX>.so`.

## Testing

- `tests/test_nift.py` — 21 focused tests: bindings, precedence, invalid
  bindings, malformed-JSON family, loader/env Found/NotFound/Error, page/
  pagination, partial, path sources, 64-way concurrent renders with callbacks,
  pagination callbacks from C++ worker threads, disposed-use rejection,
  exception containment, long-lived engine, repeated create/dispose.
  The close-during-render lifetime contract tests are DETERMINISTIC: a
  loader/environment callback acts as a rendezvous (it fires only after the
  render has entered native execution and incremented render_count), so the
  main thread provably closes the Engine/Context while an in-flight native
  render uses it, then releases the callback and asserts the render settles
  correctly. The concurrent case uses an explicit latch (all N renders signal
  in-flight before close). Repeated-close and GC-pressure remain stress
  coverage.
- `tests/embed_harness.py` — the shared-corpus adapter (seventh); the corpus
  runs 36/36 across C++, nift-rs, C ABI, Go, C#, Node and Python plus the
  negative anti-agreement self-test.
- `app/app.py` + `app/smoke.sh` — real WSGI dogfood (stdlib `wsgiref`):
  long-lived Engine, repeated + concurrent requests (24/24 external + 32
  in-request threads), request Context, engine defaults + precedence, loader
  seam, environment callback, Error(diagnostic) -> 500 verbatim, malformed-JSON
  family, pagination, graceful disposal.
