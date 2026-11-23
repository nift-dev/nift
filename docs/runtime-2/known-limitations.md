# Runtime 2.0: known limitations

This page lists what the initial Runtime 2.0 release does not run on the VM, and where the VM's
behaviour differs from the legacy interpreter on purpose. Sources: owner rulings recorded in
[decisions.md](decisions.md), and the approved semantic differences in
[compatibility.md](compatibility.md), which is authoritative for them.

## How a limitation behaves

The VM is the default scripting engine. A program that uses a construct the VM does not run is
**refused before any part of it runs**:

- exit status 1 and no output;
- `not compiled for the VM (<reason>)`;
- `note: the program was not executed: no part of it ran`;
- the command that runs it on the legacy interpreter, `NIFT_VM=0 nift …`.

Such a program is never run on legacy automatically (no silent fallback).

### Reason identifiers renamed in slot 1 (owner 2026-11-11 05:41 and 11:07; D-068 #12, D-069 #5)

The `<reason>` in the refusal text is a stable identifier. Slot 1 (C123) renamed three of them; nothing else in the
refusal text changed. Historical evidence, checksums and older records keep the old spelling and are not rewritten.

| Current id (from slot 1, C123) | Historical id (before C123) | Refusal |
| --- | --- | --- |
| `type:mul` | `type:*` | the multiplication operator on operand kinds the VM cannot prove (text "(*)" unchanged) |
| `removed-builtin:async` | `async(callable, ...) was removed; declare an async function/lambda and call it directly` | the removed `async(...)` call form |
| `removed-builtin:await` | `await(...) was removed; use 'await future' or 'await async_fn(args)'` | the removed `await(...)` call form |

`type:*` in a historical record means the multiplication refusal, not a family of `type:` reasons. Pinned by
`tests/runtime2/reason_ids.py`.

The legacy interpreter remains available: with `NIFT_VM=0`, these constructs run as they did in
Nift 4.x. Website builds keep the existing template renderer (D-053).

## Nift v4.13.0 compatibility decisions

- Async function values cannot be stored or indirectly invoked on the VM
  (`coro-async-value`). Use explicit legacy execution for scripts that require
  this value flow.
- Bare shell-command statements in scripts are refused (`statement:Command`).
  Run those scripts with `NIFT_VM=0 nift …`.
- The VM supports in-memory `minify(source, format)` with a string format.
  File-path and options forms require explicit legacy execution.
- A plain mid-file `#!` comment has no runtime effect. First-line executable
  shebang behavior is preserved. Semicolon-separated or multiline forms can
  have different legacy behavior and remain refused by the VM.

## Named deferrals (owner, 2026-11-10 13:26; D-065)

The VM does not yet run the constructs below. Nothing is removed: each one keeps working on
legacy. `make test-runtime2-named-deferrals` checks that each is refused cleanly and
deterministically and that legacy still runs it. That gate is Linux-only (its FFI case opens `libc.so.6`); on other
platforms it prints a SKIP line.

| Construct | VM refusal reason | Still works with |
| --- | --- | --- |
| A delayed import inside a function: `fn(f()) { import("./m.f"); … }` | `import-nested` | `NIFT_VM=0`. Top-level, relative, chained and package imports run on the VM. |
| Process execution: `run()`, `cmd()` and their handle methods | `callable:run`, `callable:cmd`, `method:run`, `method:pipe`, … | `NIFT_VM=0` (D-029, D-030, D-034) |
| FFI: `ffi_open` and FFI calls | `callable:ffi_open` | `NIFT_VM=0`, and the non-VM paths (docs/ffi.md, D-036) |
| Timers: `timer()` and its handle methods | `callable:timer`, `method:start`, `method:stop`, `method:elapsed`, … | `NIFT_VM=0` |
| `project_root()` | `callable:project_root` | `NIFT_VM=0`, and project builds (the template compiler, CP24, is deferred: D-053) |

**Further deferrals (owner, 2026-11-11 02:53; D-067 #2).** Deferred from the initial release; each keeps working on
legacy and is refused before execution on the VM. `make test-runtime2-named-deferrals` pins the script-reachable ones;
the session-only ones keep their existing tests (an unsafe session behaviour would be a separate defect).

| Construct | VM refusal reason | Still works with |
| --- | --- | --- |
| `a[0]++` (excluded from checkpoint 1) | `incdec-target` | `NIFT_VM=0` |
| A compound assignment used as a value: `print(a[0] += 5)` | `expression:assignment-target` | `NIFT_VM=0` |
| A future stored through a nested path: `hs[0][0] = f(1)` | `coro-future-flow:Assign` | `NIFT_VM=0` |
| In a session: `.stringify()` on an unproven receiver, a struct's own presentation method, opaque source | `builtin-receiver:stringify`, `presentation-struct-method:*`, `session:opaque` | `NIFT_VM=0` |
| Legacy dynamic scope: a callee reading its caller's local | `free-name`, `free-name:dynamic-caller` | `NIFT_VM=0` (lexical scoping is the target, D-038) |
| CP24 project and template APIs (`build`, `track`, `tracked`, `untrack`, `inject`, `status`, `build_repair`, template text in scripts) | `callable:build`, `callable:tracked`, ..., `opaque:template-text` | `NIFT_VM=0`, project builds (D-053, D-065 #5) |
| Dynamic imports (a non-literal import path) | `import-dynamic` | rejected before execution (D-037); legacy also rejects the corpus forms ("import path must be a string expression") |

## Approved refusals (release behaviour)

This construct is refused on the VM by an owner decision. It is not pending work.

| Construct | Reason | Decision |
| --- | --- | --- |
| A map, set or stack as a map key or set member, lookups included | `collection-mutable-key` | D-NEXT-refsem #6 (keys, D6) |
| Plain `@fn(...)` in a script (legacy keeps accepting it) | `fn-flags` | D-067 #3, amending D-047 #1 / D-048 #1 |
| An unqualified `cat`, `cp`, `mv`, `rm`, `ls`, `mkdir` or `touch` call with no user or module function of that name (write `native::cp(...)`) | `native-unqualified` | D-075 #2, D-095 (Runtime 2.0 breaking change, below) |
| A call of a plain object field named like one of 46 legacy method heads (`o.cat(...)`; legacy: "unknown value or malformed expression"). Also refused in unreachable code (`if (false) { o.cat("a") }`, where legacy completes), and with the same message for a missing field or a receiver of unknown type (D-109 N1, N2) | `legacy-method-precedence` | D-102, D-106, D-109 (C145 F6; compatibility.md) |
| A method named `write`, `write_line` or `write_val` with a direct `$[` literal argument (`s.write("$[x]")`): legacy's result depends on the run-time receiver | `host-literal-arg` (its inventory row is unchanged) | D-102, D-106 (C145; compatibility.md) |

**Runtime 2.0 breaking change: `native::` for seven file built-ins (L6; D-075 #2, D-095;
[checkpoints/native-qualifier.md](checkpoints/native-qualifier.md)).**
- Scope: programs the VM runs, which are script files, `-e`/`-c`/stdin programs, `-i` sessions, the REPL and the
  modules they import. There, `cat`, `cp`, `mv`, `rm`, `ls`, `mkdir` and `touch` are no longer built-ins. Call them
  as `native::cat(...)` and so on: the same natives, with the same checks, limits, tracing and dependency
  observations.
- An unqualified call of one of the seven, with no user or module function of that name, refuses the whole program
  at compile time (`native-unqualified`), even when the call is never reached. The diagnostic suggests the
  `native::` spelling, and nothing runs. In a session, the statement is refused like any other refusal.
- A user function named `cp` (or any of the seven) is an ordinary function on the VM. Legacy keeps calling its
  native first; that difference is approved (D-075 #2) and pinned in tests/runtime2/l6/adversarial-approved.json.
- Unchanged: website builds. Templates (`@script`, `$[...]`) and build hooks run on the legacy evaluator (D-053), so
  they keep the unqualified seven. This is **transitional engine-specific compatibility** (D-096 #1), not the
  intended permanent API: the goal remains consistent `native::` naming. `native::name(` works there too, with the
  same output and effects as the unqualified form. Every other built-in is unchanged.
- **No implicit external program (scope, D-101 F4).** Function-call syntax never falls back to running a program
  found on PATH: a call is a native, a user function, or refused. This is about implicit dispatch only. The explicit
  process APIs `run()` and `cmd()` launch programs by design (legacy runs them; the VM refuses them as
  `deferred-named`). Since C144 and C144b this holds in the interactive and piped shell too, for a call whose head is
  an identifier, `native::name` or a dotted member head (`o.f`): written with a blank before `(` (`cp ("a.txt",
  "c.txt")`, `o.f ("x")`) it is Nift, not a command line. A line whose first word is a shell built-in, `command`, or
  an environment assignment (`FOO=1 cp (...)`) keeps the shell rule and can run a program (compatibility.md lists
  the shell inputs that change).
- Legacy (`NIFT_VM=0`) keeps the unqualified seven as the historical oracle, and also accepts `native::name(`.
- Exactly `native::name(` is valid: no space around `::` and the `(` immediately after the name. Any other
  spelling, and `native::` before a name outside the seven, is caught by the default syntax prevalidation, which
  checks every script file, `-e`/`-c` and stdin program before engine selection. Both engines then report it with the
  same text and position, before anything runs.
- **Where prevalidation does not run, the engines differ (D-101 F2, F3).** Neither engine ever calls a wrong
  native or launches a program in any of these cases, but timing, text or position can differ:
  - with `NIFT_PREVALIDATE=off`, the VM refuses the whole program before anything runs, while legacy runs the
    earlier statements (for example prints their output) and then raises its error at the call;
  - the text differs for `native :: cp(` (legacy: `unknown value or malformed expression: native :: cp(...)`, not
    the malformed-call message) and in templates (`@script { return native :: cat(...) }` gives legacy's
    "unknown value or malformed expression");
  - an unknown name nested in an expression has the same text but a different position: the VM points at the call
    (`print(native::copy(...))` at 2:7), legacy at the statement (2:1);
  - prevalidation does not cover a bad spelling inside a `$[...]` segment of a script string, `native` and `::` on
    different lines, `fn(native::cat(p))` as a signature, or statements typed or piped into a session. The VM
    refuses each (an opaque refusal); legacy reports its own error when it reaches it (`invalid callable signature`
    for the signature form), and a session reports the error and continues.
- Migration: the repository's call sites are listed in tests/runtime2/l6/MANIFEST.tsv.

## Refusals awaiting an owner ruling

These are refused on the VM today, but no owner decision approves the refusal as release
behaviour. Their inventory status is `PENDING-OWNER`, and the gates that meet them fail until the
owner rules. (Review of C83–C99, P1-2.)

| Construct | Reason | Why it is not approved |
| --- | --- | --- |
| A function named like a built-in | `fn-name-reserved` | D-009 #3 decides the opposite: user functions take precedence over built-ins. The refusal defers D-009 #3 and does not implement it. It is not the owner's 13:26 "reserved-name check" either; that item is `binding-name-reserved` (status `implement`). |

**Unapproved behavioural differences.** Some programs run on the VM but give another result than
legacy, with no approval. They are listed, each with its gate, case, program and both outputs, in
`tests/runtime2/cp25/pending_differences.tsv`, all PENDING-OWNER. The list approves nothing: each
named gate still fails on its case until the owner rules. Today there are two:
- `copy-wrong-arity`: the VM prints `1`, legacy `1` then `2`, because legacy's loop path evaluates
  the argument twice;
- `named-precedence`: the VM prints `int`, legacy `function`.

**Plain `@fn` in a script: an explicit legacy/VM difference (approved by D-067 #3).**
- D-047 and D-048 make plain `@fn(...)` invalid in script context; the canonical form is
  `fn(...)`.
- The VM refuses it (`fn-flags`).
- The legacy interpreter still accepts it: the owner declined the legacy-side rejection on
  2026-11-10. So `@fn(v()) { return 3 }` then `print(v())` prints `3` with `NIFT_VM=0`, and is
  refused under the VM default.
- Repository fixtures use `fn(...)` (D-048 #4).

## Approved semantic differences

- **Import chains 33 to 63 modules deep** run on the VM; legacy fails at depth 33 ("maximum template parse depth
  exceeded"). Approved, subject to cycle and resource safety (D-067 #4). Depth 64 is refused (`import-limit`, the
  bounded module-graph walk). Pinned by `make test-runtime2-import-depth`.

Where the VM runs a program but its result differs from legacy on purpose, the difference is
approved and listed in [compatibility.md](compatibility.md). This list does not repeat them. The
main ones:

- arrays and objects are shared references (D-NEXT-refsem-b, B5);
- reference cycles are refused (D-NEXT-refsem #4);
- `const` is binding-only (owner Q2 option B);
- a failed comparator sort leaves the receiver unchanged (Q7);
- splice evaluates its arguments first;
- receiver and operand order;
- an import-scope collision is reported before the program runs (B4 C1);
- a function value has no shown form in `nift eval` (b4-dispatch.md 6).
- the assignment family (checkpoint 1): a compound element update `a[i] += f()` reads and writes
  the element at the key read before the right-hand side (D2, owner 2026-11-10 16:35), where legacy
  reads at the old key and writes at the new one. The same rule decides when the rhs replaces an
  intermediate container (`g[0][1] += f()` where `f` sets `g[0]`; the object form too; any callee)
  or rebinds the key inline (`a[i] += (i = 1)`): the VM writes the container and key it read
  first, legacy the new ones (D2 + owner 16:35 Option C + owner 20:11 confirmation; compatibility.md). A path store or update reads its receiver first
  for every callee (H-A D1.1, D2); its aliases, cycles, `const` and loop wording follow B5, Q2 B and
  D-018c.

**Assignment family limits (checkpoint 1, refused before any effect).** A key that is not a plain
name, an unsigned integer literal or a simple string literal (`a[i + 1][0] = v`, `o["a.b"] = v`),
`o.k = v`, `a[0]++`, a fourth level, and a compound update used as a value stay refused. Each level
must be proved an array with a number key or an object with a string key (`type:element-assignment`
otherwise): the kind inference merges a plain store's value (`o["k"] = 5`) into every member of the
object it names, so a later path through another member of that object (`o["m"]["x"] = 1`) is
refused. This is a checkpoint-1 limitation of the incomplete kind inference, not a permanent rule
(owner 20:11): it stays with the inference work for the inventory rescan; mixed operand kinds of a compound update are refused (`type:compound-assignment`). A future
stored through a path stays refused (`coro-future-flow:Assign`).

**Pending compatibility work (recorded, not decided):** `a := ["q"]; a.insert(0, "z")` is "insert: invalid index" in
legacy, while the VM inserts (found during C145; D-106 keeps it out of C145).

## Legacy-equal behaviour kept on purpose (D-079)

- **Opening a file that is also the process's own output destination** (`ofstream("o.txt")` or `file("o.txt")`
  when stdout or stderr is redirected to `o.txt`, including under `>>`) keeps legacy's open/truncate behaviour:
  existing content can be lost and earlier output can be overwritten. This is legacy-equal and intentionally
  unchanged (D-079, owner 2026-11-12). The output-order checkpoint (C139) only publishes pending output in program
  order before such an operation; it does not change what the operation does. The `save()` safety checkpoint (D-077 #5
  item 3) covers special targets only (FIFOs, devices, sockets, symlinks to non-regular files) and must not change
  ordinary file-open semantics.

## Other refusals

Every other refusal reason the release gates see is listed, with its status, in the CP25-R
inventory (`tests/runtime2/cp25/classification.tsv`; `make cp25r-inventory`). Its `status`
column is one of:

- `approved`: an owner ruling, cited;
- `deferred-named`: the deferrals above;
- `implement`: approved for implementation before release;
- `PENDING-OWNER`: not yet adjudicated, so a release blocker;
- `retired`: a reason that is resolved or no longer emitted (it counts as none of the above).

An inventory row never implies approval.

## Parallel task memory and threads (C140; D-077 #2)

`async` and `thread()` tasks run in isolates on worker threads. Their limits are ordinary errors (exit 1, code
`limit.exceeded`, not catchable, like every other `limit.exceeded`), never an internal failure. The two new ones are
reported at the refused spawn (or the wait that could not block), awaited or not, with one `future: ` or `thread: `
prefix per awaiting level above it:

| Limit | Message |
| --- | --- |
| The unfinished tasks of a run together hold more than the heap budget (`NIFT_VM_HEAP_BUDGET`, default 1 GiB) in charges: each task's bootstrap bytes (its arguments and shared variables, the program's constants and code) plus 64 KiB | `parallel task memory budget exceeded` |
| The operating system refuses a worker thread | `parallel task resources exhausted` |
| More than 1024 nested blocked waits (existing, unchanged) | `parallel blocking wait limit exceeded` |
| More than 16,384 unfinished tasks (existing, unchanged) | `parallel task admission limit exceeded` |

**A configured heap budget is not a process-wide RSS limit, and C140 is not comprehensive global memory
enforcement.** Each task's heap has its own budget (pre-existing): six tasks that each build a 16 MiB string reached
173 MB of RSS under `NIFT_VM_HEAP_BUDGET=50000000` (review of C140). The controlled failures below are those the VM
can observe; a process killed by the operating system (the OOM killer, a signal) before an allocation returns is
outside any user-space guarantee.

**R1 and R2: fixed by C141 (D-084, D-085).** R1 (P1, D-082): a thread-creation failure during the cleanup wait of
a task's teardown (its cancellation, or the end of a task whose wait for a child was refused) ended the process in
`std::terminate` (exit 134, no Nift message); it predated C140 (932ff5e2). R2 (found in the C141 review, present at
0895a234 with only `NIFT_VM_HEAP_BUDGET`): after a task's budget or thread failure, a shared wait that nothing would
ever satisfy (a `recv` on a channel the failed task was to fill) blocked forever. Since C141, once a run has a fatal
resource limit (a task's budget or thread failure, or a cleanup wait that cannot start a worker, which also fails
every queued task unrun and refuses new ones), every `send`, `recv` or mutex `lock` that would block ends the run with
exit 1 at its own position and the first recorded reason (`parallel task memory budget exceeded` or `parallel task
resources exhausted`); a wait that can complete without blocking still completes. C141a (D-087) closes a window in which a queued task
could still start right after the cleanup failure (W1), and from then on refuses every new task after any recorded
limit (N1). Remaining limitations (unchanged,
not caused by a limit): a task that computes without waiting is never interrupted, so the run ends only when it
finishes (non-cooperative code); and an uncaught error of the main program while a task it started is blocked in a
shared wait nothing will satisfy still waits for that task forever (also with an ordinary error such as `1 / 0`; found
in C141, outside D-085's scope, reported). Details: checkpoints/async-memory.md sections 10-12.
C140 is not a full resource-safety certification.

**This is not comprehensive memory accounting.** Each task's own live heap is bounded by its own copy of the
budget, not pooled with the others: the worst case is (live tasks) x (per-task budget) + the charged bytes, where the
charges bound the number of live tasks. Thread stack address space is not charged: when it runs out (for example
under an address-space limit) the run stops with `parallel task resources exhausted`, also when an allocation fails
while a task is being created (C140a, D-081); an allocation failure anywhere else is still an internal failure. The number of tasks that can be unfinished at once is floor(budget / c), with c =
64 KiB + the task's bootstrap bytes (about 73 KB for a small program: 14,651 tasks under the default budget; before
C140 the admission capacity, 16,384, was the only bound). A budget or thread failure in a task nobody awaits still
ends the run with exit 1: after the rest of the program, or at the first shared wait that would block (C141), or at
the first later spawn, which is refused with the first recorded reason (C141a, D-087 #3: no new task starts after a
recorded limit; code already running is not interrupted); an ordinary error of such a task is ignored, as before.
Deep recursion through `async`/`thread()` is also slow beyond a few hundred
levels (about 60 s at depth 1000; the scheduler wakes every blocked level on each completion). Details:
[checkpoints/async-memory.md](checkpoints/async-memory.md).

## save() filesystem safety (C142; D-074, D-091 #5)

`save()` classifies its target before the dirty check and before any temporary file exists, in both engines
(checkpoints/save-safety.md). A regular file is replaced atomically, a symlink is followed and its target replaced
(the link kept), and a dangling symlink, the process's stdout or stderr (any alias, a redirect target included), any
non-regular file (FIFO, socket, device, directory) and a read-only file are refused (`io.write_failed`); just before
the rename the destination is revalidated (C142b, D-093). Supported: safe refusal of prohibited targets under
ordinary, non-adversarial conditions, and atomic replacement of permitted regular files. Not guaranteed: race-free
operation against concurrent adversarial path replacement. What stays:

- **Hard links break by design.** The saved name gets a new inode with the new content; other names of the old
  inode keep the old content.
- **Race windows, narrowed, not closed.** The revalidation just before the rename (D-093 (d)1) refuses a destination
  that changed (`target changed during save`); it narrows the TOCTOU race but does not eliminate it. (1) A directory
  component of the resolved path swapped by someone with write access to an ancestor after the revalidation sends the
  save into the swapped directory: the deferred (d)2 (a pinned parent directory). It is a filesystem race, not a
  portability issue, and serious under elevated privilege: avoid privileged saves into directories controlled by
  untrusted users. (2) A read-only or special file swapped in at the target name after the revalidation is replaced
  by the rename, not refused; rename(2) never follows the last component, so it is never written through or opened.
  The swaps the revalidation catches are tested with a hook build; these remaining windows are not tested.
- **save() is not race-safe; the worst case is for root (REVIEW-C142 M2).** Through window (1) the temporary file is
  created, and the rename replaces whatever entry has the target's name, in a directory that was never validated: a
  read-only or protected regular file, a FIFO or a device node (an ancestor swapped for a link to /dev makes a root
  save replace `/dev/<name>`). Both engines now create the temporary file exclusively (C142b), so neither writes
  through a link planted at the temporary name (REVIEW-C142 L2, fixed).
- **Following symlinks, as root.** A save through a link replaces the link's target (before C142, only the link). A
  link planted by another user in a directory root saves into can redirect a root save onto any regular file whose
  owner-write bit is set (for example /etc/passwd); Linux's `protected_symlinks` blocks it only in sticky
  world-writable directories.
- **Deliberate behaviour changes** (save-safety.md, "Behaviour changes"): a clean save of a special, read-only,
  std-stream or dangling target is now refused (before: a no-op); a save through a link needs write access to the
  target's directory; both engines create the temporary file exclusively and refuse an occupied temporary name
  (the VM since C142, the legacy engine since C142b, D-093); a link loop reads as dangling;
  an lstat error other than ENOENT is refused (`cannot check the target`); a link to stdout or stderr on a pipe gets
  the std-stream message.
- **Narrowing items** (REVIEW-C142 (d), D-093): (d)1, the revalidation, and (d)3, the exclusive legacy temporary
  file, implemented in C142b; (d)2, the pinned parent directory, deferred.
- **Legacy behaviour change, owner-approved (D-093):** the legacy engine's save() now creates its temporary file
  exclusively, like the VM: an existing name at the temporary path fails the save ("cannot create temporary file")
  instead of being truncated or written through.
- **"target changed during save" (D-094 L1).** Just before the rename save() checks the destination again; if it
  changed (another file, a new link, a special or read-only file, a file where there was none, a retargeted link) the
  save is refused, `save: target changed during save` (`io.write_failed`), its temporary file removed and nothing
  replaced. Both engines create the temporary file exclusively and fail the save ("cannot create temporary file") if
  the temporary name is already occupied.
- **For users (D-094 L2):** if another process or an editor replaces the target while a save is under way, save()
  may refuse rather than overwrite the changed file. This is deliberate.
- **Windows** (compiled nowhere yet; unverified until Windows CI):

  | save() on Windows (D-094 L3) | Status |
  | --- | --- |
  | Exclusive temporary-file creation (`_wfopen` "wbx") | to verify on Windows |
  | Ordinary regular-file saves (atomic replace, `MoveFileExW`) | to verify on Windows |
  | Refusal of unsupported targets | to verify on Windows |
  | Symlink handling | to verify where Windows supports symlinks |
  | stdout/stderr handle check (console names, handle identity) | needs Windows execution evidence |
  | Target identity re-check before the rename | **NOT IMPLEMENTED / NOT VERIFIED on Windows**: dev/ino are 0 there, so only the type, writability and link checks run |
  | Concurrent adversarial path replacement | not guaranteed (as on every platform) |

  The Unix revalidation tests (the hook-build race cases) are not Windows evidence. Any destructive behaviour of save()
  under ordinary conditions found on Windows is a blocker, to be escalated.
- **Low, infrastructure (D-094 L4):** the test-rt-vm prerequisite lint (scripts/test_integrity_check.py,
  suite-wiring) sees only literal `test-rt-vm:` lines; a prerequisite added through a variable or an included .mk
  file is not detected. Recorded, not extended.
- **Pre-existing, unchanged:** `file(p).open("w")` reads the existing file before `save()` is ever reached, so opening
  a FIFO with no writer blocks at `open`, not at `save()` (the classification cannot reach it; open is out of scope).
- **Pre-existing, unchanged (incremental builds):** a file a page's own pre-build hook writes with `save()` is not a
  dependency of that page, so changing that file outside the hook does not rebuild the page in modified mode (found
  while fixing the CP01-tick flake, D-091 #3; dynamic_reads_review.sh).

## Release-audit items

Findings that are not release blockers by themselves but must be decided in the release audit.

| Item | Status | Record |
| --- | --- | --- |
| A struct instance passed to an async function: `struct(P) { v := 1 }` / `fn[async](af(p)) { return p.v }` / `print(await af(P()))` prints 1 on the VM; legacy errors "async function argument contains a non-transferable resource" (class iv) | **Closed: an existing approved difference** under D-NEXT-refsem #5 (owner 2026-11-01, "Struct instances cross too (legacy refuses)"), confirmed by D-070 #2 (owner 2026-11-11 11:49); compatibility.md:38; no new approval; scope unchanged. The investigation (SAFE-IMPROVEMENT) is retained. Release-audit item kept for Checkpoint 2: the recursive transfer-safety requirements of D-070 #3 are mandatory once struct fields hold more value types | tsv row `difference:async-struct-argument` (approved); rescan-post-cp1.md sections 26 and 28 |
| **Low, test quality** (D-071 #3, owner 2026-11-11 17:11): the file-handle negative control `reentrant-not-detected` (tests/runtime2/cp17/handle_mutants.py, retargeted in C129 after the C57 pattern drift) is detected by the unit tests' 120-second timeout (a missed re-entrancy deadlocks the table mutex), not by a specific assertion; the review notes the same for `limit-setting-ignored` | **Open, before final release certification:** verify that the timeout is deterministic, that the unmodified build completes reliably, and that the hang is caused by the mutant; improve the diagnostic (identify the deadlock directly) if practical, without a broad harness campaign. Not a slot-2 blocker ("Accept the repaired negative control for checkpoint purposes") | decisions.md D-071 #3; REVIEW-C129-SLOT2 section 4; rescan-post-cp1.md section 30 |
| **`make test-runtime2-host-deps`: FAILED** (exit 2) with 24 known refusals unrelated to slot 2, all in read_limit.py, none with an import. By capability group (inventory row, status): `builtin-bytes:open_bytes` 7 (implement); `callable:run` 6 and `callable:cmd` 2 (deferred-named); `callable:read` 3 (implement); `host-handle-flow` 4, `statement:Command` 1 and `type:length` 1 (PENDING-OWNER) | **FAILED, not PASS, and not an approved release deferral** (D-071 #1). Checkpoint acceptance is not blocked; the 24 stay open under their capability groups and on the certification inventory (the recorded fingerprint tests/runtime2/certify-fingerprints/test-runtime2-host-deps.txt, 33 FAILs, contains all 24), still open as release-gate findings (D-075 #1, owner 2026-11-12 01:27), subject to final release qualification. Baseline: the C129 freeze compared a scratch rebuild of the slot-1 sources (sha256 not recorded; deleted at cleanup), with identical 24 FAILs, so the independent review could not reproduce the baseline ("fresh baseline reproduction was unavailable because the Slot 1 binary was not present") | decisions.md D-071 #1; REVIEW-C129-SLOT2 section H; rescan-post-cp1.md sections 29.5 and 30 |
| **P1 release blocker** (D-072 #1, owner 2026-11-11 18:00): `print` / `cat` output order. a.txt `hello` (no newline); `print(1)` / `cat("a.txt")` / `print(2)` with stdout a pipe: legacy writes `1\nhello2\n`, the VM writes `hello1\n2\n`. No import involved | **Implemented and accepted** (C139, the output-order checkpoint, D-077 #5 item 1; accepted by D-079 #1, owner 2026-11-12; docs/runtime-2/checkpoints/output-order.md). Not an approved difference | decisions.md D-072 #1; tsv `defect:print-cat-output-order`; rescan-post-cp1.md section 31 |
| **P2** (D-072 #2): read-mode file handle trace order: legacy traces `open` then `read`, the VM `read` then `open`; output equal | **Under read-only investigation, not approved** | decisions.md D-072 #2; tsv `difference:file-open-read-trace-order` |
| **P1 filesystem safety, open** (F-save; D-073 #1, owner 2026-11-11 22:23, provisional P1): `save()` writes a temporary file and renames it over the target on both engines, so a FIFO, device node, socket, or a symlink to a non-regular file (`/dev/*`, `/proc/self/fd/*` aliases) is destroyed and replaced by a regular file. Demonstrated in this sandbox on 2026-11-11 at 19:12: a probe's `save()` turned `/dev/stdout` into a 2-byte regular file; the coordinator restored the symlink to /proc/self/fd/1 at 22:23 (D-073 #1) and every gate result that depended on it in that window was re-run (rescan-post-cp1.md 32.8) | **Implemented in C142, C142a and C142b** (D-074, D-091 #5, D-093): special targets, symlinks, std streams and read-only files are classified first and refused or resolved, and the destination is revalidated just before the rename (section "save() filesystem safety" above). Supported: safe refusal of prohibited targets under ordinary, non-adversarial conditions, and atomic replacement of permitted regular files. Not guaranteed: race-free operation against concurrent adversarial path replacement (the deferred parent-directory race, (d)2). Pending independent review and owner acceptance | decisions.md D-073 #1, D-077 #5; scratch review INVESTIGATE-SAVE-SPECIAL-TARGETS.md |

