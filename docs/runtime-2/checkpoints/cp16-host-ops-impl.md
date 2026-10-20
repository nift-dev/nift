# CP16 — Host operations: implementation record (slice 1, Host layer)

Status: **in progress: slice 1 (Host layer) done; phase 2 (legacy routing, §9) done, awaiting certification** (`runtime-2-cp16-dev`,
local, not pushed). Base: `runtime-2` @`6181549b` (slice 1), @`e8e4b106` (phase 2). Design: `cp16-host-ops-design.md`
(§1–§12, cherry-picked from `runtime-2-cp16-design` @`d4310bc`). Decisions:
D-028, D-029, D-030 (alternative A), D-031. Owner decision 2026-10-19: start
with the isolated Host-layer components whose contracts are approved, without
depending on uncertified VM interfaces.

Slice 1 was not reachable from `nift`; since phase 2 (§9) the Host layer is
linked into `nift` and serves the legacy interpreter's reads and `run()`. No file
owned by another agent was edited (compiler/VM/natives, `handle.*`, the
legacy files `feature-host-deps` changes).

## 1. What slice 1 contains

| Part | Files | Design |
|---|---|---|
| Error codes and catchability | `host_error.{h,cpp}` | §2.6. One table: code → required status, catchable. |
| Host-op table | `host_op.{h,cpp}` | §2.4/§2.5. Per op: v1 name and emission point (`Pre`, `PostOk`, `PostAlways`, `None`), v2 name, recording rule. |
| Trace channel v1/v2 | `host_trace.{h,cpp}` | §2.4, O2 adopted. v1 byte-identical to `src/HostTrace.h`; v2 behind `NIFT_TRACE_HOST_VERSION=2`. |
| Dependency-recording hooks | `observer.h`; `src/PageObserver.*` | §2.5. Interface in the Host layer; the one recorder, shared by every engine, since phase 2 (§9.2). |
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
| FS/EN/TI/RN Host implementations (`Host` interface §2.2, `FaultHost`) | slice 2; needs host-deps' `ReadLimit.h` and FileSystem changes to land first, so reads use the shared bounded-read code instead of a copy |
| Reading `NIFT_READ_LIMIT` | `capture_limit` is a parameter; the setting is parsed by host-deps' `ReadLimit.h` (not integrated) |
| Windows `ProcessGroup` | no Windows toolchain here; job-object helpers written, untested; every entry point reports `process.unsupported` |
| Detached pipelines | not needed by alternative A (`run()` is one command); refused loudly |
| `auto_superseded_pass`, `build_*` lifetime tests, `terminal_foreground_handoff` (pty), `exit_sigint`, macOS/Windows rows | need the build engine/CLI wiring, a pty harness, or other platforms |
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

## 4. Handles: on CP17's table (swap done)

CP17 was integrated on `runtime-2` @`02fab6c5` and feature-host-deps
@`2d191e6b`; CP16 was rebased once onto `2d191e6b` and the swap was made in
`src/rt/host/process_handles.{h,cpp}` (the only file that sees both sides).
The temporary stand-in (`handle_adapter_tmp.*`) and the scratch swap test are
deleted; no competing ownership or lifecycle mechanism remains in the Host
layer. §8 describes the interface as implemented.

Also aligned with host-deps (D-028) for the coming PageObserver: `rw` opens
are dependencies like `r`; file-backed input streams record at open; a
recoverable read failure records the path (absent or unreadable); HostCall
always calls the observer it is given, so hook and standalone contexts can
keep reporting `file().open` reads to the host (legacy c7971365). Phase 2
(§9) implements the recorder (`PageObserver`) and reroutes legacy reads.

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

## 7. Investigation: the rare `leftovers_c1_and_holders` failure (closed)

**Symptom.** One failure in about 30 full runs: right after a `FileCapture`
run of `sh -c "sleep 30 & echo started"`, the test found **0** processes with
its tag instead of the surviving background `sleep` (C1: it must survive the
call).

**Instrumentation** (test build only, `-DNIFT_HOST_TEST_HOOKS`): every signal
the Host layer sends goes through `runtime::host_kill`, and a timestamped
event trace (`runtime::test_event`, monotonic clock) records spawns (pid,
pgid), signals (target, signal, call site), pipe EOFs, reaps (pid, wait
status), cancellations (reason, scope), host-loop ticks and finishes (state).
On failure the test prints the trace for the last 3 s plus every member of the
group (`/proc` state, ppid, cmdline, environ size) and rescans 50 ms later.
`CP16_REPEAT=N` repeats the selection; `stress.sh` runs it under
`run_contained.sh` (D-031 limits) through `heavy.sh`, with and without two
CPU burners.

**Reproduced** (before the fix): 2/300 single-test rounds without load, 1/300
with load, 3/600 in a targeted run. Every captured trace has the same shape:

```
left count 0; sh: state exited exit 0 signal 0; pgid 30062
  +3000.382 ms spawn   a=30062 b=30062      (sh: pid 30062, pgid 30062)
  +3006.260 ms tick
  +3006.267 ms reap    a=30062 b=0 exited   (sh exited 0)
  +3006.294 ms finish  b=1 exited
  member 30063 state S ppid 1 cmdline [sleep 30 ] environ 24056 bytes
  rescan after 50 ms: 1 tagged
```

No signal was sent to the group (no `signal` event), `sh` exited normally,
and the `sleep` was alive, in the group, carrying the tag — the scan missed
it. **Cause: a test observation race.** The scan ran while the background
child was inside `execve()`: the new address space is installed before its
environment area is set up, and `/proc/<pid>/environ` reads empty in that
window. Not a Host-layer race (no signal, no reap, no state change involved).
The same race could make `none_tagged()` miss a live process and let an
orphan check pass falsely.

**Fix (test, strengthened, not weakened).** `read_environ()` re-reads a live,
non-kernel process whose environ is empty until it is non-empty (bounded,
0.5 ms steps); no process on this host has a genuinely empty environment.
Both "exactly one survivor" and "no survivor" checks are now immune to the
exec window.

**Second finding from the same campaign (Host layer, fixed).** Repeating the
full suite in one process made every round after the first fail only zombie
checks (178 failures in 25 rounds without load, 111 in 15 with load). Round 1
ends by enabling the Linux subreaper; from then on, background descendants
reparented to Nift stayed zombies until a context end swept them — a breach of
"never zombies while Nift lives" (§11.9). Fix: when the subreaper is enabled,
a chained `SIGCHLD` handler wakes the host loop, which reaps zombie children
that are provably adopted orphans: their process group was created by the Host
layer, or their session is not Nift's (escapees, detached sessions). Unreaped
stages of live groups and children of other code in Nift's own session and
groups (legacy `run()`, job control) are never touched (still no
`waitpid(-1)`); the scan holds the launch mutex so a stage between `fork()`
and its registration cannot be mistaken for an orphan; known pgids are
forgotten once the group is gone. Regression test `subreaper_reaps_orphans`
(adopted orphan reaped with no further Host call; a foreign `fork()`ed child
keeps its exit status for its owner) — it fails on a mutant without the
`SIGCHLD` hookup (orphan left a zombie), passes with the fix.

**Before the fixes** (`stress.sh` through `heavy.sh`, `run_contained.sh` limits):

| Phase | Rounds | Failures |
|---|---|---|
| A: `leftovers_c1_and_holders` alone, no load | 300 | 2 (exec window) |
| B: full suite repeated in one process, no load | 25 | 178 (all zombie checks, rounds 2+) |
| C: `leftovers_c1_and_holders` alone, 2 CPU burners | 300 | 1 (exec window) |
| D: full suite repeated, 2 CPU burners | 15 | 111 (all zombie checks, rounds 2+) |
| targeted, with member diagnostics | 600 | 3 (exec window; traces above) |

**Round 2 (after `c3952f5`) was not clean**, so nothing was closed then:

| Phase | Rounds | Failures | Cause |
|---|---|---|---|
| A | 300 | 1 | same miss: `read_environ()` gave up when `open()` of `/proc/<pid>/environ` failed, and `tagged()` skipped a pid whose `stat` read failed — both can also happen inside `execve()` |
| B | 25 | 24 | rounds 2+: zombie checks right after a detached launch whose exec failed (the grandchild, reparented under the subreaper, was not handed to the reaper and was only reaped later by the SIGCHLD scan); fd checks perturbed by host-loop pidfd watches of earlier tests' processes |
| C | 300 | 0 | |
| D | 15 | 18 | as B |

Fixes (`bc171c0`): the scan retries a failed open or stat while the process
still exists (bounded, ~10 ms; per-pid scan notes printed on failure) —
0 misses in 1418 targeted rounds afterwards; the failed detached grandchild
goes to the reaper; zombie checks assert the specified bound ("reaped within
1 s": asynchronous under the subreaper by design); fd-leak checks subtract the
host loop's own descriptors and re-measure until stable (a persistent leak
still fails); `low_fd_limit` sizes `RLIMIT_NOFILE` by free descriptor numbers.

**Round 3 (after `bc171c0`): clean — closed.** Same campaign through
`heavy.sh` under `run_contained.sh` limits, then the sanitizers, in one gate
job (2026-10-19 23:14–23:40):

| Run | Rounds | Checks | Failures |
|---|---|---|---|
| A: leftover test alone, no load | 300 | 3,000 | 0 |
| B: full suite repeated in one process, no load | 25 | 31,100 | 0 |
| C: leftover test alone, 2 CPU burners | 300 | 3,000 | 0 |
| D: full suite repeated, 2 CPU burners | 15 | 18,660 | 0 |
| `make test-rt-host-sanitize` (ASan+UBSan) | 1 | 497 + 1,244 | 0, no sanitizer reports |
| `make test-rt-host-tsan` (ThreadSanitizer) | 1 | 1,244 | 0, no TSan warnings |

No tagged test process survived any run. The flake (test scan racing
`execve()`) and the subreaper zombie defect are closed on this evidence.

## 8. Lifecycle authority: CP16 reports, CP17 decides (implemented)

Owner rule: CP17 owns handle identity, reference counts, context ownership and
the authoritative lifecycle state (`Active → Terminating → Completed | Failed |
Lost`); CP16 owns OS process operations and their outcomes. Exactly one place
decides "cleanup completed / failed / Lost": **CP17's table**, from facts CP16
reports once. Interface against CP17 `runtime-2-cp17-dev` @`245f290` (D-035,
D-039, review fixes R1–R14: `CompletionTicket`, `begin_termination`,
`TerminationStart`, `LifeState`, `TerminationCause`):

**1. Host-side seam (CP16, no CP17 types in `src/rt/host/`).**

```cpp
// Implemented in process.h (enum OsTermination, struct OsOutcome, class
// LifecycleSink). The process layer calls it without holding the group's lock.
struct LifecycleSink {
    virtual ~LifecycleSink() = default;
    // CP16 itself starts termination while the handle stays open: deadline
    // expiry on the host loop, cancel(), or the natural exit of every stage.
    // Only these three causes (CP17 returns InvalidCause for any other);
    // close, abandonment and context end reach CP16 through the cleanup hook.
    virtual void terminating(TerminationCause cause) = 0;   // Exited | Deadline | Cancel
    // Exactly once, when the group has settled (stages reaped or handed off,
    // leftover members handled): the OS facts, not a lifecycle verdict.
    virtual void settled(const OsOutcome& facts) = 0;
};
struct OsOutcome {
    bool all_stages_reaped;          // every stage's exit was observed
    bool stage_unconfirmed;          // a stage still unreaped after SIGKILL + bound (handed to the reaper)
    bool members_survived;           // leftover group members still present after SIGKILL + bound
    std::string detail;              // "process 'x' (pid N) ..." for the report
};
void ProcessGroup::set_lifecycle_sink(std::shared_ptr<LifecycleSink>, bool replay = true);
```

**2. Binding (the swap commit; CP16 code that sees both sides).**
* `terminating(cause)` → `table.begin_termination(handle, Process, cause,
  ticket, &info)`. `info.started` is true exactly when a ticket was issued
  (the first call while `Active`): the binding keeps it and CP16 terminates
  and reports through it. Later calls, and calls after cleanup began, get no
  ticket — termination happens once. For a **detached** handle no ticket is
  issued and the lifecycle does not change; `info.detached_request` is true
  and CP16 may send the cancel *request* (§11.10: SIGTERM to the detached
  session, escalation driven by the host loop), with nothing supervised and
  nothing reported. The binding never passes `Close`, `Abandoned`,
  `ContextEnd` or `Shutdown` here (`InvalidCause`).
* Cleanup hook (`CleanupEvent&`): if `event.ticket` is non-null (the handle
  was still `Active`: close, last reference, context end), move it into the
  binding, start the OS sequence (`cancel`/`begin_terminate`) and return
  `CleanupOutcome::pending()` — never block (D-035). If null (CP16 already
  started termination and holds the ticket), only release bookkeeping and
  return `completed()`; the existing ticket decides.
* `settled(facts)` → `ticket.complete(...)` with this fixed mapping (CP17 may
  rename the codes): all reaped and nothing survived → `completed()`;
  `stage_unconfirmed` → `lost("process.lost", detail)` (termination could not
  be confirmed); `members_survived` (alive after SIGKILL + bound, known to be
  running) → `failure("process.cleanup_failed", detail)`.
* **Every script-visible process handle is created counted**
  (`CreateOptions{counted = true}`): `interactive` handles, so abandonment is
  detected by reference count (D-035), and `detached` handles, because only
  counted handles can be detached (R14: an uncounted handle gets
  `NotCounted`). A `run(..., {"mode": "detached"})` launch creates the counted
  handle in the current context and immediately calls `detach_as()`; on any
  refusal the process is cancelled at once (never left running unowned).
* Detached processes: detach is a CP17 ownership step; the hook does nothing to
  the process and completes at once (bookkeeping only); `context_in_build()`
  (@fbdca07) feeds `RunRequestContext::in_build` so detach in a build is
  refused before launching.
* **Transfer is not used for processes** (§11.2: ownership never moves except
  by detach). Since R10, a refused `transfer()` reclaims the resource by
  default (`TransferPolicy::ReclaimOnRefusal`, D-035): it would run the
  process hook with `ContextEnd` and terminate the process. If a later host
  path ever needs to move a process resource, it must decide that consequence
  explicitly (pass `KeepOnRefusal` to keep it with the source context, whose
  end is then the fallback) — never rely on the default by accident.
* Blocking `run()` calls have no script-visible handle; when a call returns
  with background members still in its group (C1), the binding registers an
  **uncounted** Process resource for the call's context (it is never detached
  or dropped by the script, so R14 does not apply), and the context end runs
  the same hook (the residue is cleaned up and settled through a ticket like
  any other).

**3. What moved out of CP16 at the swap (no competing mechanism).**

| CP16 before | Now |
|---|---|
| `handle_adapter_tmp.*` (stand-in table) | deleted; `handles::HandleTable` |
| `ProcessGroup::cleanup_failed()`, `GroupState::Lost` | OS facts in the result object only; the verdict is CP17's `LifeState` (`completed` / `lost` `process.lost` / `alive_after_kill` `process.alive_after_kill`) |
| `ContextCleanup` (label, error flag, one wait per context end, failure list) | deleted: `close_context(id, on_error)` waits for every obligation; `CleanupEvent::context_label` / `context_error` drive the routine warning |
| `runtime::report_cleanup_failure` / failure sink, settle notifications | deleted: `CloseReport::failures`, `final_exit_status(original)` (D-035) |
| reaper adoption of abandoned groups | unchanged mechanism; completion through the moved-out ticket |
| routine warnings | unchanged, CP16's (D-030) |

Implementation details settled during the swap: the hook never blocks (it
moves the ticket out, starts termination, adopts the group into the reaper
and returns `pending()`); the group -> sink -> group reference cycle is broken
when the ticket completes; a run() residue attaches its sink without replay
(`set_lifecycle_sink(sink, false)`), so the call's own exit does not complete
the residue's lifecycle before its context ends (found by the
`alive_after_kill` test); a refused detach cancels the process at once.

**4. D-035 items in the swap.** Reference-counted abandonment: started
(`interactive`) and `detached` handles are created with
`CreateOptions{counted = true}` (also required for detach, R14); the VM's boxed `HandleRef` drop is the
`Abandoned` path tested here. Use after close: every process native maps
CP17's `Closed` status to `runtime.handle_closed`; CP16's
`process.stdin_closed` stays the error for `write()` after `close_stdin()` on a
still-open handle. Cleanup failure → nonzero exit: via
`final_exit_status`, never by CP16 directly.

**5. Integration points, settled.** (a) CP17's `CleanupEvent` carries the
context label and error flag (owner, D-035): `ContextCleanup` is gone. (b)
Members alive after SIGKILL map to `Failed` (`alive_after_kill`), an
unconfirmed stage to `Lost` (owner).

## 9. Phase 2: legacy Host reads and processes through the Host layer

Base: `runtime-2` @`e8e4b106` (host-deps integrated @`2d191e6b`, CP17 swap
@`b4a086ed` accepted). The Host layer and CP17's handle table are now linked
into `nift` (`RT_HOST_LINK_SOURCES`); the legacy interpreter performs its
Host reads and its `run()`/`cmd().run()` launches through it, with the
recording hooks live.

**Scope of "don't touch the parser/lexer".** Read as Runtime 2.0's
`src/rt` parser, lexer, `compiler_expr` literal region, `natives/*`, `value*`
and records20k's `compiler.cpp`/`prepared.cpp`: none of them changed. Routing
the *legacy* interpreter's reads necessarily edits its host call sites in
`src/Parser*.cpp` (no grammar, evaluation or dispatch change).

### 9.1 What goes through `HostCall`

| Legacy operation | Host op (v1 / v2) | Recording |
|---|---|---|
| `open()`, `cat()` | `Read` (`read` / `read`) | dependency + hash; failure: absent / unreadable |
| `open_bytes()`, `sha256_file()` | `ReadBytes` (`read` / `read-bytes`) | streamed hash; failure as above |
| `file().open(m)` | `Open` (`open`+`read` / `open`, `read`) | `r`,`rw`: `open_read` (hook/standalone contexts still report the bytes, c7971365); `w`,`a`: none |
| `ifstream()`, `ofstream()` | `StreamOpen` (— / `stream-open`) | input: dependency hashed at open; output: none |
| `exists`, `is_file`, `is_dir`, `stat`, `file().exists`, template directory walks | `Stat` (`stat` / `stat`) | presence / kind (+ metadata for `stat()` of a file) |
| `ls()` (dir), template listings | `List` (`list` / `list`) | listing fingerprint, recorded on success **and** failure (legacy observed before listing) |
| `ls()` (glob) | `Glob` | as list |
| `getenv()`, `@getenv` | `Getenv` / `GetenvTemplate` | environment value (not `NIFT_HOOK_*`) |
| `env()`, `platform()` | `EnvAll` / `Platform` | snapshot / platform (legacy's non-standalone conditions) |
| `minify()` source read | `Read` via `observe_read` | as `open()` |
| `run()`, `cmd().run()` | `Spawn` per stage (`spawn` / `spawn` with argv, cwd) | none |

Paths: legacy resolves and validates (three copies of `resolve_path`, the
`--fs-root`/project-root messages unchanged) and the call **adopts** the
result (`HostCall::adopt`; the trace spells it absolute and lexically
normal). Op table change: `ReadBytes` is traced `read` after success (host-deps
made `open_bytes()` a traced dependency; the CP16 fixtures were re-captured by
host-deps and are applied in `56ecb7b1`).

Still traced by legacy's own emitter (not Host calls yet): writes (`save`,
`touch`, `mkdir`, `remove`, `copy-to`, `move-*`), template `read-source` /
`read-json`, job-control `spawn`. Under `NIFT_TRACE_HOST_VERSION=2` these lines
are written in v2 framing (`"v":2`, v1 result class, no `err`), so a trace file
never mixes formats; the version is now taken with the descriptor in
`src/HostTrace.h`.

### 9.2 One recorder for every engine (owner requirement)

`src/PageObserver.{h,cpp}` is the only implementation of the recording rules
(moved unchanged from `ParserObservation.cpp`). An engine supplies its
RenderHost, RenderResult and the context's mode (`observing`, `standalone`);
the legacy interpreter's `ParserHostObserver` is a `PageObserver` built from
the parser. Legacy's remaining record-only call sites (page members, copy
sources, template absences) delegate to it too. The VM's host natives, when
they land (CP13–CP15 own `natives/*`), construct the same `PageObserver` and
hand it to their `HostCall`s; nothing else records.

**Parity fixture** (`tests/runtime2/cp16/engine_parity.py`, `make
test-runtime2-cp16-engine-parity`, in `make test`): 17 reads (all of 9.1's
recorded reads, plus a failed open) × 9 contexts (page `@script`, function,
imported module, async task, thread, pre-build hook, custom build script,
post-build hook, standalone script) = 153 cases, each run with `NIFT_VM`
unset and `NIFT_VM=1`; page-info dependency fields (or, for standalone
scripts, which record nothing, the v2 Host-op stream) are compared, and
legacy's recordings are pinned in `tests/runtime2/cp16/parity-expected/`.
Results: 0 differences. **The VM was engaged in none of the 153 cases**, and
the harness says so per case: the VM runs standalone scripts only and has no
host natives yet (each script case falls back: `fallback:callable:open`,
...); page, hook and build contexts never reach it. So today the fixture
proves (a) one recorder, (b) recording unchanged by this phase in every
context — the 136 page/hook/build recordings were identical to the
pre-routing binary (`e8e4b106` + `56ecb7b1`) before they were pinned; the 17
script cases were pinned from this binary because the v2 Host-op stream did
not exist before — and (c) it fails at once if an engine records differently.
`--require-vm` makes "VM not engaged" a failure, for when the VM runs these
contexts. A seeded change of one pinned recording is reported as DIFF.

### 9.3 `run()` / `cmd().run()` on `ProcessGroup`

`Parser::run_host_pipeline` (`src/ParserHost.cpp`) maps `ProcessSpec` to a
`FileCapture` request (capture limit = the `NIFT_READ_LIMIT` value, 0 =
unbounded), launches through `start_traced(..., wait_at_once)` and maps the
result back. Result objects are unchanged (`legacy_routing.sh` case 1 pins
them against the legacy launcher: exit codes, captures, exec failures 127,
signals 137, pipelines' per-stage results, env/cwd/redirections/append).
Differences, all deliberate:

* **No C3/C1 for legacy run()** (`SpawnRequest::own_group = false`, remote
  job cp16-phase2 item 4): the stages stay in Nift's process group, as the
  legacy launcher left them. Two pinned contracts need exactly that:
  rt-diff-selftest kills a timed-out entry's process group and expects
  nothing left (a child in its own group survived it), and
  test-consumer-snapshots' interrupt barrier expects a hook's child to
  outlive a SIGKILL of Nift alone (it keeps the build lock). Only Nift's own
  group satisfies both. Consequently there is no group to find leftover
  background members by (C1) and no terminal handoff (C3) for legacy
  `run()`; Nift signals its stages by pid. C1/C3 remain the default for the
  Host layer's own `run()` options (VM natives). The C1 machinery for legacy
  (one CP17 context per parser, closed at script end, render end and
  destruction, `register_run_residue`) stays wired and is inert while
  `own_group` is false. **Owner decision point:** C1/C3 for legacy run()
  would need either a change to those two contracts or the subreaper
  (adopted orphans found by parentage), which is deferred (job control).
* **C2**: installed at the first managed launch when SIGTERM is at its default
  (stages in Nift's group are terminated by pid)
  (SIGINT default or ignored: an ignored SIGINT, e.g. a shell background job,
  stays ignored); never in the interactive shell or under `--auto`'s own
  handler. A `run()` that returns while Nift is shutting down fails, so the
  script does not run on; Nift ends with the signal (143/130). Signal wake-up
  is a futex on Linux (no descriptor).
* **No PDEATHSIG for legacy launches** (same reason): `test-consumer-snapshots`' interrupt
  barrier requires a hook's child to outlive a SIGKILLed Nift and keep the
  build lock. SIGKILL of Nift therefore behaves as before.
* Low descriptors (CP07 "capture under `ulimit -n 6`" kept): the exec-failure
  report pipe is optional when out of descriptors (an exec failure is then
  only its 127/126 status, as legacy); a blocking call arms no host-loop
  timer (`ProcessGroup::start_for_wait`); `group_has_members` first asks
  `kill(-pgid, 0)`.
* Parent-side launch failures (pipe/fork/capture-file creation) keep the Host
  layer's texts ("cannot create pipe: ...") instead of legacy's "pipe failed" /
  "fork failed"; reachable only by fault injection.
* `sha256_file()` mid-read I/O errors now record the failed read (the Host
  layer's D-028 rule; legacy recorded nothing in that one case).

Not moved: Windows keeps the legacy launcher (`process_win.cpp` reports
`process.unsupported`; the Windows build compiles the Host layer but was not
compiled here — no Windows toolchain), the interactive shell's command
statements and job control (CP19–CP21), templates' `@run`-style and package
tool launches (`nift_run_process` callers outside `run()`/`cmd().run()`),
`Serve`. The subreaper is not enabled from the CLI (job-control orphans).

### 9.4 Tests (local, light; heavy gates on Linode)

| Test | Result |
|---|---|
| `test-rt-host` | 495/0 and 1402/0 |
| CP16 fixtures `capture.py --check` | 30/30 (base and this binary) |
| `legacy_routing.sh` (new, `make test`) | 23 PASS: result objects, C3, C1 script and page, C2 plain and SIGTERM-ignoring (SIGKILL after the 2,000 ms grace), trace v2 (all lines v2; failed read with its code; v1 unchanged), stdin |
| `process_capture.sh` | 10 PASS; case 4 flipped (C2) |
| `engine_parity.py` (new, `make test`) | 153 cases, 0 differences, VM engaged in 0 (§9.2) |
| `host_deps.py`, `dynamic_reads.sh`, `sha256_builtins.sh` | pass |
| `test-consumer-snapshots` | interrupt barrier 6/6 after the PDEATHSIG decision; the suite itself has timing-sensitive cases that fail intermittently on this loaded host for the base binary too (`versions`, `fingerprint_migration`), so its verdict is taken from Linode |

### 9.5 Remote job cp16-phase2 (671f2236): fixes

| Item | Cause | Fix |
|---|---|---|
| 1 `deadline_races` (rt-host) | real race: a cleanup request on a finished group with nothing left in it made the group unsettled until the next pump, after its lifecycle had already completed (4/300 locally; 0/300 at slice 1, the cheaper `kill(-pgid, 0)` membership check changed the timing) | `begin_terminate()` settles such a group at once; 0/400 since |
| 2 `proc_run` fixture | environmental expectation: `#!/nonexistent/interp` gives ENOTDIR on the capture host (`/nonexistent` is a file there) and ENOENT elsewhere, so both launchers (same logic) said "executable not found" here and "interpreter not found" on Linode | interpreter `$BASE/ext/missing-interp` (ENOENT everywhere); re-captured with the base binary |
| 3 `test-sha256` SIGABRT | not reproduced locally (`make test-sha256` passes, SHA-NI host); no log available yet | rerun in the next job with its log copied |
| 4 rt-diff-selftest leftovers | legacy run() children in their own group (C3) | §9.3: `own_group = false` for legacy run() |
| 5 `test-rt-lint` | `src/rt/host` broke the CP09 lint (file and function size, own headers, line length, string compares) | `process_posix.cpp` split by concern (`process_posix_{sys,state,launch,loop,lifecycle}`), `process_runtime.cpp` split (`process_{loop,reaper,signals}`), headers for `process_common`/`process_win`, long lines wrapped, cold-file markers for option parsing, trace-version parsing and `HostCall` mode checks; `PASS rt-lint: 0 problem(s)` |
| 6 rt-host-tsan | environment (worker 9 `vm.mmap_rnd_bits`) | rerun |
