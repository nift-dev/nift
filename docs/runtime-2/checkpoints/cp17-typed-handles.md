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
* **Who reports a context's failures (review R1):** exactly one
  `close_context` call owns ending each context: the first one to reach it.
  A parent close does not take over a descendant another close is already
  ending; it waits until that descendant is gone, counts it in
  `descendants_closed_elsewhere`, and leaves its failures to that close's
  report. A second `close_context` on a context another close owns waits until
  it is gone and returns `ContextClosed` with `closed_elsewhere = true`. Every
  caller therefore returns only after the context's hooks and obligations
  finished, and each failure appears in exactly one report.
* **Re-entrancy (R3):** `close_context` and `shutdown` called from inside a
  cleanup hook of the same table return `ReentrantClose`
  (`internal.handle_reentrant_close`) instead of waiting for the hook itself.
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
open (owner definitions):

```
Active ──(termination begins)──▶ Terminating ──(ticket completed once)──▶ Completed | Failed | Lost | CleanupIncomplete
```

| State | Meaning |
|---|---|
| `Active` | in use; nothing has started terminating it |
| `Terminating` | termination started; graceful termination in progress is **not** a failure |
| `Completed` | the required cleanup finished (reaped / closed; for a detached process: Nift's bookkeeping released while the process lives on) |
| `Failed` | the resource is **known to be alive** after forced termination |
| `Lost` | termination **could not be confirmed**; never described as "still running" |
| `CleanupIncomplete` | the resource is **known to have ended** (exited, closed) but reaping or handle cleanup could not be completed; never described as "still alive", never `Lost` |

**Outcomes → states → exit status → handle** (CP16 reports OS facts only; the
table maps them):

| CP16 reports | State | Default code / message | D-035 exit rule | Handle afterwards |
|---|---|---|---|---|
| `completed()` | `Completed` | — | no effect | open handle stays open; once closed, uses give `runtime.handle_closed` |
| `alive_after_kill(code, msg)` | `Failed` | `internal.handle_cleanup_failed`, "still alive after forced termination" | nonzero (1) unless an earlier error / signal status (kept) | same |
| `lost(code, msg)` | `Lost` | `internal.handle_cleanup_lost`, "termination could not be confirmed" | nonzero, same rule | same |
| `cleanup_incomplete(code, msg)` (= `exited_cleanup_incomplete`, `failure`) | `CleanupIncomplete` | `internal.handle_cleanup_incomplete`, "ended, but cleanup could not be completed" | nonzero, same rule | same |
| hook threw / ticket never completed / `pending()` given as final | `Lost` | `internal.handle_cleanup_threw` / `internal.handle_cleanup_unresolved` | nonzero, same rule | same |

The settled state is the resource's, not the handle's: a deadline that ends
in `Failed` while the handle is open leaves the handle usable (its result is
CP16's); only closing the handle makes uses answer `runtime.handle_closed`
(and `close()` again is Ok). `CloseReport::final_exit_status(original)`:
a nonzero `original` (the first error, or a signal-derived 130/143) stays
primary; otherwise any `Failed`, `Lost` or `CleanupIncomplete` gives 1.

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

`begin_termination` accepts only `Exited`, `Deadline` and `Cancel`
(`InvalidCause` otherwise: the other causes belong to the table's own events).
`info.started` means exactly "a ticket was issued": only while the lifecycle is
`Active`. Later calls report the current state and cause and get no ticket,
so no resource is terminated twice. For a detached handle no ticket is ever
issued; `info.detached_request` tells CP16 it may send the cancel request.

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
  releases bookkeeping and its return value is ignored. An exception from a
  hook is always reported (`Lost`, `internal.handle_cleanup_threw`), also when
  it was thrown after the hook took the ticket (R9).
* Detached resources (D-039): the hook sees `owner == kDetached` and **no
  ticket**; it only releases Nift's bookkeeping and does not touch the process
  (it survives its last reference). The table records `Completed` at once: no
  record, ticket or obligation is kept for a detached resource and nothing
  waits for it, except one case: a resource detached after it exited whose
  reaping ticket CP16 still holds keeps its record until that report arrives
  (record ids must not be reused under a live ticket); `shutdown()` waits for
  it. A failure the hook reports for a detached resource (it knows it), or an
  exception, goes to `shutdown()`'s `detached_failures` (R6), which never
  change the exit status. `begin_termination` on a detached handle (a script's
  cancel) issues no ticket and leaves the lifecycle unchanged: a request, not
  supervision.

**How async completion is reported:** `CompletionTicket::complete(outcome)`
from any thread, exactly once (`true` if it settled the lifecycle; a second
call, or a moved-from ticket, returns `false`). `pending()` is not a final
outcome. A ticket destroyed or overwritten without being completed settles the
resource as `Lost` with `internal.handle_cleanup_unresolved`, so no context
waits forever on a lost report. A hook that returns `pending()` without taking
the ticket gets the same.

**How the close report and the final result are derived:** every `Failed` or
`Lost` settle (synchronous or asynchronous, while the handle was open or
after) is recorded as a `CleanupFailure` (handle, kind, owner, reason, cause,
state, context label, code, message) against the resource's owner at settle time (detached:
the shutdown report). `close_context` appends each context's failures in
settle order after all its obligations settled, so the `CloseReport` is final.
`cleanup_failed()` is true when any failure was recorded, and
Failures Nift actually knows about for detached resources (a hook that threw
while releasing bookkeeping) go to `shutdown()`'s `detached_failures`, which
never change the exit status (D-039). `final_exit_status(original)` gives the
run's status: a nonzero `original`
(the earlier error, or a signal-derived 130/143 during SIGTERM) stays primary;
otherwise a cleanup failure makes it 1 (D-020/D-035). Default codes:
as in the table above; CP16 passes specific ones (`process.kill_failed`,
`process.lost`, `process.reap_failed`, ...).

### Errors (D-020, D-022, D-025, D-035)

| Status | Code | Class | Meaning |
|---|---|---|---|
| `Closed` | `runtime.handle_closed` | **Recoverable** (catchable) | the handle was valid and has been closed or reclaimed; `close()` on it is Ok (idempotent) |
| `NotAHandle` | `internal.handle_not_a_handle` | Internal | the value is not a handle (e.g. a legacy `"\x1fnift:file:0"` string) |
| `KindMismatch` | `internal.handle_kind_mismatch` | Internal | handle of another kind |
| `Foreign` | `internal.handle_foreign` | Internal | issued by another table |
| `Forged` | `internal.handle_forged` | Internal | never issued |
| `TypeMismatch` | `internal.handle_type_mismatch` | Internal | `resolve_as<T>` with the wrong `T` |
| `NotOwned` | `internal.handle_not_owned` | Internal | caller may not access / is not the required owner (host-facing; script detach reports `DetachFailed`) |
| `NotCounted` | `internal.handle_not_counted` | Internal | retain/drop on an uncounted handle |
| `ContextClosed` / `UnknownContext` | `internal.handle_context_closed` / `internal.handle_unknown_context` | Internal | context ended, ending or cancelling / never issued |
| `UnknownKind` / `KindRegistered` / `NullResource` | `internal.handle_unknown_kind` / `..._kind_registered` / `..._null_resource` | Internal | registration and create errors |
| `DetachForbidden` | per kind: `process.detach_in_build`, else `runtime.detach_in_build` | Error | detach inside a build (D-030) |
| `DetachFailed` | `runtime.detach_failed` | Error | detachment could not be established (D-039): Nift already started terminating the resource (deadline, cancel, close; decided by the termination cause, never by timing), the owning context is ending or cancelling, or the caller does not own it. A resource that only exited (reported, reaped or not) detaches trivially (R5). CP16 reports its own failures to set the lifetime up (setsid, job breakaway) as operation failures and does not call `detach` |
| `NotCounted` on detach | `internal.handle_not_counted` | Internal | only counted handles can be detached, so a detached handle's bookkeeping always ends with its last reference (R14); CP16 creates detachable kinds counted |
| `TransferForbidden` | `runtime.transfer_out_of_build` | Internal (host-facing only) | transfer out of the build family, or adoption of a detached resource |
| `LimitExceeded` | `limit.exceeded` | Limit | `run: handle limit exceeded (N handles, NIFT_HANDLE_LIMIT)` |
| `InvalidSetting` | `runtime.invalid_argument` | Error | `NIFT_HANDLE_LIMIT='x' (expected a handle count from 1 to 4294967295, optionally with K or M)` |
| `CleanupFailed` | `runtime.cleanup_failed`; natives report the failure's own code and text | Error (exit 1, D-035) | `close`/`release` whose resource settled Failed, Lost or CleanupIncomplete synchronously; the `CleanupFailure` (same defaults as the report) is also in the close report (R4) |
| `InvalidCause` | `internal.handle_invalid_cause` | Internal | `begin_termination` with a cause other than Exited/Deadline/Cancel |
| `ReentrantClose` | `internal.handle_reentrant_close` | Internal | `close_context`/`shutdown` from inside a cleanup hook of the same table |

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
cancelling source and an **open** destination. A resource never leaves the
build family (out of it: `TransferForbidden`); moving a script's resource
*into* a build is allowed, because the build then owns its cleanup, which is
stricter, not looser (R10). A destination that has ended or is cancelling
refuses the transfer (`ContextClosed`/`UnknownContext`) and, by default
(`ReclaimOnRefusal`, D-035's wording), the resource is cleaned up at once
(reason `ContextEnd`); `KeepOnRefusal` leaves it with the source, whose end is
the fallback. Returning a handle from an async
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
* `rt/handles/handle_box.{h,cpp}`: `box_counted_handle(table, h)` boxes a
  counted handle, adopting the creator's reference (a non-handle or an
  already boxed value is returned unchanged; R12); `share_into_isolate(table, boxed)` retains a new table
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
  call back into the table (resolve, create, close, drop, transfer), must not
  block, and may not call `close_context` or `shutdown` of the same table
  (`ReentrantClose`, detected per thread).
* The owner's label reaches hooks by reference (valid during the hook call:
  the context cannot end while the hook is in flight); it is copied only into
  a `CleanupFailure` (R8).
* `CompletionTicket::complete` may be called from any thread (the reaper).
* `resolve` returns a `shared_ptr` copy: a resource stays alive while a native
  uses it even if another thread ends it meanwhile. The caller's previous
  `out` value is destroyed after the lock is released, so a resource whose
  destructor drops a `HandleRef` cannot deadlock (R2). No other caller value is
  destroyed under the lock.
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
if (info.started) reaper.cancel(group, std::move(t));       // a ticket was issued: we terminate, then report
else if (info.detached_request) send_cancel_request(group); // detached: a request; nothing is tracked
// reaper later: t.complete(completed() | alive_after_kill() | lost() | cleanup_incomplete())
CloseReport r = table.close_context(script, failed);       // waits for obligations; r.final_exit_status(status)
```

A process hook: if `!e.ticket` return `pending()` (already terminating or
settled); if `e.owner == kDetached` return `completed()` (bookkeeping only);
if the group already exited and was reaped, `completed()`; otherwise start
the §11.4 sequence asynchronously, hand `std::move(*e.ticket)` to the reaper
and return `pending()`; the reaper reports `completed()`, `alive_after_kill()`,
`lost()` or `cleanup_incomplete()` / `exited_cleanup_incomplete()`. Warnings (§11.3/§11.5) are CP16's,
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
| `tests/rt/handle_lifecycle_unit.cpp` (+ `_more.inc`) | D-035 with a fake process kind and a test-driven reaper: aliasing; last-reference release (non-blocking, closed at once, obligation until settled); `close_context` waiting for obligations; deadline and cancel before/after release (one termination; timeout stays a resource outcome); 300 threaded races of deadline/cancel vs last drop vs `close()`; detached process surviving its last reference, detach refused in builds; context-end fallback with live references; success, alive-after-kill failure, earlier error and signal status primary, `Lost` text, asynchronous failure after drop, deadline failure while open, unresolved tickets (hook and reaper), exactly-once tickets; 200 settle-vs-close races; `handle_closed` identity (explicit close, idempotence, automatic cleanup, registry reuse, cancel race, wrong kind, forged); transfers (worker result, cancelled and ended destinations with keep/reclaim, stale handle, 200 races against the source's end); `NIFT_HANDLE_LIMIT` parsing, message and invalid value; D-039: a second `close()` through an alias (and through `native_handle_close`) after the first, every alias then `handle_closed`; detach refused while terminating (`runtime.detach_failed`, ownership unchanged), by a non-owner and on a closed handle; a detached cancel is a request with no ticket and nothing retained; context labels and the on-error flag in hooks and reports (a build ending on an error with a Lost page process and a build process alive after SIGKILL); every settled state (Completed, Failed, Lost, CleanupIncomplete) settled while the handle is open and after close: state, default code and message ("alive" only for Failed, "confirm" only for Lost), exit rule, `handle_closed` afterwards; CP16's exited-but-unreaped report |
| `tests/rt/handle_table_stress.cpp` | 4 workers + a context manager + an asynchronous reaper over one table: random create (counted and not), resolve (always the identified resource), release, retain/drop, detach, deadlines (`begin_termination`), transfer while contexts open and close; a capacity-48 run hitting the limit; `release`/`drop`/`close_context` race on one counted handle; `close_context` blocked on another thread's hook. Every resource is cleaned once and settled once; nothing outlives shutdown; every failure reported to the table appears in exactly one report (asserted) |
| `tests/rt/handle_table_bench.cpp` + `tests/runtime2/cp17/handle_guards.py` | callgrind instruction guards (+10 %), including `context_label` (a realistic 53-character page label; R8) |
| `tests/rt/handle_lifecycle_review.inc` (in the lifecycle suite) | the adversarial review's findings as deterministic tests: R1 a second close waits for the owning close's hooks and reports `closed_elsewhere`, and 100 rounds of child-close vs parent-close always report the child's failure in the child's report only; R2 `resolve` into an `out` holding the last reference of a resource that drops a `HandleRef`; R3 `close_context`/`shutdown` from a hook → `ReentrantClose`; R4 synchronous Failed / Failed-with-defaults / CleanupIncomplete / Lost through `close()` and `native_handle_close` (own code, own text, class Error, report); R5 detach after exit (reaped and still reaping), after a deadline, in a cancelling context; R7 every invalid cause, `started` vs `detached_request`; R9 throw after taking the ticket; R14 uncounted detach refused, last counted reference frees all bookkeeping; R10 default reclaim and transfer into a build; `close_context` waiting for another thread's in-flight hook (also in the mutant harness) |
| `tests/runtime2/cp17/handle_mutants.py` | 46 mutants (one per state mapping, D-039, and the review findings), each removing one rule; the unit or lifecycle suite must fail (a hang counts) |
| `tests/runtime2/cp17/no_handle_strings.py` | gate: no `\x1fnift` handle string in code under `src/rt`; self-test |
| `patches/vm-handle-box.patch` (`tests/rt/handle_box_unit.cpp`) | boxed handles: refcounted aliases, equality/hash with plain handles, cross-isolate share, last box schedules cleanup |

Make targets: `test-rt-handles` (unit, lifecycle, stress, gates, lint),
`test-rt-handles-sanitize` (ASan+UBSan), `test-rt-handles-tsan`,
`test-rt-handles-guards` (callgrind; run under `heavy.sh`). Runs are under
`ulimit -f` and `timeout` (D-031); `ulimit -v` where no sanitizer runs.

### Results (2-vCPU shared host, GCC 13.3)

Tip `11148415` (base `runtime-2` @6181549b), GCC 13.3, 2-vCPU shared host.
Heavy runs went through `scratchpad/heavy.sh` with `nice make -j1`.

| Run | Result |
|---|---|
| `make test-rt-handles` | unit 230 / 0; lifecycle 186 / 0; stress 515 / 0 (about 1.5 M handles created and cleaned at capacity 512, 0.2 M at capacity 48); handle-string gate 0 problems (self-test 8/8); rt-lint 0; transfer gate clean |
| `make test-rt-handles-sanitize` (ASan + UBSan, `-fno-sanitize-recover=all`) | unit 230 / 0; lifecycle 186 / 0; stress 315 / 0; no reports |
| `make test-rt-handles-tsan` | lifecycle 186 / 0; stress 315 / 0; no reports |
| `handle_mutants.py` | 31 mutants, 31 killed (including one per state mapping; `settle-twice` was dropped as unreachable, because each record gets exactly one ticket) |
| callgrind guards (marginal Ir per iteration) | create+resolve+release 2,091; resolve 251; counted lifecycle 2,165; context + 4 handles + close 8,443. Before D-035 these were 1,256 / 250 / 1,368 / 5,055: lifecycle records, tickets and settles cost about +830 Ir per handle; resolve is unchanged. The new baseline is recorded at +10 % in `handle_guards.json`. |
| `make test-rt-value` | unit 82, regress 277, round 2 28, property 1,867,405 checks; 0 failures (also with both value patches applied) |
| `vm-handle-box.patch` (scratch worktree, both patches applied) | `handle_box_unit` 12 / 0 under ASan+UBSan; `make test-rt-value` and `make test-rt-handles` pass |
| earlier: TSan lock-removal mutant | TSan reports a data race (exit 66) |

D-039 rerun at tip `4c641df5`: `make test-rt-handles` unit 234, lifecycle
208, stress 515 (0 failures, gates and lint clean); ASan/UBSan unit 234,
lifecycle 208, stress 315; TSan lifecycle 208, stress 315 (no reports);
mutants 35 of 35 killed; guards within +10 % (2,089 / 251 / 2,163 / 8,444 Ir).

One hang was found during development, and it was a defect in the test. With
the build-on-error test, the main thread was in `close_context` → `close_one`
waiting for the page's obligation, while the test's settler thread waited for
two queued tickets. The build's process is scheduled only after the page
settles, so the second ticket never came. That ordering is correct behaviour;
the test now settles one ticket at a time. An ASan use-after-free in the first
version came from hook state destroyed before the table; the contract under
Shutdown covers it.

## Owner decisions (D-035)

| # | Decision | Where |
|---|---|---|
| O1 | counted handle kinds boxed in a refcounted VM value; last reference schedules cleanup; exactly-once lifecycle; detached survive; context end fallback | counted references, lifecycle, `vm-handle-box.patch` |
| O2 | `transfer` host-internal, atomic, refused (optionally reclaimed) into ended/cancelling contexts | Ownership transfer |
| O3 | `runtime.handle_closed` catchable; forged / foreign / wrong kind fatal `internal.handle_*`; per-kind detach code; `close()` idempotent | Errors, `cp13-handle-codes.patch` |
| O4 | `NIFT_HANDLE_LIMIT`, named by `limit.exceeded` | Capacity |
| O5 | cleanup failure (`Failed` alive after forced termination, `Lost` unconfirmed, `CleanupIncomplete` ended but not cleaned up) → nonzero exit; original error / signal status primary; declared only after the bounded procedure or a definitive OS fact | Lifecycle and completion API |
| O6 | nested contexts may use ancestors' handles; not parents or siblings | Ownership and lifetime |

## Questions resolved by D-039

Both were approved as D-039: explicit `close()` closes the resource for every
reference (later uses through any alias raise `runtime.handle_closed`; a
repeated `close()` through any alias is Ok), and failures after a successful
detach never change the exit status, while failing to establish detachment is
a reportable operation failure (`DetachForbidden`, `DetachFailed`, `NotOwned`,
`Closed`). Original questions, for the record:

* **Q1 — `close()` vs other references.** Explicit `close()` ends a resource
  even while other references exist (they then get `runtime.handle_closed`).
  The earlier design refused this (`InUse`); with VM boxing every variable is
  a reference, so refusing would make `close()` fail on any aliased handle.
  Confirm.
* **Q2 — Detached failures.** Failures of detached resources (their
  bookkeeping, or a cancel the script sent) are reported by `shutdown()`, at
  the very end of the run. Should they also affect the exit status (they are
  not a context's obligations)?
