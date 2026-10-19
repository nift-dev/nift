# CP17 — Typed handles (`src/rt/handle.*`, `src/rt/handles/`)

Status: core implemented on `runtime-2-cp17-dev` (base `runtime-2` @6181549b),
ahead of CP16 by owner decision (2026-10-19), so that CP16's Host layer builds
on the real handle representation. Not reviewed or certified. Not linked into
`nift` yet: nothing in the VM or legacy engine calls it.

## Objective
Checkpoint row: "CP17 typed handles end-to-end; registry reclamation (removes
D3); no `\x1fnift:` strings in VM paths; handle-forgery tests fail closed."

This phase delivers the handle table that CP16 needs (D-029: "Process handles
use CP17's typed-handle design"; CP16 design §2.3 and §11): typed,
generation-checked handles owned by execution contexts (D-029/D-030), with
per-kind cleanup hooks, bounded slot reuse, a documented concurrency contract,
and interfaces for the Host layer and natives. The VM-side parts (handle
values that count references, compiler rule W13) are specified below as
integration work, not implemented here (the VM files belong to other agents).

## Scope
Owned and changed: `src/rt/handle.{h,cpp}` (CP09 registry, extended),
`src/rt/handles/*` (new), `tests/rt/handle_*` (new),
`tests/runtime2/cp17/*` (new), this record, and new Makefile test targets.
Not touched: `src/rt/compiler*`, `src/rt/vm*`, `src/rt/natives/*`,
`src/rt/value.h` (a patch is supplied), legacy sources.

## Design

### Files

| File | Concern |
|---|---|
| `src/rt/handle.h/.cpp` | CP09 `HandleRegistry` (unchanged behaviour, plus `check()` with a specific reason) and the shared status vocabulary: `HandleStatus`, `handle_status_code`, `handle_status_text`, `handle_error_class` |
| `src/rt/handles/types.h/.cpp` | contexts, release reasons, `KindSpec` + `CleanupHook`, `CleanupFailure`, `CloseReport`, options, stats |
| `src/rt/handles/slot_store.h/.cpp` | slots, identities, generations, reuse and retirement, intrusive per-owner lists (no locking) |
| `src/rt/handles/context_tree.h/.cpp` | the execution-context tree, access rule, deterministic close order (no locking) |
| `src/rt/handles/table.h/.cpp` | `HandleTable`: the locked, thread-safe facade; runs cleanup hooks |
| `src/rt/handles/handle_ref.h` | `HandleRef`: RAII counted reference (retain/drop) |
| `src/rt/handles/native_access.h/.cpp` | native-side interface over `rt::Value` (no `RuntimeValue`) |

`HandleRegistry` (CP09) stays as the minimal single-threaded, owner-less
building block for its existing tests. Host resources use `HandleTable`.

### Identity

A handle is the CP09 immediate `Value` `(HandleKind, 64-bit id, 32-bit
generation)`; there is still no string encoding (D3 stays removed).

* `id = (table tag << 32) | slot index`. The tag is a process-wide counter,
  never 0, different for every `HandleTable`, so a handle from another table
  (another run, isolate or test) is `Foreign`, never a slot of this one.
* Generations start at **1** (0 is never issued) and are bumped on every
  release. A slot therefore never re-issues a generation: all handles ever
  issued for it are below its current generation once released (`Stale`), and
  anything at or above it that is not the live handle was never issued
  (`Forged`). Generations are per slot, across kinds: a stale `File` handle
  can never resolve to a `Process` that later reuses the slot.
* **Generation wrap**: a slot whose generation is `UINT32_MAX` is retired on
  release and never reused (CP09 rule kept), so generations never wrap.
  Retired slots count against the capacity (reachable only after 2^32
  create/release cycles of one slot).
* Classification order, each a specific code: not a handle → kind ≠ expected →
  tag ≠ table → slot index unknown or generation 0 → (live slot) older
  generation `Stale`, newer or slot kind ≠ handle kind `Forged` → (free slot)
  older `Stale`, else `Forged` → (retired slot) `Stale`.

Kinds are a byte: any `HandleKind` but `None` can be registered, including
values beyond the named enumerators (tested with kind 200).
`tests/runtime2/cp17/patches/value-h-stream-command.patch` adds the named
`Stream` and `Command` kinds CP16 §2.3 asks for (appended, existing numbers
unchanged); `value.h` is not owned by CP17, so the patch is applied by the
CP16 integration.

### Identity vs ownership (owner requirement, 2026-10-19)

A handle **identifies** a resource; it never owns it.

| Concept | What it is | Who holds it |
|---|---|---|
| identity | the handle `Value` (tag, slot, generation, kind) | any number of copies anywhere: registers, arrays, objects, other threads |
| reference | a counted claim that keeps a *counted* resource from being abandoned (`HandleRef`, or `retain`/`drop`) | the VM's handle boxes (see VM integration), host code |
| ownership | the single responsibility for ending the resource | exactly one owner per live resource: one execution context, or `kDetached` |
| cleanup policy | the kind's hook, run **exactly once** per resource, with one `ReleaseReason` | fixed at registration of the kind |

Rules, each enforced in the table and tested:

1. **One owner.** A slot has one `owner` field; it is set by `create()` and
   changed only by `detach()` or `transfer()`.
2. **Explicit, single-step transfer.** `detach(h, kind, expected_owner)` and
   `transfer(h, kind, expected_owner, new_owner)` are one locked step each and
   name the owner they expect (compare-and-swap). Two racing transfers cannot
   both succeed; the loser gets `NotOwned`. Detaching an already-detached
   handle is `NotOwned`; adopting a detached resource is `TransferForbidden`.
3. **No double cleanup.** A resource leaves its slot exactly once (the slot is
   freed and its generation bumped under the lock), by whichever of
   `release`, last `drop`, `close_context` or `shutdown` gets there first;
   only that caller runs the hook. Every later operation through any copy of
   the handle is `Stale`.
4. **No premature cleanup.** References never end a resource while another
   counted reference lives: the last `drop` abandons it, and an explicit
   `release` of a counted handle with more than one reference is `InUse`
   (the owner's own reference is the one consumed). Uncounted handles are
   ended only by their owner or their owner's end; after that every copy is
   `Stale`, which is a defined, loud state (never another resource).
5. **Owner-only release.** Only the owner may `release`; nested contexts may
   *use* an ancestor's handle (resolve) but not end it.
6. **Owner end wins.** When the owning context ends, its resources end,
   whatever references remain (D-030: managed resources belong to their
   execution context); remaining references hold a stale identity.

### Ownership and lifetime (D-029/D-030)

Contexts: `Script`, `Build`, `BuildHook`, `PageRender`, `AutoRebuild`, opened
with a parent (or as roots). Context ids are never reused (a stale id is
`ContextClosed`, an id never issued `UnknownContext`).

* **Access**: a handle resolves for its owner and for contexts nested in the
  owner (a page render may use its build's handle); never for siblings,
  parents or unrelated contexts (`NotOwned`). Detached handles resolve for
  any context. Host-internal code may pass `kAnyAccessor`.
* **Build family**: `Build`, `BuildHook`, `PageRender`, `AutoRebuild` and
  every context nested in one forbid `detach` (`DetachForbidden`, D-030:
  detachment is forbidden in builds including hooks and `--auto` rebuilds) and
  `transfer` to a context outside the build family (`TransferForbidden`), so
  no build can leave a resource behind. `context_in_build(id, in_build)`
  answers the same question up front, so a native can refuse a detached
  launch before starting anything (CP16 request).
* **Context end** (`close_context`): the context and all its descendants are
  marked closing at once (no new handles, children, detaches or transfers in
  them), then cleaned in a fixed order: descendants before ancestors, later
  opened siblings first; within a context the newest handle first (LIFO, like
  destructors, so a newer resource built on an older one is cleaned while the
  older one still resolves). One hook call per handle, reason `ContextEnd`.
  A hook that fails (returns a failure, throws `std::exception`, throws
  anything else) never stops the cleanup of the others; the slot is reclaimed
  anyway; the failure is reported in the `CloseReport` in cleanup order,
  after any earlier failures of abandoned handles of that context.
* `close_context` returns only when no hook for a handle of the closed
  contexts is still running, including hooks started concurrently by other
  threads (`release`, last `drop`). A superseded `--auto` pass therefore
  closes its `AutoRebuild` context and knows every hook has finished before
  the next pass starts.
* **Shutdown** (`shutdown()`, also run by the destructor): closes every open
  root (newest first), then releases detached handles (reason `Shutdown`; the
  process hook does nothing to a detached process, CP16 §11.5). Idempotent.
  Hook state must outlive the table, or `shutdown()` must be called first
  (found by ASan in the first test run).

### Errors (D-020, D-022, D-025)

| Status | Code | Class | Meaning |
|---|---|---|---|
| `NotAHandle` | `internal.handle_not_a_handle` | Internal | the value is not a handle (e.g. a legacy `"\x1fnift:file:0"` string) |
| `KindMismatch` | `internal.handle_kind_mismatch` | Internal | handle of another kind |
| `Foreign` | `internal.handle_foreign` | Internal | issued by another table |
| `Forged` | `internal.handle_forged` | Internal | never issued |
| `Stale` | `internal.handle_stale` | Internal | issued once, released since |
| `TypeMismatch` | `internal.handle_type_mismatch` | Internal | `resolve_as<T>` with the wrong `T` |
| `NotOwned` | `internal.handle_not_owned` | Internal | caller is not the required owner / not allowed to access |
| `InUse` | `internal.handle_in_use` | Internal | release of a counted handle with other references |
| `NotCounted` | `internal.handle_not_counted` | Internal | retain/drop on an uncounted handle |
| `ContextClosed` | `internal.handle_context_closed` | Internal | context ended or ending |
| `UnknownContext` | `internal.handle_unknown_context` | Internal | context never issued |
| `UnknownKind` / `KindRegistered` | `internal.handle_unknown_kind` / `internal.handle_kind_registered` | Internal | registration errors |
| `NullResource` | `internal.handle_null_resource` | Internal | `create(nullptr)` |
| `DetachForbidden` | `runtime.detach_in_build` (per-kind override: CP16 registers `process.detach_in_build`) | Error | detach inside a build (D-030) |
| `TransferForbidden` | `runtime.transfer_out_of_build` | Error | transfer out of the build family, or adoption of a detached resource |
| `LimitExceeded` | `limit.exceeded` | Limit | table capacity reached |
| `CleanupFailed` | `internal.handle_cleanup_failed` (or the hook's own code) | Internal | a cleanup hook failed; `internal.handle_cleanup_threw` when it threw |

Internal errors are fail-closed: nothing resolves, nothing is released, the
output parameter is unchanged. CP16 §2.3/§11.5 already fixed that a failed
resolution is `internal.*` (exit 70), because the compiler admits handle
values only where they cannot be forged (W13).

### Capacity (D-020, D-028)

`TableOptions::capacity` (default 2^20 slots) bounds live + free + retired
slots. Free slots are reused before the table grows (LIFO free list), so the
slot count is the live high-water mark (200,000-step churn with ≤ 64 live
handles: 64 slots). Creating past the capacity is `LimitExceeded`
(`limit.exceeded`); the refused resource is destroyed (outside the lock) and
no hook runs.

### Concurrency contract

Consistent with how `async` functions and `thread()` workers work today
(feature-host-deps: workers record into their own state and the render merges
it when it ends, after waiting for the workers):

* One `HandleTable` per Host run (CP16 `HostHandles`), shared by the run's
  contexts and by every worker thread of those contexts. Contexts are not
  thread-affine: a worker uses its enclosing context's id.
* Every operation is linearizable under one mutex. Cleanup hooks run without
  it, so hooks may call back into the table (resolve older handles, create in
  other open contexts, release or drop other handles). A hook must not close
  the context that owns the handle it is cleaning (that close waits for it).
* `resolve` returns a `shared_ptr` copy: a resource stays alive while a
  native uses it even if another thread ends it meanwhile. Hooks must
  therefore be safe against concurrent users of the resource (CP16's
  `ProcessGroup` cancellation is designed to be).
* Kind registration is write-once; hooks are read without the lock after a
  handle of the kind exists.
* Context end in a render or build happens after its workers settled (as the
  host-deps merge already requires). If a worker still runs, its creates in
  the closing context fail with `ContextClosed` (the resource is destroyed,
  never leaked) and its releases race safely (exactly once).
* Values are 16-byte immediates and refcounts in CP09 are per isolate; handle
  values cross isolates by copy like any immediate, which is safe because the
  table, not the value, holds the resource.

### Interface for CP16 (Host layer)

```cpp
HandleTable table;                                   // one per Host run (HostHandles)
KindSpec spec{"process", hook, "process.detach_in_build"};
table.register_typed_kind<ProcessResource>(HandleKind::Process, spec);
ContextId script; table.open_context(ContextKind::Script, kDetached, script);
Value h; table.create(script, HandleKind::Process, group, h, CreateOptions{/*counted*/ true});
std::shared_ptr<ProcessResource> p; table.resolve_as(h, HandleKind::Process, accessor, p);
bool in_build; table.context_in_build(script, in_build);  // refuse a detached launch early
table.detach(h, HandleKind::Process, script);        // explicit; refused in builds
CloseReport r = table.close_context(script);         // hooks with ContextEnd, failures listed
```

The hook receives `CleanupEvent{kind, handle, owner, reason, resource}` and
returns `CleanupOutcome::success()` or `failure(code, message)`. For
processes, CP16's hook should *start* the §11.4 sequence (cooperative signal,
hand the group to the reaper) and return, and CP16's context-end path then
waits once for every group of the context against one deadline; running the
whole grace period inside each hook would serialise 2 s per process.
`ReleaseReason` lets the hook distinguish §11.3 (context end: warning if
still running), §11.5 (abandoned: asynchronous termination and its warning),
explicit release and shutdown (detached: nothing). Warnings are CP16's.

### Interface for natives

`native_access.h`, over `rt::Value` only: CP16 adds `handles::HandleAccess
handles;` (table + current context) to `natives::Context`.
`native_handle_arg<T>(access, builtin, arg, kind, out, error)` resolves an
argument; `native_handle_result(access, builtin, kind, resource, out, error)`
creates a handle owned by the native's context. Error text is
`<builtin>: <reason> (<kind> handle <slot>/<generation>, <code>)`. Status
mapping: class Internal → `Status::Internal`, Limit → CP16's `Status::Limit`,
Error → `Status::Error`.

## Integration work (hooks and patches for other owners)

**CP16 (Host layer)**
1. Apply `tests/runtime2/cp17/patches/value-h-stream-command.patch` (adds
   `HandleKind::Stream`, `HandleKind::Command`).
2. `HostHandles` holds one `HandleTable`; register File, Stream, Command,
   Timer and Process kinds with typed resources and hooks (§2.3 mapping).
   Legacy never releases file/cmd/timer instances during a run, so those are
   created uncounted and end at context end; the end-of-run "managed file left
   open" check runs before `close_context`.
3. Open/close contexts at the D-030 boundaries (script run, page render, build
   hook, build, each `--auto` pass; superseded pass: close before the next).
4. Add `src/rt/handles/*.cpp` to `RT_LINK_SOURCES` in the Makefile when the
   first native uses the table (left out here so `nift` is unchanged).
5. Register the new error codes with CP13's code table (`internal.handle_*`,
   `runtime.detach_in_build`, `runtime.transfer_out_of_build`).

**VM (compiler/vm owners)**
1. W13 stays the forgery barrier: handle values come only from natives;
   literals and conversions never produce one; a failed resolve is exit 70.
2. Abandonment (CP16 §11.5 "CP17 phase") needs references the VM counts.
   `Value` handles are trivially-copied immediates, so the VM cannot count them
   today. Proposed patch (owner decision O1): counted kinds (started
   `Process`, possibly `Stream`) are created with `CreateOptions::counted` and
   wrapped in a refcounted heap box holding a `HandleRef`; the box's
   destruction (last VM reference) drops the reference, and the table abandons
   the resource. Uncounted kinds keep the immediate encoding. Until then CP16's
   "pre-CP17" semantics apply: abandoned means "still owned at context end".
3. Map `HandleErrorClass` to the VM stop reasons as above.

## Tests

| Test | What it covers |
|---|---|
| `tests/rt/handle_table_unit.cpp` | status codes (all distinct), CP09 `check()`, registration, identity layout, forgery matrix (string, int, null, kind, foreign tag, unknown slot, generation 0 / future / max, rewritten kind byte on a live slot), stale after release and after reuse by another kind, generation wrap and retirement, churn reclamation (200,000 steps, capacity 64, every live handle resolves to its own resource and no old one resolves), capacity limit (`limit.exceeded`, refused resource destroyed) |
| `tests/rt/handle_table_ownership.cpp` | access rules, identity vs ownership (copies never own; owner-only release; detach/transfer as single CAS steps; build-family bans; per-kind detach code), context close ordering across a build with pages and a hook, LIFO with an older handle resolvable inside a newer one's hook, create inside a closing context refused, failing hooks (failure / `std::exception` / non-exception) reported in order with the rest still cleaned, explicit-release failure, closed and unknown contexts, destructor shutdown, counted references (`InUse`, abandonment exactly once, `HandleRef` copy/move/share, failures of abandoned handles in the owner's report or in `shutdown()`), native interface |
| `tests/rt/handle_table_stress.cpp` | 4 workers + a context manager over one table: random create (counted and not), resolve (checks the resolved resource is the one the handle identified), release, retain/drop, detach, transfer while contexts open and close; then a capacity-48 run that hits the limit under contention; a race of `release`, three `drop`s and `close_context` on one counted handle (exactly one cleanup); `close_context` blocked until a hook another thread started for its handle has finished. Checks every created resource is cleaned exactly once and none outlives shutdown |
| `tests/rt/handle_table_bench.cpp` + `tests/runtime2/cp17/handle_guards.py` | callgrind instruction guards (+10 %), marginal Ir per iteration |
| `tests/runtime2/cp17/handle_mutants.py` | 15 mutants, each removing one rule; the unit suite must fail for each |
| `tests/runtime2/cp17/no_handle_strings.py` | gate: no `\x1fnift` handle string (any escape spelling) in code under `src/rt` (comments allowed); self-test |

Make targets: `test-rt-handles` (unit, stress, gate, lint),
`test-rt-handles-sanitize` (ASan+UBSan), `test-rt-handles-tsan`,
`test-rt-handles-guards` (callgrind; run under `heavy.sh`).
Runs are under `ulimit -f` and `timeout` (D-031); `ulimit -v` too where no
sanitizer runs.

### Results (2-vCPU shared host, GCC 13.3)

Base `runtime-2` @6181549b. Heavy runs went through `heavy.sh` with `nice make -j1`.

| Run | Result |
|---|---|
| `make test-rt-handles` (unit, stress 2 s + 500 races, gate, lint) | unit 223 checks / 0 failures; stress 511 checks / 0 failures; gate 0 problems (self-test 8/8); rt-lint 0 problems |
| `make test-rt-handles-sanitize` (ASan + UBSan, `-fno-sanitize-recover=all`) | unit 223 / 0; stress 313 / 0 (281,657 handles created and cleaned at capacity 512; 50,538 at capacity 48 with the limit hit under contention); no reports |
| `make test-rt-handles-tsan` | stress 313 / 0, no TSan reports (66,851 + 13,087 handles) |
| TSan mutant: `resolve` without the lock | TSan data race reported, exit 66 (the TSan run can detect a missing lock) |
| stress mutant: `close_context` not waiting for in-flight hooks | 2 failures (caught) |
| `tests/runtime2/cp17/handle_mutants.py` | 15 mutants, 15 killed |
| `make test-rt-handles-guards` (callgrind, marginal Ir per iteration) | create+resolve+release 1,256; resolve 250; counted create+retain+drop+drop 1,368; open context + 4 creates + close 5,055. Recorded in `tests/runtime2/cp17/handle_guards.json` and checked at +10 %. The baseline was written and checked in the same run, so it is a lock against regressions, not an independent target. |
| `make test-rt-value` (CP09 suites, `handle.cpp` changed) | unit 82, regress 277, round 2 28, property 1,867,405 checks; 0 failures |
| GCC 13 `-Wall -Wextra -pedantic`, clang `-fsyntax-only` | no warnings |

The one defect found was in the tests: ASan reported a use-after-free when
hook state was destroyed before the table. The tests now declare that state
first, and the contract is documented under Shutdown.

No wall-clock benchmark was run. D-032 quick benchmarks need a dedicated
Linode, and this table is not on any build path yet.

## Open questions (owner decisions)

* **O1 — VM reference counting for abandonment.** Box counted handle kinds
  in a refcounted heap object holding a `HandleRef` (proposed), or keep
  "abandoned = still owned at context end" permanently. Affects
  CP16 §11.5's `abandon_running_cp17` test.
* **O2 — Transfer between contexts.** The owner's requirement names "moving
  to another context"; CP16 §11.2 says ownership never moves except by
  detach. The table provides `transfer` (single step, never out of a build,
  never adoption of a detached resource) for host use, e.g. promoting a
  resource to the parent context. Should it ever be reachable from scripts?
* **O3 — Error codes.** `internal.handle_*` family; `runtime.detach_in_build`
  as the generic code with CP16's `process.detach_in_build` as the process
  kind's code; `runtime.transfer_out_of_build`. Confirm or rename.
* **O4 — Capacity.** Default 2^20 handles per run, not configurable. D-028
  asks limit errors to name a setting; is a setting (e.g. `NIFT_HANDLE_LIMIT`)
  wanted, or is the message "handle table capacity exceeded" enough?
* **O5 — Cleanup failures and exit status.** The table reports hook failures
  (e.g. a process that could not be killed) loudly; D-030 fixes only that the
  *warning* for terminating a still-running process leaves the exit status
  unchanged. Should a *failed* cleanup change the exit status?
* **O6 — Access by nested contexts.** Nested contexts may resolve their
  ancestors' handles (a page render using its build's handle); parents and
  siblings may not use a child's. Confirm.
