# CP21 slice 1 — interactive Host job control

**Status:** bounded implementation candidate; **not integrated**, not pushed, no
CI dispatched. Base and tree are immutable identities below.

## Base

| Item | Value |
|---|---|
| Base commit | `45b22cdcc8fa2e70c2ce44a2a5244c0f8a4b555e` |
| Base tree | `bf44b84f06eea823dcf9cf4b80518899f62da059` |
| Branch | `cp21-interactive-host` in worktree `cp21-dev` |

The base is the CP14B-integrated local `runtime-2` tip. CP17 + N2 and CP18 are
accepted but not present at this base.

## Dependency finding

The slice is independently implementable on the current interfaces. It does
**not** require CP17's typed handles, observers or W13 integration: job
control is owned by `shell::JobTable` over `rt::host::ProcessGroup`, and the
POSIX stop/terminal operations are Host process facts, not handle lifetime.

Interfaces that will need reconciliation when CP17 lands:

- `src/rt/host/process.h`: CP21 adds `wait_job_control()`, `continue_group()`,
  `give_terminal()` and `reclaim_terminal()` to `ProcessGroup`. CP17's
  lifecycle/handle binding wraps the same group but does not replace these
  operations; the binding must not take ownership of the terminal.
- `src/rt/host/process_posix.cpp`: CP17 R4 currently reverts the CP20 F6
  fast-path cleanup in `~ProcessGroup`. That is a merge/rebase decision for the
  integrator; CP21 did not change that code.
- `src/shell/Spawn.cpp` / `Spawn.h`: the job table stores
  `shared_ptr<ProcessGroup>` and now carries the interactive flag. If CP17
  routes the same groups through the handle table, ownership must have one
  owner (the shell table or the handle table), not both.
- `src/CLI.cpp`: all POSIX interactive external execution now uses
  `shell::JobTable`; no legacy `ShellJobTable` routing remains. CP19's shared
  front end can change command classification without changing this routing.

## Implemented surface

- POSIX interactive foreground and background external commands, pipelines and
  redirections run on the Host `JobTable`.
- One job ID space for foreground, background, `jobs`, `fg`, `bg`, `wait`,
  `wait N`, and the exit-time drain.
- Ctrl-Z stops the foreground group, returns exit status 148, restores the
  terminal to the shell and retains the stopped job.
- `fg` resumes a stopped or running background group, hands it the terminal,
  and returns on either exit or another stop. `bg` sends SIGCONT and leaves
  the job in the background.
- Background groups use `TerminalPolicy::Never`; they never own the tty.
- Interactive foreground jobs use `ProcessGroup::start_for_wait()`, so an
  ordinary command does not create the persistent Host event-loop
  descriptors. A background job, or a stopped job that remains live, arms the
  loop as before.
- Foreground job wait uses `waitid(WNOWAIT)` state observation; the stop
  status stays available to the shell's later blocking wait/exit path, which
  preserves the pre-CP20 `exit` with a stopped job blocking contract.
- Completion, history, `rc` execution and command classification are
  unchanged. `rc` compilation remains CP19's.
- CP20 test-only switches `NIFT_CP20_FORCE_SCRIPTED` and
  `NIFT_CP20_FORCE_INTERACTIVE` are removed. Windows keeps its legacy
  launcher; the CP21 Host operations are Windows no-ops.

## Tests and raw results

- PTY oracle (`pty_oracle.py`, pre-CP20 baseline `e1f2f59f`): **7/7**,
  5/5 release repetitions; ASan+UBSan also 7/7. `exit_stopped` remains
  `BLOCKING`, as in pre-CP20/CP20.
- `tests/cp21_interactive_host.py`: foreground tty, background ownership,
  Ctrl-C, Ctrl-Z, stop→bg→fg, unique IDs, foreground fd non-growth, prompt
  latency. Release 3/3 repetitions and ASan+UBSan (latency gate skipped under
  sanitizers).
- CP21 mutants, all rejected: background takes tty, no SIGCONT on resume,
  stopped shown as Running, terminal not returned after stop, duplicate job
  ID.
- `make test-cp20-r1`: 22/22. `make test-cp20-shell-spawn-mutants`: 11/11.
- `make test-v45-job-control`, `make test-cp19-s1-dispatch`,
  `tests/v44_interactive_completion_pty.py`, `v44_cp21_history_smoke.sh`,
  `cp19_completion.py`: pass.
- `make test-rt-host`: pass. `make test-rt-host-sanitize`: pass (one earlier
  `managed_pgid_cleanup` run under load reported `after=1`; isolated rerun and
  the final full target report `after=0`; the same base sanitizer test passed).
- `make test-test-integrity` (532 files, 0 findings), `make test-rt-lint`,
  `make warnings-check`, `make test-rt-host-sources-list`: pass.
- Prompt diagnostic: **0.76–1.05 ms/command** (200 external commands per
  round, 3 rounds, release build). CP20 PTY benchmark on the same build:
  0.74 ms/launch.

## Remaining CP21 inventory

1. macOS acceptance: rerun the oracle and `cp21_interactive_host.py`; the
   `/proc` ownership assertions are Linux-only, so macOS uses text/status
   parity. CI/reference host required.
2. Windows: legacy launcher retained; MinGW cross-compile and Windows runtime
   are unverified on this host.
3. Milestone performance: rerun the prompt target on the reference platform.
4. Full certification integration with CP17/CP18 once their authoritative
   interfaces are present.
5. `rc` compilation remains CP19.
6. Optional hardening: automatic completion notices and job-ID reuse/notification
   ordering beyond the oracle's exact cases.

## Unverified gates

- macOS job-control acceptance.
- Windows runtime and MinGW compilation.
- Reference-platform prompt latency.
- Full CP17/CP18 combined-tree and milestone certification.
