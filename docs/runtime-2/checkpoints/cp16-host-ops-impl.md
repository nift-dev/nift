# CP16 — Host operations: implementation record (slice 1, Host layer)

Status: **in progress, slice 1 of the Host layer** (`runtime-2-cp16-dev`,
local, not pushed). Base: `runtime-2` @`6181549b`. Design: `cp16-host-ops-design.md`
(§1–§12, cherry-picked from `runtime-2-cp16-design` @`d4310bc`). Decisions:
D-028, D-029, D-030 (alternative A), D-031. Owner decision 2026-10-19: start
with the isolated Host-layer components whose contracts are approved, without
depending on uncertified VM interfaces.

Nothing here is reachable from `nift` yet: `src/rt/host/` is built only by the
new test targets (`RT_LINK_SOURCES` is `src/rt/*.cpp`, not recursive). No file
owned by another agent was edited (compiler/VM/natives, `handle.*`, the
legacy files `feature-host-deps` changes).

## 1. What slice 1 contains

| Part | Files | Design |
|---|---|---|
| Error codes and catchability | `host_error.{h,cpp}` | §2.6. One table: code → required status, catchable. |
| Host-op table | `host_op.{h,cpp}` | §2.4/§2.5. Per op: v1 name and emission point (`Pre`, `PostOk`, `PostAlways`, `None`), v2 name, recording rule. |
| Trace channel v1/v2 | `host_trace.{h,cpp}` | §2.4, O2 adopted. v1 byte-identical to `src/HostTrace.h`; v2 behind `NIFT_TRACE_HOST_VERSION=2`. |
| Dependency-recording hooks | `observer.h` | §2.5. **Interface only**; the `PageObserver` waits for `feature-host-deps` (§9 order of work). |
| Path resolution | `path.{h,cpp}` | §3. Legacy `resolve_path` rules; containment only as an opt-in policy. |
| `HostCall` | `host_call.{h,cpp}` | §2. resolve → v1 point → attempt → classify → v2 → record. |
| `run()` options (alternative A) | `run_options.{h,cpp}` | §12.2, D-030. |
| Process model (POSIX) | `process.h`, `process_posix.cpp`, `process_common.cpp` | §11, D-029/D-030. |
| Process runtime | `process_runtime.{h,cpp}` | §11.3/§11.4/§11.9: reaper, termination index, C2 handler, subreaper, orphan sweep. |
| Process cleanup hook | `process_cleanup.{h,cpp}` | §11.3/§11.5, CP17 integration note. |
| Traced launches | `process_call.{h,cpp}` | §2.4 spawn rows (v1 and v2). |
| Windows | `process_win.cpp` | Job-object helpers only, **untested** (§4.3, §11.9). |
| Handle stand-in | `handle_adapter_tmp.{h,cpp}` | **TEMPORARY** (see §5). |

### 1.1 HostCall and the op table

Every Host op is one `HostCall`: `resolve(raw)` (one `Resolved` reused by the
trace, the op and the recording), `begin()` (writes the v1 line only for `Pre`
ops), then exactly one `succeed(res, observation)` or `fail(status, error)`.

* v1 timing comes only from the table, so a native cannot trace at the wrong
  time: reads after success only; `stat` after the query (also `error`);
  `list`/`glob`/`open`/writes/`env-all`/`spawn` before the attempt; ops that
  v1 does not trace never emit v1. `succeed()` without `begin()` is refused
  (`internal.host_call`).
* A path refused by the policy (`--fs-root`, embedder) emits no v1 line
  (legacy validates before tracing) and one v2 line with the error code.
* `fail()` checks the status against the code table: a catchable code
  reported as fatal (or the reverse, or an unknown code) becomes
  `internal.host_call`. A `HostCall` destroyed without an outcome reports
  `internal.host_call` on v2.
* Recording (page renders only; standalone passes no observer) happens after
  success only, per the op's rule. `file().open("rw")` keeps legacy's
  hash-only recording; `open_bytes` (`read-bytes`) and file-backed streams
  (`stream-open`) record as D-028 requires. **The final table is pinned
  against `feature-host-deps` when that branch is integrated** (its fixes
  define what failed/missing opens record).

### 1.2 Trace v2 (O2)

`NIFT_TRACE_HOST_VERSION=2` with `NIFT_TRACE_HOST=<fd>`: one line per Host op,
including the ops v1 omits, written after the attempt:

```
{"v":2,"ctx":"script","op":"save","arg":"/abs/o.txt","res":"error","err":"io.write_failed"}
{"v":2,"ctx":"script","op":"spawn","arg":"sh","res":"launched","argv":["sh","-c","echo a b"],"cwd":"/tmp"}
```

`res` on success is v1's result class where v1 has one, else `ok`; failures
are `res:"error"` plus `err:<code>` (severity and catchability follow from the
code). A process launch failure is a value: `res:"launch_failed"`, no `err`.
Spawn lines carry the argv as a JSON array and the absolute cwd (Q-H11). The
version variable is read once and removed from the environment, like
`NIFT_TRACE_HOST`; an invalid value keeps v1 (tracing must never change other
output). Only one version is active per process; v1 stays the parity channel.

### 1.3 `run()` options (D-030 alternative A)

Keys: `mode` (`capture` | `pipe` | `stream` | `interactive` | `detached`),
`timeout_ms`, `kill_after_ms` (non-negative integers ≤ 2^53−1), `stdin`
(D-034, §6.2). Parsed from a VM-independent
`OptionObject`; the VM wiring converts the object later.

* Unknown keys are fatal **first** (before any other check), with a spelling
  suggestion: case and `-`/`_` folding always suggest; otherwise ≤ 2 edits
  (transpositions count once) and fewer than half the word
  (`run: unknown option 'timout_ms' (did you mean 'timeout_ms'?)`); without a
  close key the message lists the options.
* Value errors: `run: mode must be one of capture, stream, interactive,
  detached` (+ suggestion), `run: mode must be a string, not number`,
  `run: timeout_ms must be a non-negative integer`.
* Incompatible combinations: `detached` + `timeout_ms` (`run: deadline is not
  allowed for a detached process`, §11.7); `kill_after_ms` without
  `timeout_ms` in `capture`/`stream` (nothing could ever cancel the blocking
  run). Detach in a build is a launch-time context check
  (`process.detach_in_build`).
* All are `runtime.invalid_argument`, fatal, not catchable (D-022 today).

### 1.4 Process model (§11)

`ProcessGroup` = one launch of a pipeline. POSIX, `fork` + `execve` (no
`posix_spawn`), exec preparation with legacy's `PreparedProcessPOSIX`
semantics (PATH search in the parent, ENOEXEC → `/bin/sh`, interpreter
detection, the same launch-failure texts), launches serialised with legacy's
`nift_process_launch_mutex`, environment snapshot under
`nift_environment_mutex`.

| Requirement | Implementation |
|---|---|
| launch failure ≠ nonzero exit ≠ timeout ≠ cancel | per stage `StageState` (`launch_failed`, `exited`, `signaled`, `lost`, `not_started`); group `GroupState` (`exited`, `launch_failed`, `cancelled`, `timed_out`, `lost`, `detached`); exec/setup failures reported over a CLOEXEC pipe; parent-side failures (pipe/fork/capture) are `launch_failed` with nothing left running |
| C3 one group per managed pipeline | both parent and child `setpgid`; all stages join the first stage's group |
| deadlock-free draining | one `poll()` loop over stdout, stderr, stdin (non-blocking) and Linux pidfds (tick fallback); completion = every stage reaped, never pipe EOF |
| deadlines | absolute at launch; poll timeout = time left; expiry starts §11.4 |
| cancellation Child / Pipeline / Tree | `kill(pid)` / `kill(-pgid)` (all stages at once) / Pipeline + `/proc` descendants; SIGTERM, grace (`kill_after`, default 2,000 ms), SIGKILL, bounded wait (5,000 ms), then `Lost` (handed to the reaper) |
| stdin closed and output still drained during cancellation | step 1 of §11.4 |
| bounded capture while running (D-028) | per stream ≤ `capture_limit` (the `NIFT_READ_LIMIT` value supplied by the caller); pipe modes check on every read, `FileCapture` checks the file sizes on a 20 ms tick; exceeding it cancels the group and reports `capture_limit_exceeded` + `run: captured stdout of <prog> exceeds the read limit of N bytes (NIFT_READ_LIMIT)` (host-deps wording), output released, never truncated |
| modes | `FileCapture` (legacy default: anonymous files, seekable stdout), `PipeCapture` (D-011 opt-in), `Stream` (flush callback, then inherit), `Inherit`, `Interactive` (non-blocking handle: `write`, `close_stdin`, `read_line`, `read`, `wait`, `cancel`, bounded buffers with backpressure; `wait()` drains into the capture buffer), `Detached` (`setsid` + double fork, stdio to `/dev/null`, pid record) |
| leftovers in pipe modes | after the last stage is reaped: read what is there; members still in the group are cancelled (silently); a holder in another session (group empty) cannot keep the call open |
| C1 | `FileCapture` keeps legacy's behaviour for the call itself (descendants not waited for); `begin_terminate()` at context end terminates leftover group members silently |
| C2 | `runtime::install_signal_cleanup()`: SIGINT/SIGTERM handler writes to a self-pipe; a watcher thread runs SIGTERM → grace → SIGKILL → bounded wait over the termination index, then Nift dies of the same signal (exit status 130/143 in a shell); detached processes untouched |
| Linux PDEATHSIG | managed stages get `PR_SET_PDEATHSIG(SIGKILL)` (checked against the parent pid after setting) |
| Linux subreaper | `runtime::enable_subreaper()`; `runtime::sweep_orphans()` for root-context ends terminates and reaps reparented orphans that belong to no live group and no detached session |
| no zombies | stages reaped by pid only (never `waitpid(-1)`, so legacy's and other code's children are never stolen); Lost stages, detached pids (under the subreaper) and orphans go to a lazily started reaper thread |
| terminal (§11.8) | a blocking call with terminal stdin makes the group the foreground group for the call (`tcsetpgrp` in parent and child, SIGTTOU ignored meanwhile) |
| detach rules | refused in builds (`process.detach_in_build`), with a deadline, and (slice 1) for pipelines (`process.unsupported`) |

The termination index records live groups (pgid, unreaped stage pids) only so
that C2 and the orphan sweep know what to signal or spare; it decides nothing
about ownership.

### 1.5 Cleanup hook and context ends (CP17 integration note)

`process_cleanup(group, ProcessRelease)` is the per-kind hook body; the four
reasons mirror CP17's `ReleaseReason` (`Explicit`, `Abandoned`, `ContextEnd`,
`Shutdown`). As CP17's record asks, the hook never waits out a grace period:
it **starts** the §11.4 sequence and returns. `ContextCleanup` (RAII, per
thread) is what CP16's context-end path wraps around the table's
`close_context()`: hooks add their groups to it and `wait()` drives them all
against **one** deadline (tested: three groups that ignore SIGTERM end in one
500 ms grace period, not three). It also carries what CP17's `CleanupEvent`
does not: the context's label for the warning, and whether the context ended
on an error (no warning then, §11.3). Abandoned and explicitly released groups
are finished asynchronously by the reaper.

## 2. Deferred, with reasons

| Item | Why deferred |
|---|---|
| VM wiring (natives, `Context.handles`, W12–W24, option conversion from `rt::Value`) | CP13–CP15 integration owns `src/rt/compiler*`, `vm*`, `natives/*`; not certified yet |
| `PageObservation` extraction, `PageObserver` | waits until `feature-host-deps` is integrated (design §9; D-028) |
| FS/EN/TI/RN Host implementations (`Host` interface §2.2, `FaultHost`) | slice 2; needs host-deps' `ReadLimit.h` and FileSystem changes to land first, so reads use the shared bounded-read code instead of a copy |
| Reading `NIFT_READ_LIMIT` | `capture_limit` is a parameter; the setting is parsed by host-deps' `ReadLimit.h` (not integrated) |
| Windows `ProcessGroup` | no Windows toolchain here; job-object helpers written, untested; every entry point reports `process.unsupported` |
| Detached pipelines | not needed by alternative A (`run()` is one command); refused loudly |
| `auto_superseded_pass`, `build_*` lifetime tests, `terminal_foreground_handoff` (pty), `exit_sigint`, macOS/Windows rows | need the build engine/CLI wiring, a pty harness, or other platforms |
| Linking `src/rt/host` into `nift` | nothing calls it yet; added when the first native uses it |
| Checking `shutting_down()` before a build or page render | the build engine is not wired to the Host layer yet; the flag is exported for it (§6.4) |

**CP22 items (recorded, D-034 item 6).**
* **PDEATHSIG is per thread on Linux.** `PR_SET_PDEATHSIG(SIGKILL)` fires when
  the *thread* that forked the stage exits, not when Nift exits. Harmless
  while every launch happens on the VM thread (W18 refuses threads), but once
  CP22 lets workers launch processes, a worker that ends before its child would
  kill that child. CP22 must launch from a long-lived thread (for example the
  host loop) or drop PDEATHSIG for worker launches.
* **The host loop is CP22's scheduler candidate.** It is a generic one-thread
  loop (monotonic timers + one-shot fd watches, sleeping in `poll()` until the
  next event). CP22 can host its scheduler on it or replace it behind the same
  three calls (`loop_at`, `loop_when_readable`, `loop_cancel`); the process
  model does not depend on anything else.

## 3. Tests (run 2026-10-19, Linux 6.18, GCC 13.3, 2-vCPU shared host)

| Run | Result |
|---|---|
| `host-layer-unit` (`make test-rt-host`, first half) | 497 checks, 0 failures |
| `host-process-unit` contained (`run_contained.sh`: `ulimit -f` 1 GiB, `ulimit -v` 4 GiB, `timeout 600`, own session, tagged-process cleanup) | 1238 checks, 0 failures; 16 consecutive clean runs after the last fixes (one earlier run failed once in `leftovers_c1_and_holders` and was not reproduced in 28 runs; a diagnostic print stays in that test) |
| `make test-rt-host-sanitize` (ASan+UBSan, `detect_leaks=1`, `halt_on_error=1`; no `ulimit -v` under ASan) | layer 497/0, process 1238/0 |
| `make test-rt-host-tsan` (ThreadSanitizer, `halt_on_error=1`) | process 1238/0, no reports |
| CP17 swap trial (scratch merge of `runtime-2-cp17-dev` @`11e98d2`, ASan+UBSan) | `host_cp17_binding` 27 checks, 0 failures |

The two unit binaries were compiled directly at `nice -n 19` (about 30 s
of CPU each build) instead of through `heavy.sh`: the gate queue held a
priority `make certify` and four jobs; nothing heavier than these two links
was run.

`tests/rt/host_process_unit.cpp` (orphan detection: every test process
carries `NIFT_TEST_PROC_TAG=<tag>`, survivors found through
`/proc/*/environ`; zombies through `/proc` ppid/state; descriptor leaks
through `/proc/self/fd`):

| Test | §11.12 row(s) | Asserts |
|---|---|---|
| `launch_vs_exit_each_mode` | `launch_vs_exit_each_mode` | missing program → `launched:false`/127 in FileCapture, PipeCapture, Inherit, Interactive; `exit 3` → launched/3; 126 permission denied; interpreter not found; cwd setup failure 126; middle stage missing in a pipeline keeps its `launch_failed` stage; 143 + `SIGTERM` for a signaled stage |
| `capture_basic` | — | stdout/stderr capture, stdin text, stderr of all stages, seekable stdout in FileCapture (legacy), pipe in PipeCapture, own pgid (C3), redirections (truncate/append/stdin file), env overrides |
| `own_blocking_reaped` | `own_blocking_reaped` | 300 runs, no zombie, no fd leak, no survivor |
| `drain_both_large` | `drain_both_64m` (16 MiB per stream) | 16 MiB on each stream interleaved, exact bytes, in PipeCapture, FileCapture and Interactive (`wait()` without reading) |
| `drain_stdin_before_read` | `drain_stdin_before_read` | 1 MiB to stderr before reading 1 MiB stdin, blocking and handle API; write after close → `process.stdin_closed` |
| `interactive_lines` | §11.10 | `write`/`read_line`/`close_stdin` (idempotent)/EOF/`wait` repeatable/`cancel` after exit = no-op |
| `deadlines` | `deadline_sleeping`, `deadline_blocked_read`, `deadline_blocked_write`, `deadline_ignores_term` | `timed_out`, 143 or 137 (`SIGKILL` after `kill_after`), returns within deadline + grace + bound; no deadline when none was asked |
| `default_grace_2000ms` | D-030 default | SIGTERM-ignoring child: SIGKILL after 1.9–5 s |
| `cancellation` | `cancel_pipeline_all_stages`, `cancel_child_scope`, `cancel_idempotent` | `yes \| gzip \| cat`: all three `SIGTERM`; Child scope signals only stage 1; Tree scope reaches a `setsid` descendant |
| `exit_cancel_race` | exit/cancel races | 200 × start `true` + immediate cancel: no zombie, no survivor |
| `capture_limit_loud` | `capture_limit_loud` | 8 MiB vs 1 MiB limit in PipeCapture and FileCapture: `capture_limit_exceeded`, message, output released, group cancelled (< 3 s although the child sleeps 30 s); exactly-at-limit is fine; stderr limit |
| `leftovers_c1_and_holders` | `own_bg_descendant_context_end` (script), `drain_bg_holds_pipe` | `sh -c "sleep 30 &"` survives the call, is terminated silently at context end; `(sleep 30 &); echo x` in PipeCapture returns promptly with the holder terminated |
| `adapter_lifecycle` | `abandon_running_pre_cp17` (via stand-in), `abandon_exited`, `exit_normal_unwaited`, `exit_error_unwaited` | abandoned running → asynchronous (< 200 ms) + exact warning; abandoned exited → silent; explicit release → no warning; three groups at context end → one warning each, **one** 500 ms grace for all; error end → no warning; detached never touched, own session |
| `detached_rules` | `build_detach_refused` (Host side) | `process.detach_in_build`, nothing launched; deadline refused; detached launch failure is a value |
| `low_fd_limit` | `fd_no_leak_low_limit` | `RLIMIT_NOFILE` = open + 3 → `launched:false`, `cannot create pipe: Too many open files`, no leak; 300 launches under a tight limit, no leak |
| `trace_spawn` | §2.4 | v1 spawn lines before launch (joined argv, cwd as spelled); v2 after the attempt (argv array, absolute cwd, `launch_failed`); refused request → v2 error only |
| `sigterm_to_nift_c2` | `exit_sigterm` (C2) | SIGTERM to a Nift process blocked in `run()` with a SIGTERM-ignoring child: child killed, detached process alive, Nift dies of SIGTERM within grace + bound |
| `sigkill_pdeathsig_linux` | `exit_sigkill_linux` | direct stage dies when Nift is SIGKILLed |
| `escapees_subreaper` | `escape_setsid_linux` | holder in another session does not hang PipeCapture; escapee reparented to Nift, untouched by group cleanup, removed by the orphan sweep; detached process spared and later reaped |
| `pipe_mode_suite` | D-034 pipe | 16 MiB stdout + 16 MiB stderr at once; descendant holding the pipe terminated, prompt return; launch failure / exit 4 / timeout / limit as distinct states; capture still a seekable file, pipe a pipe; cancel from another thread while the owner is blocked in `wait()`; context-end cleanup |
| `stdin_suite` | D-034 stdin | empty text; shell syntax arrives literally; 8 MiB in while 8 MiB comes out (pipe and capture); early exit (`head -c 10`, `exit 3`) keeps the result, incomplete delivery, no error; `null`; deadline and cancellation while feeding; `{"file"}` |
| `deadline_suite` | D-034 deadlines | idle handle, child ignoring SIGTERM: finished with nobody calling it (SIGKILL after the grace); four concurrent idle deadlines expire in order; expiry while the owner is blocked in `read_line()`; expiry recorded though the child exits during the grace; nothing left armed afterwards |
| `deadline_races` | D-034 race safety | 30 rounds of deadline + own exit + cross-thread `cancel()` + context cleanup at once |
| `shutdown_refuses_launches` | §6.4 | no new launch while shutting down (`host.shutting_down`); HostCall refusals are in the layer test |
| `cleanup_failures_reported` | §6.5 | unkillable process (test hook): error-ended context → no warning, failure reported; normal end → warning + failure; error-ended context with a killable process → silent |

Not run (deferred, §2): `auto_superseded_pass`, `build_page_unwaited`,
`build_hook_bg`, `exit_sigint`, `terminal_foreground_handoff`,
`deadline_lost_dstate`, `stale_handle_internal`, macOS/Windows rows. The
21 parser-checked syntax examples (`tests/runtime2/cp16/syntax/`) are kept
unchanged as acceptance fixtures for the VM wiring (D-030); they need the
`run()` native to run on the VM.

## 4. Handles: the temporary stand-in and CP17

CP17 committed its table on `runtime-2-cp17-dev` during this slice
(`344108c`, `11e98d2`: `src/rt/handles/`, uncertified, not on `runtime-2`).
Its interface **fits** the process hook:

* `KindSpec{"process", hook, "process.detach_in_build"}`, the hook receives
  `CleanupEvent{kind, handle, owner, reason, resource}` and may keep the
  resource to finish asynchronously; `ReleaseReason` {Explicit, Abandoned,
  ContextEnd, Shutdown} maps one to one onto `ProcessRelease`.
* Counted handles + `HandleRef` give abandonment; `detach()` is refused in
  the build family with the per-kind code.

What CP17's event does not carry (the context's label for the warning text,
and whether the context ended on an error, §11.3 "no extra warning") is
supplied by CP16's own context-end path (`ContextCleanup`), which is also
where CP17's note asks CP16 to wait once per context. Detach-in-build is
checked before launching from the context kind CP16 itself opens (CP17's
integration step 3), so nothing is launched and then refused. The root-context
orphan sweep runs in that same path after `close_context()`. None of these
needs a CP17 change.

Evidence: a scratch merge of `runtime-2-cp17-dev` into this branch (clean
merge) with `tests/runtime2/cp16/cp17-swap/host_cp17_binding.cpp` registering
the unchanged hook body on the real `HandleTable`: abandonment via
`HandleRef`, three-group context end waited once, build-family detach refused,
detached handle untouched by `shutdown()` — 27 checks, 0 failures under
ASan+UBSan.

The stand-in (`handle_adapter_tmp.{h,cpp}`) copies CP17's vocabulary, is
used by tests only, and is deleted in the swap commit: rename
`tmp_handles::` → `handles::`, `TemporaryTable` → `HandleTable`, move the
binding test into `tests/rt/`. The swap is not committed here because CP17's
code is not on `runtime-2` yet (committing it would make this branch carry
CP17's uncertified history).

CP17 added `HandleTable::context_in_build()` (@fbdca07) on request; the swap
sets `RunRequestContext::in_build` from it, so detach-in-build is refused
before anything is launched. D-034 item 7: the swap stays uncommitted until
CP17 is integrated. The swap test was updated for `ContextCleanup::wait()`
now returning the list of cleanup failures.

## 5. Owner answers (D-034) and where they are implemented

| # | Question | D-034 answer | Implemented in |
|---|---|---|---|
| 1 | capture vs pipe | `mode: "pipe"` opt-in; `"capture"` unchanged | §6.1 |
| 2 | `stdin` values | `"inherit"`, `"null"`, `{"file"}`, `{"text"}` | §6.2 |
| 3 | idle-handle deadlines | enforced now, from the reaper/host loop | §6.3 |
| 4 | error-ended contexts | no routine warning; genuine failures still reported | §6.5 |
| 5 | SIGTERM window | accepted, bounded, documented | §6.4 |
| 6 | `test-rt-host` in `make test` | yes | `Makefile` (`test: test-rt-host`) |
| 7 | PDEATHSIG | CP22 item | §2 |
| 8 | CP17 swap | not yet; use `context_in_build()` (@fbdca07) | §4 |

## 6. D-034 implementation

### 6.1 Modes: capture, pipe, stream, interactive, detached

| | `capture` (default) | `pipe` | `stream` | `interactive` | `detached` |
|---|---|---|---|---|---|
| child's stdout/stderr | anonymous regular files (seekable) | pipes | Nift's own stdout/stderr (after flushing Nift's buffers) | pipes | `/dev/null` |
| what `run()` returns | result with `stdout`/`stderr` | same | result, `stdout`/`stderr` empty | `Process` handle | `Process` handle (pid record) |
| completion | every stage reaped | every stage reaped; then leftovers holding the pipe are terminated | every stage reaped | `wait()` | never waited |
| background descendants | not waited for; terminated when the context ends (C1) | terminated as soon as the stages are reaped (they would keep the pipe open) | as capture | as pipe, once cancelled or waited | not managed |
| output bound | `NIFT_READ_LIMIT` per stream, checked on a 20 ms tick while running and on read-back | checked on every read | none (nothing is retained; writing to Nift's stdout gives backpressure) | per-stream buffer with backpressure; `wait()` drains up to the read limit | — |
| best for | today's programs; tools that seek or stat their stdout; compatibility | large or fast output, stdout and stderr together, prompt return | build tools whose progress the user should see | dialogue with a process | servers in standalone scripts (refused in builds) |
| costs | two temporary files per call; output written to disk first | a child that keeps background helpers alive loses them; children see a pipe, not a file | output cannot be inspected | the script must read, or `wait()` | Nift cannot capture its output or wait for it |

Outcomes are distinct in every blocking mode: `launch_failed` (with `launched:
false`, 127/126, `error`), `exited` with the exit code, `timed_out`,
`cancelled`, `limit_exceeded` (`capture_limit_exceeded` names the stream;
output released, never truncated) and `lost`. Results of calls without the new
options keep today's fields; the new ones (`state`, `timed_out`, `cancelled`,
`signal`, `stdin_*`) are for results of calls that use them (§11.10).

### 6.2 stdin

| Form | Behaviour |
|---|---|
| omitted, `"inherit"` | today's behaviour: the child inherits Nift's stdin (terminal foreground handoff for blocking calls, §11.8) |
| `"null"` | `/dev/null` |
| `{"text": s}` | a string, written through a pipe by the same event loop that drains stdout/stderr (no deadlock for any size). Never through a shell: the bytes arrive literally. Bytes are refused (`stdin.text must be a string, not bytes`); binary stdin is a later byte form or a stream handle |
| `{"file": p}` | read by the Host through `HostCall` (op `read`, so trace v1/v2 and dependency recording in builds), with **normal OS path semantics** (`link/../x` is what the OS opens; the canonical path is what `--fs-root` checks and what is recorded), bounded by the read limit (`limit.exceeded` naming `NIFT_READ_LIMIT`, never truncated); then fed exactly like `{"text"}`, so the child receives the bytes that were recorded. Missing file or a directory: `io.open_failed` (catchable). Detached processes open the file themselves (they are never connected to Nift) |

Delivery: an early exit or a closed stdin is not an error. The result keeps the
process outcome, with `stdin_complete: false` and how much was written. Only a
genuine write failure (any errno other than EPIPE) is reported, in
`stdin_error`. Cancellation and deadlines close stdin first (§11.4 step 1).
Mode rules: `stdin` is refused with `interactive` (the process reads what
`write()` sends) and `"inherit"`/`{"text"}` with `detached`.

One stdin mechanism: CP19/CP20 `< file` redirections and `cmd().stdin(path)`
keep `SpawnStage::stdin_path` (the child opens the file, as today); the option
forms use the same `StdinMode` slot.

### 6.3 Deadlines

* Monotonic clock (`steady_clock`); the deadline is fixed at launch.
* **A deadline marks when cancellation starts**, not when everything is gone:
  SIGTERM to the whole pipeline at the deadline, SIGKILL after the grace period
  (`kill_after`, default 2,000 ms), then a bounded wait (5,000 ms) before
  `lost`. Cleanup can therefore finish up to about grace (+ bound) after the
  deadline.
* The expiry is recorded (`timed_out`, state `timed_out`) even when the process
  exits by itself during the grace period.
* Enforced whether or not the handle is used: when a call returns and the group
  is not finished, it arms a host-loop timer for its next step (deadline,
  SIGKILL, bound) and one-shot pidfd watches (so an exit is reaped at once).
  The loop thread sleeps in `poll()` until the nearest timer or watch — no
  periodic polling. Abandoned and explicitly released handles are adopted by
  the reaper and finished by the same timers.
* Race safety: every group operation holds the group's lock; the event loop
  releases it only while in `poll()`. A timer that finds the group busy retries
  once after 5 ms (the holder re-arms when it lets go). Tested: 30 rounds of
  deadline, own exit, `cancel()` from another thread and context cleanup at the
  same moment — every group settles, no zombie, no survivor.

### 6.4 SIGINT/SIGTERM: shutting down (bounded window)

From the moment the signal arrives (`install_signal_cleanup()`):
1. Nift is **shutting down**: `ProcessGroup::start` refuses new launches
   (`host.shutting_down`, fatal); `HostCall::begin()` refuses every externally
   visible op (writes, copies, moves, removals, spawns). Reads stay possible so
   running code can reach its shutdown path. The build engine is to check
   `runtime::shutting_down()` before starting a build or a page render (wired
   with the build engine).
2. A watcher thread sends SIGTERM to every managed group, waits up to the grace
   period, sends SIGKILL, waits up to the bound; detached processes are not
   touched.
3. Then Nift ends with the same signal (exit status 130/143 in a shell). If
   the program reaches its end first, an atexit handler waits for step 2
   (bounded) and re-raises, so the status is signal-derived either way.

Window: running (non-blocked) script code may continue for at most grace +
bound (2 s + 5 s by default) after the signal. This is a **normal-shutdown
guarantee only**: SIGKILL or a crash of Nift runs none of it (Linux PDEATHSIG
still kills direct stages; everything else is the §11.9 table). There is no
general interruption mechanism for script code.

### 6.5 Contexts that end on an error

The routine warning (`was still running when <context> ended; terminated`) is
suppressed; the context's own error stays the primary diagnostic. Genuine
cleanup failures are still reported, separately, through the failure sink
(stderr by default), after the error:
`error: cleanup: process 'x' (pid N) could not be terminated (still running
after SIGKILL); left to the reaper` (or `... within N ms ...` when a group did
not settle in time). The same report appears for normal context ends and for
abandoned handles finished by the reaper. Tested with SIGKILL suppressed by a
test-only hook (`-DNIFT_HOST_TEST_HOOKS`; not compiled into anything else).
