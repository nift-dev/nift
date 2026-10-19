# CP17 — Typed handles (`src/rt/handle.*`, `src/rt/handles/`)

Status: core implemented on `runtime-2-cp17-dev` (base `runtime-2` @6181549b),
ahead of CP16 by owner decision (2026-10-19), so that CP16's Host layer builds
on the real handle representation. D-035 (owner decisions O1–O6 and their
refinements) is implemented. Not reviewed or certified. Not linked into
`nift` yet: nothing in the VM or legacy engine calls it.

## Objective
Checkpoint row: "CP17 typed handles end-to-end; registry reclamation (removes
D3); no `\x1fnift:` strings in VM paths; handle-forgery tests fail closed."

This phase delivers the handle table that CP16 needs (D-029: "Process handles
use CP17's typed-handle design"; CP16 design §2.3 and §11): typed,
generation-checked handles owned by execution contexts (D-029/D-030), the
authoritative resource lifecycle with an asynchronous completion API (D-035),
bounded slot reuse, a documented concurrency contract, and interfaces for the
Host layer and natives. The VM-side parts (boxed counted handles, compiler
rule W13) are supplied as a tested patch and integration notes; the VM files
belong to other agents.

## Scope
Owned and changed: `src/rt/handle.{h,cpp}` (CP09 registry, extended),
`src/rt/handles/*` (new), `tests/rt/handle_*` (new),
`tests/runtime2/cp17/*` (new), this record, and new Makefile test targets.
Not touched: `src/rt/compiler*`, `src/rt/vm*`, `src/rt/natives/*`,
`src/rt/value.h` (patches supplied), legacy sources.

## Design

### Files

| File | Concern |
|---|---|
| `src/rt/handle.h/.cpp` | CP09 `HandleRegistry` (unchanged behaviour, plus `check()`) and the shared status vocabulary: `HandleStatus`, `handle_status_code`, `handle_status_text`, `handle_error_class` |
| `src/rt/handles/types.h/.cpp` | contexts, release reasons, lifecycle states and termination causes, `KindSpec`/`CleanupHook`/`CleanupEvent`/`CleanupOutcome`, `CleanupFailure`, `CloseReport`, options, stats |
| `src/rt/handles/slot_store.h/.cpp` | slots, identities, generations, reuse and retirement, intrusive per-owner lists (no locking) |
| `src/rt/handles/context_tree.h/.cpp` | the execution-context tree, states (Open, Cancelling, Closing), access rule, close order (no locking) |
| `src/rt/handles/lifecycle.h/.cpp` | lifecycle records: one per resource, outliving its slot until settled (no locking) |
| `src/rt/handles/completion.h/.cpp` | `CompletionTicket`, the one-shot completion API |
| `src/rt/handles/table.h/.cpp`, `table_contexts.h/.cpp` | `HandleTable`: the locked facade; runs hooks; context end, shutdown, detach, transfer |
| `src/rt/handles/handle_ref.h` | `HandleRef`: RAII counted reference (retain/drop) for host code |
| `src/rt/handles/limit_setting.h/.cpp` | `NIFT_HANDLE_LIMIT` |
| `src/rt/handles/native_access.h/.cpp` | native-side interface over `rt::Value` (no `RuntimeValue`; no transfer) |

### Identity

A handle is the CP09 immediate `Value` `(HandleKind, 64-bit id, 32-bit
generation)`; there is no string encoding (D3 stays removed).

* `id = (table tag << 32) | slot index`. The tag is a process-wide counter,
  never 0, different for every `HandleTable`: a handle from another table is
  `Foreign`, never a slot of this one.
* Generations start at **1** (0 is never issued) and are bumped when a handle
  is closed. A slot never re-issues a generation: every handle ever issued for
  it is below its current generation once closed (`Closed`), and anything at or
  above it that is not the open handle was never issued (`Forged`). This is how
  a closed or reclaimed handle keeps enough identity to be recognised as
  valid-but-closed (D-035) without keeping any per-handle state: the slot can
  be reused at once and the old handle still answers `runtime.handle_closed`.
  Generations are per slot across kinds: a closed `File` handle can never
  resolve to a `Process` that reuses the slot.
* **Generation wrap**: a slot whose generation is `UINT32_MAX` is retired when
  closed and never reused (CP09 rule kept). Retired slots count against the
  capacity (reachable only after 2^32 cycles of one slot).
* Classification order: not a handle → kind ≠ expected → tag ≠ table → slot
  unknown or generation 0 → (open slot) older generation `Closed`, newer or
  slot kind ≠ handle kind `Forged` → (free slot) older `Closed`, else `Forged`
  → (retired slot) `Closed`. Wrong kind is decided before staleness, so a
  closed handle of another kind is still `KindMismatch` (fatal).

Kinds are a byte: any `HandleKind` but `None` can be registered (tested with
kind 200). `tests/runtime2/cp17/patches/value-h-stream-command.patch` adds the
named `Stream` and `Command` kinds CP16 §2.3 asks for.

### Identity vs ownership

A handle **identifies** a resource; it never owns it.

| Concept | What it is | Who holds it |
|---|---|---|
| identity | the handle `Value` (tag, slot, generation, kind) | any number of copies anywhere |
| reference | a counted claim that keeps a *counted* resource from being abandoned | the VM's handle boxes (patch), `HandleRef` in host code |
| ownership | the single responsibility for ending the resource | exactly one owner per open resource: one execution context, or `kDetached` |
| lifecycle | the authoritative state of the resource (below) | the table, and only the table |

1. **One owner.** A slot has one `owner`; it is set by `create()` and changed
   only by `detach()` or the host-internal `transfer()`, each one locked step
   that names (or, for `detach_as`, checks) the current owner. Two racing
   ownership changes cannot both succeed.
2. **No double cleanup.** A slot is given up exactly once (close, last drop,
   context end, shutdown, refused transfer with reclaim); only that caller runs
   the hook. A lifecycle leaves `Active` exactly once and settles exactly once.
3. **No premature implicit cleanup.** Dropping references never ends a
   resource while another counted reference lives. Explicit `close()` by the
   owner (or a context nested in it) ends it whatever references remain; the
   others then hold a closed identity (`runtime.handle_closed`, catchable),
   never another resource.
4. **Owner end wins.** When the owning context ends, its open resources are
   cleaned up whatever references remain (the fallback for still-referenced
   handles, D-030/D-035).

### Ownership and lifetime (D-029/D-030/D-035)

Contexts: `Script`, `Build`, `BuildHook`, `PageRender`, `AutoRebuild`, opened
with a parent (or as roots) and a **label** (`"page 'index'"`, `"build"`,
`"script a.f"`). Ids are never reused (`ContextClosed` / `UnknownContext`).
The table is the one source of truth for a context's label and for whether it
is **ending on an error** (`close_context(id, /*on_error=*/true)`; its
descendants end on the same error): both reach hooks
(`CleanupEvent::context_label`, `context_error`) and reports
(`CloseReport::label`, `on_error`, `CleanupFailure::context_label`), so CP16
keeps no per-thread record of its own.

* **Access (O6, confirmed):** a handle resolves for its owner and for contexts
  nested in it; never for parents or siblings (`NotOwned`). Detached handles
  resolve for any context; host code may pass `kAnyAccessor`.
* **Build family:** `Build`, `BuildHook`, `PageRender`, `AutoRebuild` and
  everything nested in one forbid detaching (`DetachForbidden`, with the
  kind's code: `process.detach_in_build` for processes, otherwise
  `runtime.detach_in_build`) and transfers out of the family.
  `context_in_build()` answers up front so a native can refuse a detached
  launch before starting anything.
* **Cancelling:** `cancel_context()` marks a context and its descendants
  Cancelling (a superseded `--auto` pass, a cancelled worker): no new handles,
  children, detaches or inbound transfers; existing handles stay open until
  the context is closed.
* **Context end** (`close_context`): the subtree is marked Closing, then each
  context is cleaned descendants first (later-opened first), each context's
  handles newest first (LIFO), one hook call per handle (reason `ContextEnd`).
  Failing hooks never stop the others. Then `close_context` **waits until
  every obligation of those contexts has settled**: a context never reports a
  final result with cleanup unresolved (D-035). CP16 bounds that wait (grace,
  forced termination, 5 s post-kill bound → `Lost`). Reference drops never
  wait; only context end does.
* **Shutdown** (`shutdown()`, also the destructor): closes every open root
  (newest first), releases detached handles' bookkeeping (reason `Shutdown`),
  waits for every obligation. Idempotent. Hook state and tickets must not
  outlive the table.

### Lifecycle and completion API (D-035; the CP16 contract)

The table owns handle identity, reference counts, context ownership and the
**authoritative lifecycle state**. CP16 owns OS operations and their outcomes
and reports them through the completion API. Exactly one component decides
and records whether cleanup Completed, Failed or was Lost: the table.

**States** (`LifeState`), per resource, independent of whether its handle is
open:

```
Active ──(termination begins)──▶ Terminating ──(ticket completed once)──▶ Completed | Failed | Lost
```

| State | Meaning |
|---|---|
| `Active` | in use; nothing has started terminating it |
| `Terminating` | termination started (graceful termination in progress is **not** a failure) |
| `Completed` | Nift has no further obligation: reaped/closed, or for a detached process its bookkeeping released while it lives on |
| `Failed` | the OS gave a definitive failure, or the bounded procedure was exhausted without success |
| `Lost` | termination could not be confirmed. Never described as "still running" |

**Failure detail** (`FailureDetail`, on every `CleanupFailure`) keeps the
three process cases apart, so a report never calls an exited process alive or
lost (D-035):

| Reported with | State | Detail | Default code / message |
|---|---|---|---|
| `alive_after_kill(code, msg)` | `Failed` | `AliveAfterKill`: leftover known to be alive after SIGKILL / forced termination | `internal.handle_cleanup_failed`, "still alive after forced termination" |
| `lost(code, msg)` | `Lost` | `Unconfirmed`: termination could not be confirmed | `internal.handle_cleanup_lost`, "termination could not be confirmed" |
| `exited_cleanup_incomplete(code, msg)` | `Failed` | `ExitedCleanupIncomplete`: known to have exited, but reaping or handle cleanup could not be completed | `internal.handle_cleanup_incomplete`, "exited, but cleanup could not be completed" |
| `failure(code, msg)` | `Failed` | `Other` | `internal.handle_cleanup_failed`, "cleanup failed" |

`Lost` is always `Unconfirmed` and `Unconfirmed` is always `Lost` (the table
normalises a mismatched report). All four force a nonzero exit; the original
error stays primary.

**What starts termination** (`TerminationCause`), exactly once per resource,
first event wins, whoever it is:

| Event | API | Handle afterwards |
|---|---|---|
| deadline, cancellation, natural exit | CP16 calls `begin_termination(h, kind, cause, ticket, &info)` | stays open: timeout/cancel/exit outcomes stay on CP16's result object, they are not `handle_closed` |
| explicit close | `close()` (script) / `release()` (host) → hook, reason `Explicit` | closed |
| last counted reference dropped | `drop()` / `HandleRef` / VM box destruction → hook, reason `Abandoned` | closed |
| owning context ends | `close_context()` → hook, reason `ContextEnd` | closed |
| refused transfer with reclaim | `transfer(..., ReclaimOnRefusal)` → hook, reason `ContextEnd` | closed |
| table shutdown (detached) | `shutdown()` → hook, reason `Shutdown` | closed |

`begin_termination` returns the ticket (`info.started`) only while the
lifecycle is `Active`; later calls report the current state and cause and
get no ticket, so no resource is terminated twice.

**What a hook receives** (`CleanupEvent`): kind, the (already closed) handle,
owner, release reason, the owner's label and whether it is ending on an error, the resource (`shared_ptr`; the reaper may keep a copy:
"count reached zero" is not "fully reclaimed"), the lifecycle `state` when
cleanup began, the `cause` that started termination (if any), and `ticket`:

* `ticket != nullptr` (the lifecycle was `Active`): the hook is responsible.
  It returns a final outcome synchronously (`completed()`, `failure(code,
  msg)`, `lost(code, msg)`), or moves the ticket out, schedules the work and
  returns `pending()`. It must not block: hooks can run on the VM thread that
  dropped the last reference.
* `ticket == nullptr` (a deadline or cancel already started termination, or
  the resource already settled): the existing ticket decides; the hook only
  releases bookkeeping and its return value is ignored (an exception is still
  reported as a failure).
* Detached processes: the hook sees `owner == kDetached`, completes at once
  and does not touch the process (it survives its last reference).

**How async completion is reported:** `CompletionTicket::complete(outcome)`
from any thread, exactly once (`true` if it settled the lifecycle; a second
call, or a moved-from ticket, returns `false`). `pending()` is not a final
outcome. A ticket destroyed or overwritten without being completed settles the
resource as `Failed` with `internal.handle_cleanup_unresolved`, so no context
waits forever on a lost report. A hook that returns `pending()` without taking
the ticket gets the same.

**How the close report and the final result are derived:** every `Failed` or
`Lost` settle (synchronous or asynchronous, while the handle was open or
after) is recorded as a `CleanupFailure` (handle, kind, owner, reason, cause,
state, detail, context label, code, message) against the resource's owner at settle time (detached:
the shutdown report). `close_context` appends each context's failures in
settle order after all its obligations settled, so the `CloseReport` is final.
`cleanup_failed()` is true when any failure was recorded, and
`final_exit_status(original)` gives the run's status: a nonzero `original`
(the earlier error, or a signal-derived 130/143 during SIGTERM) stays primary;
otherwise a cleanup failure makes it 1 (D-020/D-035). Default codes:
`internal.handle_cleanup_failed`, `internal.handle_cleanup_lost`,
`internal.handle_cleanup_incomplete`, `internal.handle_cleanup_threw`,
`internal.handle_cleanup_unresolved`; CP16 passes specific ones
(`process.kill_failed`, `process.lost`, `process.reap_failed`, ...).

### Errors (D-020, D-022, D-025, D-035)

| Status | Code | Class | Meaning |
|---|---|---|---|
| `Closed` | `runtime.handle_closed` | **Recoverable** (catchable) | the handle was valid and has been closed or reclaimed; `close()` on it is Ok (idempotent) |
| `NotAHandle` | `internal.handle_not_a_handle` | Internal | the value is not a handle (e.g. a legacy `"\x1fnift:file:0"` string) |
| `KindMismatch` | `internal.handle_kind_mismatch` | Internal | handle of another kind |
| `Foreign` | `internal.handle_foreign` | Internal | issued by another table |
| `Forged` | `internal.handle_forged` | Internal | never issued |
| `TypeMismatch` | `internal.handle_type_mismatch` | Internal | `resolve_as<T>` with the wrong `T` |
| `NotOwned` | `internal.handle_not_owned` | Internal | caller may not access / is not the required owner |
| `NotCounted` | `internal.handle_not_counted` | Internal | retain/drop on an uncounted handle |
| `ContextClosed` / `UnknownContext` | `internal.handle_context_closed` / `internal.handle_unknown_context` | Internal | context ended, ending or cancelling / never issued |
| `UnknownKind` / `KindRegistered` / `NullResource` | `internal.handle_unknown_kind` / `..._kind_registered` / `..._null_resource` | Internal | registration and create errors |
| `DetachForbidden` | per kind: `process.detach_in_build`, else `runtime.detach_in_build` | Error | detach inside a build (D-030) |
| `TransferForbidden` | `runtime.transfer_out_of_build` | Internal (host-facing only) | transfer out of the build family, or adoption of a detached resource |
| `LimitExceeded` | `limit.exceeded` | Limit | `run: handle limit exceeded (N handles, NIFT_HANDLE_LIMIT)` |
| `InvalidSetting` | `runtime.invalid_argument` | Error | `NIFT_HANDLE_LIMIT='x' (expected a handle count from 1 to 4294967295, optionally with K or M)` |
| `CleanupFailed` | the failure's own code | Internal | `close`/`release` whose hook settled Failed/Lost synchronously (also in the close report) |

Forged, cross-table and wrong-kind handles stay fatal `internal.*`: the
compiler admits handle values only where they cannot be forged (W13).
`tests/runtime2/cp17/patches/cp13-handle-codes.patch` registers every code
with CP13's table (`src/Diagnostic.h` and `code-taxonomy.tsv`;
applies to `spec-integration-cp14` @ce932e39; `runtime.handle_closed` is the
only Recoverable one).

### Capacity: `NIFT_HANDLE_LIMIT` (D-028 convention)

`table_options_from_environment()` reads `NIFT_HANDLE_LIMIT=<count>[K|M]`
(binary multiples, 1 to 4294967295; default 2^20). Like `NIFT_READ_LIMIT`, an
invalid value is reported by every `create()` (`InvalidSetting`), never
silently replaced. The limit bounds live + free + retired slots; free slots
are reused first, so the slot count is the live high-water mark. Creating past
it is `limit.exceeded` naming the setting; the refused resource is destroyed
(outside the lock) and no hook runs.

### Ownership transfer (O2: host-internal)

`transfer(h, kind, expected_owner, new_owner, policy)` is for the host only
(async workers handing a result to the parent context); scripts get `close`
and `detach` and nothing in `native_access.h` reaches `transfer` (checked by
`make test-rt-handles`). It is one locked step, so there is no window in which
no context is responsible. It requires the named current owner, an open or
cancelling source, and an **open** destination in the same build family.
A destination that has ended or is cancelling refuses the transfer
(`ContextClosed`/`UnknownContext`); with `ReclaimOnRefusal` the resource is
cleaned up at once (reason `ContextEnd`), with `KeepOnRefusal` it stays with
the source, whose end is the fallback. Returning a handle from an async
worker: the host transfers it to the parent before closing the worker
context, so the worker's end never releases it. `detach` stays separate.

### Counted references and the VM hook (O1)

Counted handles (`CreateOptions::counted`) start with the creator's
reference; `retain`/`drop` (or `HandleRef`) count references; the last drop
starts cleanup with reason `Abandoned` and returns without waiting (the hook
schedules). No separate GC: abandonment is reference counting; context end is
the fallback for anything still referenced (including cycles).

`tests/runtime2/cp17/patches/vm-handle-box.patch` (apply after
`value-h-stream-command.patch`) is the VM side, tested in a scratch worktree:

* `HandleBoxObject : HeapObject` (kind `Handle`) holds one table reference and
  a release callback; a boxed handle `Value` keeps kind and generation inline
  and marks itself boxed in byte 12; `is_heap()`, `handle_id()` and
  destruction (`delete_object` calls the callback, which `drop`s) learn about
  it; `heap_account` counts the box.
* `rt/handles/handle_box.{h,cpp}`: `box_counted_handle(table, h)` adopts the
  creator's reference; `share_into_isolate(table, boxed)` retains a new table
  reference for another isolate (heap refcounts are per isolate);
  `Value::handle_identity()` gives the plain handle for table calls.
* Equality and hashing of boxed and plain handles agree (same identity).
* The VM must destroy its values before the table shuts down (the run's table
  outlives the run's heap).

### Concurrency contract

* One `HandleTable` per Host run (CP16 `HostHandles`), shared by the run's
  contexts and by every worker thread of those contexts. Contexts are not
  thread-affine.
* Every operation is linearizable under one mutex. Hooks run without it, may
  call back into the table, must not block and must not close the context
  that owns the handle being cleaned.
* `CompletionTicket::complete` may be called from any thread (the reaper).
* `resolve` returns a `shared_ptr` copy: a resource stays alive while a native
  uses it even if another thread ends it meanwhile.
* `close_context` returns only after every hook for its handles has finished
  (including ones other threads started) and every obligation has settled.
* Kind registration is write-once.
* Context end in a render or build happens after its workers settled (as the
  host-deps merge already requires). A worker still running sees
  `ContextClosed` on create (the resource is destroyed, never leaked).

### Interface for CP16 (Host layer)

```cpp
HandleTable table(table_options_from_environment());       // one per Host run (HostHandles)
KindSpec spec{"process", hook, "process.detach_in_build"};
table.register_typed_kind<ProcessResource>(HandleKind::Process, spec);
ContextId script; table.open_context(ContextKind::Script, kDetached, script, "script a.f");
bool in_build; table.context_in_build(script, in_build);   // refuse a detached launch early
Value h; table.create(script, HandleKind::Process, group, h, CreateOptions{/*counted*/ true});
CompletionTicket t; TerminationStart info;                 // deadline / cancel / natural exit
table.begin_termination(h, HandleKind::Process, TerminationCause::Deadline, t, &info);
if (info.started) reaper.cancel(group, std::move(t));       // reaper later: t.complete(completed() | failure() | lost())
CloseReport r = table.close_context(script, failed);       // waits for obligations; r.final_exit_status(status)
```

A process hook: if `!e.ticket` return `pending()` (already terminating or
settled); if `e.owner == kDetached` return `completed()` (bookkeeping only);
if the group already exited and was reaped, `completed()`; otherwise start
the §11.4 sequence asynchronously, hand `std::move(*e.ticket)` to the reaper
and return `pending()`; the reaper reports `completed()`, `alive_after_kill()`,
`lost()` or `exited_cleanup_incomplete()`. Warnings (§11.3/§11.5) are CP16's,
using `e.context_label` and `e.context_error`; whether the process was still
running is CP16's knowledge, and `Lost` is never phrased as running.

### Interface for natives

`native_access.h`, over `rt::Value` only: CP16 adds `handles::HandleAccess
handles;` (table + current context) to `natives::Context`.
`native_handle_arg<T>` resolves an argument; `native_handle_result` creates a
handle owned by the native's context; `native_handle_close` (idempotent) and
`native_handle_detach` (refused in builds with the kind's code). Error text:
`<builtin>: <reason> (<kind> handle <slot>/<generation>, <code>)`. Status
mapping: Recoverable → catchable error with the code, Internal →
`Status::Internal`, Limit → CP16's `Status::Limit`, Error → `Status::Error`.

## Integration work (hooks and patches for other owners)

**CP16 (Host layer)**
1. Apply `patches/value-h-stream-command.patch`.
2. `HostHandles` holds one `HandleTable` built from
   `table_options_from_environment()`; register File, Stream, Command, Timer
   and Process kinds with typed resources and hooks (§2.3 mapping and the
   process hook above).
3. Open/close contexts at the D-030 boundaries; `cancel_context()` a
   superseded `--auto` pass before closing it; feed
   `CloseReport::final_exit_status()` into the run's exit status and print
   the failures as diagnostics after the primary error.
4. Report deadline, cancel and exit through `begin_termination` + ticket.
5. Add `src/rt/handles/*.cpp` to `RT_LINK_SOURCES` when the first native uses
   the table.

**CP13 / diagnostics:** apply `patches/cp13-handle-codes.patch`.

**VM (compiler/vm owners)**
1. W13 stays the forgery barrier.
2. Apply `patches/vm-handle-box.patch`; natives that return counted handles
   box them with `box_counted_handle`; table calls use `handle_identity()`.
3. Map `HandleErrorClass` as above; `runtime.handle_closed` is catchable.

## Tests

| Test | What it covers |
|---|---|
| `tests/rt/handle_table_unit.cpp` | status codes (all distinct) and classes, CP09 `check()`, registration, identity layout, forgery matrix (string, int, null, kind, foreign tag, unknown slot, generation 0 / future / max, rewritten kind byte), closed after release and after reuse by another kind, generation wrap and retirement, churn reclamation (200,000 steps, capacity 64), capacity limit |
| `tests/rt/handle_table_ownership.cpp` | access rules, identity vs ownership, detach/transfer as single steps, build-family bans, `context_in_build`, close ordering across a build with pages and a hook (LIFO; an older handle resolvable inside a newer one's hook), failing hooks (failure / `std::exception` / non-exception) all reported with the rest still cleaned, explicit-release failure in the close report, idempotent `close`, closed and unknown contexts, destructor shutdown, counted references, native interface |
| `tests/rt/handle_lifecycle_unit.cpp` (+ `_more.inc`) | D-035 with a fake process kind and a test-driven reaper: aliasing; last-reference release (non-blocking, closed at once, obligation until settled); `close_context` waiting for obligations; deadline and cancel before/after release (one termination; timeout stays a resource outcome); 300 threaded races of deadline/cancel vs last drop vs `close()`; detached process surviving its last reference, detach refused in builds; context-end fallback with live references; success, definitive failure, earlier error and signal status primary, `Lost` text, asynchronous failure after drop, deadline failure while open, unresolved tickets (hook and reaper), exactly-once tickets; 200 settle-vs-close races; `handle_closed` identity (explicit close, idempotence, automatic cleanup, registry reuse, cancel race, wrong kind, forged); transfers (worker result, cancelled and ended destinations with keep/reclaim, stale handle, 200 races against the source's end); `NIFT_HANDLE_LIMIT` parsing, message and invalid value; context labels and the on-error flag in hooks and reports (a build ending on an error with a Lost page process and a build process alive after SIGKILL); exited-but-cleanup-incomplete next to the other two cases, and a mismatched Lost report normalised |
| `tests/rt/handle_table_stress.cpp` | 4 workers + a context manager + an asynchronous reaper over one table: random create (counted and not), resolve (always the identified resource), release, retain/drop, detach, deadlines (`begin_termination`), transfer while contexts open and close; a capacity-48 run hitting the limit; `release`/`drop`/`close_context` race on one counted handle; `close_context` blocked on another thread's hook. Every resource is cleaned once and settled once; nothing outlives shutdown |
| `tests/rt/handle_table_bench.cpp` + `tests/runtime2/cp17/handle_guards.py` | callgrind instruction guards (+10 %) |
| `tests/runtime2/cp17/handle_mutants.py` | 23 mutants, each removing one rule; the unit or lifecycle suite must fail (a hang counts) |
| `tests/runtime2/cp17/no_handle_strings.py` | gate: no `\x1fnift` handle string in code under `src/rt`; self-test |
| `patches/vm-handle-box.patch` (`tests/rt/handle_box_unit.cpp`) | boxed handles: refcounted aliases, equality/hash with plain handles, cross-isolate share, last box schedules cleanup |

Make targets: `test-rt-handles` (unit, lifecycle, stress, gates, lint),
`test-rt-handles-sanitize` (ASan+UBSan), `test-rt-handles-tsan`,
`test-rt-handles-guards` (callgrind; run under `heavy.sh`). Runs are under
`ulimit -f` and `timeout` (D-031); `ulimit -v` where no sanitizer runs.

### Results (2-vCPU shared host, GCC 13.3)

RESULTS_PLACEHOLDER

## Owner decisions (D-035)

| # | Decision | Where |
|---|---|---|
| O1 | counted handle kinds boxed in a refcounted VM value; last reference schedules cleanup; exactly-once lifecycle; detached survive; context end fallback | counted references, lifecycle, `vm-handle-box.patch` |
| O2 | `transfer` host-internal, atomic, refused (optionally reclaimed) into ended/cancelling contexts | Ownership transfer |
| O3 | `runtime.handle_closed` catchable; forged / foreign / wrong kind fatal `internal.handle_*`; per-kind detach code; `close()` idempotent | Errors, `cp13-handle-codes.patch` |
| O4 | `NIFT_HANDLE_LIMIT`, named by `limit.exceeded` | Capacity |
| O5 | cleanup failure → nonzero exit; original error / signal status primary; failure only after the bounded procedure or a definitive OS failure; `Lost` = unconfirmed | Lifecycle and completion API |
| O6 | nested contexts may use ancestors' handles; not parents or siblings | Ownership and lifetime |

## Open questions

* **Q1 — `close()` vs other references.** Explicit `close()` ends a resource
  even while other references exist (they then get `runtime.handle_closed`).
  The earlier design refused this (`InUse`); with VM boxing every variable is
  a reference, so refusing would make `close()` fail on any aliased handle.
  Confirm.
* **Q2 — Detached failures.** Failures of detached resources (their
  bookkeeping, or a cancel the script sent) are reported by `shutdown()`, at
  the very end of the run. Should they also affect the exit status (they are
  not a context's obligations)?
