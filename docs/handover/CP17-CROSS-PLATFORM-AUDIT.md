# CP17 cross-platform binding behaviour audit

Status: 2026-08-26.

The C ABI is the cross-platform boundary: the canonical C++ Embed is exercised
on Linux, macOS and Windows by the Checkpoint-10 workflow (3-OS behavioural
corpus + comparison). The production bindings (C#, Node, Python) are thin
adapters over that frozen ABI and are built+tested on Linux (the binding-gates
CI job and local). This audit records the platform-sensitive surface and the
residual items scheduled for the release/packaging phase.

## Loader-key separator normalization (fixed in CP17)

The engine reports loader keys and dependencies with forward slashes
(`path.lexically_normal().generic_string()`) on every platform, while a
Windows corpus root is expressed with backslashes. All five harness adapters
(c-abi, go, cs, js, py) previously trimmed only the root's trailing
separators without normalizing both sides, so a Windows root would never
match a forward-slash key prefix and loaderKeys would stay absolute. Each
adapter now normalizes `\` -> `/` on both the root prefix and every key before
stripping. Covered by Go unit tests (Windows-style root) and direct checks of
the other four; the shared corpus remains 36/36 on Linux.

## Native-library resolution (documented; packaged at release)

- C#: `DllImport("libnift_c")` with a `DllImportResolver` honouring
  `NIFT_NATIVE_LIB`; per-OS library name (libnift_c.so/.dylib/nift_c.dll) is
  a packaging concern.
- Node: `build.sh` locates N-API headers via `NIFT_NODE_INCLUDE` then the
  distro include dirs; the `.node` extension suffix differs per OS.
- Python: `build.sh` uses `python3-config`; the extension suffix
  (`_nift<EXT_SUFFIX>.so`/`.pyd`) differs per OS.

## Callback exception/panic boundaries (verified across bindings)

- Go: host callbacks recover panics (TestPanicContainedInCallback).
- Node: thrown callbacks contribute their message as the exact diagnostic
  (exception-containment test).
- Python: raised callbacks contribute their message (exception-containment
  test).
- C#: FIXED in CP17 - user delegates could previously throw across the native
  callback boundary (undefined behaviour in C++); they are now contained and
  surfaced as a controlled host Error whose diagnostic is the exception
  message (new test, suite 20 -> 21).

## Residual (release/packaging phase, not CP17 blockers)

Building and exercising the C#/Node/Python bindings on Windows and macOS
native runners (per-OS library naming, header discovery, extension suffix)
remains for the packaging/release stage; the underlying C ABI they consume is
already 3-OS verified.

## FFI disposal-lifetime audit (Go + C#, CP17 round 2)

The CP17 round-1 Go callback-buffer reclamation used only an atomic render
counter, which does not create an epoch boundary: a new render could start
between "the last old render observed zero" and "its buffers were freed",
reintroducing the cross-thread callback-output UAF CP11 rules out. Review
rejected that design. The repaired invariants:

### Go (nift.go)

- `Engine.lifecycle` (sync.Mutex) couples render ADMISSION with quiescent
  RECLAMATION: beginRender holds it while incrementing renderCount; the
  render-done closure holds it across the decrement AND the buffer free (or
  deferred destruction), so no new render can be admitted between the zero
  transition and reclamation. Lock order is always lifecycle -> callbackSet.mu;
  callbacks take only callbackSet.mu, so there is no inversion.
- `Engine.Close()`/`Context.Close()` are logical: they mark the object closed
  (new operations rejected via alive()/beginRender) and DEFER native
  destruction until the last in-flight render quiesces (the render-done
  closure performs it). An in-flight render on another goroutine can never use
  freed engine/context/token/buffer state.
- Deterministic tests: close-engine and close-context during a render use a
  loader-callback rendezvous (render provably in native execution before
  close, released after); the reclamation-admission race is forced with a
  test-only hook that blocks inside the critical section while a new render
  attempts admission (it must block until reclamation completes). All under
  `go test -race`.

### C# (Nift.csproj)

- The C# binding had no deferred destruction: Engine/Context Dispose freed the
  native SafeHandle/GCHandle immediately, so a concurrent Dispose during an
  in-flight render could free native state. Enforced the same invariant:
  render methods EnterRender/ExitRender around the native call; Dispose marks
  disposed immediately (new operations throw ObjectDisposedException) and
  defers native destruction until the last in-flight render quiesces. The
  try/finally uses entered-flags so an EnterRender rejection never decrements
  a count it did not increment. Deterministic dispose-during-render tests via
  a loader-callback rendezvous. C# suite 21 -> 23.

## Non-render native-operation lifecycle (CP17 round 3)

Round 2 enforced admission/lifetime only around renders; non-render native
operations still had a check-then-free-then-native-call window (alive() then
the native call, with Close able to destroy between). Now every public
native-touching method is a lifecycle admission:

- Go: IsOpen, OpenError, SetRoot, Reload, SetLoader, SetEnvironmentProvider
  and every binding setter hold `Engine.lifecycle` (or `Context.lifecycle`)
  across the native call and reject a closed engine inside that critical
  section, so Close can never free the engine/context mid-call. The same
  test-only hook (nativeOpTestHook) forces the "operation admitted, Close
  concurrently blocked" window deterministically; tests cover an engine
  setter, a context setter and a query racing Close.
- C#: the same pattern via Engine/Context `Guarded(...)`, which holds
  `_lifecycle` across the native call and throws ObjectDisposedException if
  disposed. A test-only NativeOpTestHook forces the window; tests cover an
  engine setter, a query (IsOpen) and a context setter racing Dispose.

The invariant now holds at the native-handle boundary generally:

```text
operation admitted before Close -> native resource stays alive until it returns
Close wins admission -> operation rejected before native use
```

## Provider-setter lifecycle (CP17 round 4)

Round 3 left SetLoader / SetEnvironmentProvider loading the callback registry
and reading e.id OUTSIDE the lifecycle mutex: after Close (e.id==0, registry
entry deleted) they could nil-dereference before reaching the closed check, and
the e.id read raced Close's write. Both methods now begin with the lifecycle
admission (lock, closed check, test hook, e.id read, registry lookup with an
explicit `!ok` guard, callback-state mutation, native install) as one protected
operation. Regression tests: SetLoader/SetEnvironmentProvider after Close do
not panic, and a provider install races Close deterministically (admitted under
lifecycle, Close blocked until the install completes). A final audit of every
e.engine / e.id / callbackRegistry / c.ctx access confirms each is
lifecycle-gated, render-count protected, or construction/destruction-only.
Provider replacement remains covered by the documented
configuration-before-concurrent-use rule (not newly concurrent-safe).
