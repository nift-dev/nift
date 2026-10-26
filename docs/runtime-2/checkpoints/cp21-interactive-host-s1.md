# CP21 slice 1 (R1) — interactive Host job control

**Superseded by `cp21-interactive-host.md` (full checkpoint candidate).**

**Status:** R1 corrected implementation candidate; **not integrated**, not
pushed, no CI dispatched. The frozen S1 candidate remains
`0bd13b2b` / tree `3bff9c23`; R1 is on branch `cp21-interactive-host-r1`.

## Base and frozen candidate

| Item | Value |
|---|---|
| Base commit | `45b22cdcc8fa2e70c2ce44a2a5244c0f8a4b555e` |
| Base tree | `bf44b84f06eea823dcf9cf4b80518899f62da059` |
| Frozen S1 commit | `0bd13b2bd7ccd94f15b512383a673e0c980e7cc5` |
| Frozen S1 tree | `3bff9c230450560541c2185127dcfa00f425ae16` |

The base is the CP14B-integrated local `runtime-2` tip. CP17 + N2 and CP18 are
accepted but not present at this base.

## R1 corrections

- **F1 terminal handoff order.** Interactive `fg` now acquires the terminal
  before SIGCONT. A reader resumed in the background can no longer take
  SIGTTIN and stop again. The contract includes a background reader, repeated
  stop/resume cycles and a test-only delayed `tcsetpgrp` scheduling window.
- **F2 process-wide terminal lease.** New
  `src/rt/host/process_terminal.*` owns one process-wide terminal lease:
  competing owners are rejected, only the current owner can release, and the
  original SIGTTOU disposition is saved once and restored exactly. Launcher
  handoff and the public give/reclaim API use the same coordinator. A real-PTY
  overlap test covers competing acquire, non-owner reclaim, former-owner
  reclaim, concurrent acquisition/release, destruction of a live owner and
  disposition restoration.
- **F3 failure propagation.** `bg` returns `failed to continue job` when
  SIGCONT fails and keeps the job Stopped. `fg` handles terminal/SIGCONT
  failure, does not wait on an unresumed job, and distinguishes a concurrent
  completion from an operation failure. Terminal-reclaim results are checked
  and surfaced; launcher restoration records its result.
- **F4 mutant detector integrity.** Mutant mode now requires a named
  behavioural witness, the specific intended failing check and no unrelated
  failure. Dead `/bin/false`, the normal build and an unrelated fd-leak
  control must all be rejected; raw logs are retained.
- **F5 portable contract.** Linux keeps `/proc` group/tty ownership
  assertions. Other POSIX hosts use a compiled foreground probe, real terminal
  signals, `jobs` state observations and `ps`-based child discovery. The
  portable path is exercised on Linux with `CP21_FORCE_PORTABLE=1`; actual
  macOS execution remains UNVERIFIED.

## Dependency finding

No CP17 typed-handle, observer or W13 interface is required. `shell::JobTable`
owns its groups directly; CP17's binding must not become a second cleanup owner
or overwrite a lifecycle sink. CP17 R4 also carries a revert of the CP20 F6
fast path in `~ProcessGroup`; CP21 did not touch that destructor and leaves the
reconciliation to the integrator.

## Implemented surface

- POSIX interactive foreground/background external commands, pipelines and
  redirections use the Host `JobTable`; no CLI routing through the legacy
  `ShellJobTable` remains.
- One job-ID space and one terminal owner. Background groups use
  `TerminalPolicy::Never`; Ctrl-Z returns 148, reclaims the tty and keeps the
  job; `fg` re-hands the tty before SIGCONT; `bg` propagates continuation
  failures.
- Foreground waits observe stops with `waitid(WNOWAIT)` without consuming the
  stop status, preserving the blocking exit-with-stopped-job contract.
- Interactive foreground launches use `start_for_wait()` so ordinary commands
  do not create persistent Host-loop descriptors.
- CP20 force switches removed; Windows keeps the legacy launcher; rc compile
  remains CP19; completion/history/classification unchanged.

## Tests and raw results

- PTY oracle (pre-CP20 baseline `e1f2f59f`): 7/7 on three release repetitions
  and 7/7 under ASan+UBSan; `exit_stopped` remains BLOCKING.
- CP21 contract: foreground tty/ownership, background, Ctrl-C, Ctrl-Z,
  repeated stop→fg/bg cycles, background reader, unique IDs, fd non-growth;
  release and ASan+UBSan pass (latency is a separate measurement).
- Fault controls: delayed `tcsetpgrp` and failed SIGCONT pass on R1 and fail
  on frozen S1; the terminal-lease overlap test passes on R1 and fails on S1.
- Mutants 1–5 are rejected for their named checks. Dead-binary, normal-build
  and unrelated-failure controls are rejected; six compiled controls are
  retained.
- CP20 R1 22/22; CP20 mutants 11/11; v45 job control, CP19 dispatch,
  completion/history, Host tests, Host ASan/UBSan pass.
- Integrity 533 files/0 findings, runtime lint, warnings and Host source-list
  checks pass.
- Prompt measurement is now individual command-to-prompt observations:
  `/bin/true` median ~0.80 ms (p95 ~1.12, max ~1.95);
  `/bin/true | /bin/cat` median ~3.51 ms (p95 ~4.32, max ~5.80, 86/100 over
  3 ms). Classification remains **UNVERIFIED**; the interval includes process
  launch/execution and no reference platform has been measured.

## Remaining qualifications

1. macOS job-control acceptance has not been executed; the portable path is
   implemented and simulated, not certified.
2. Windows runtime and MinGW compilation are unverified; the legacy launcher
   is retained.
3. Prompt latency remains UNVERIFIED on the reference platform.
4. `managed_pgid_cleanup` remains UNRESOLVED / POSSIBLY FLAKY: the reviewer
   reproduced it on the exact base. Final R1 runs were green; no root cause is
   claimed and no unrelated Host lifecycle rewrite is included.
5. Full CP17/CP18 combined-tree and milestone certification.
6. `rc` compilation remains CP19.
