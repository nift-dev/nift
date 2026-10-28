# CP21 — interactive Host layer (full checkpoint candidate)

**Status:** implementation candidate for the whole CP21 checkpoint. Not
integrated, not pushed, no CI dispatched. The frozen S1 and R1 candidates are
preserved on their own branches; this candidate supersedes them after the N1–N5
review corrections.

## Base and history

| Item | Value |
|---|---|
| Base commit | `45b22cdcc8fa2e70c2ce44a2a5244c0f8a4b555e` |
| Base tree | `bf44b84f06eea823dcf9cf4b80518899f62da059` |
| Frozen S1 | `0bd13b2b` / tree `3bff9c23` |
| Rejected R1 | `af869bd4` / tree `2b5d8cac` |
| Candidate branch | `cp21-complete` in `cp21-dev` |

CP17 + N2 and CP18 are accepted but not present at this base. CP19 retains
ownership of `.niftrc` compilation and the shared compiler front end.

## Specification traceability

| CP21 requirement | Implementation | Evidence |
|---|---|---|
| One job model for interactive/script POSIX | `shell::JobTable` over Host `ProcessGroup`; CLI has no legacy table routing | CP21 contract; CP20 R1; v45 job control |
| Foreground/background, `jobs`/`fg`/`bg`/`wait` | Host job table, one ID space, truthful Done/Stopped/Running | PTY oracle 7/7; CP21 contract |
| Ctrl-C / Ctrl-Z and terminal handoff | Host process groups, terminal lease, give-before-SIGCONT | oracle; delayed-reader and recovery tests |
| One process-wide terminal ownership policy | `process_terminal.*` exclusive lease and pending recovery | terminal lease/recovery tests |
| Completion and history unchanged | line editor/history paths untouched; existing suites retained | completion PTY; history smoke; CP19 completion |
| rc compilation | **CP19** (not implemented here) | — |
| Prompt ≤ 3 ms | individual command-to-prompt measurement, threshold preserved | `perf/cp21-prompt-latency.json`; **UNVERIFIED** |
| Linux/macOS job control | Linux pass; portable path implemented and simulated | macOS execution **UNVERIFIED** |
| Windows behaviour unchanged | legacy launcher retained; new Host APIs are POSIX-only | MinGW/Windows **UNVERIFIED** |
| CP20 switches removed | `NIFT_CP20_FORCE_*` no longer read | CP20 tests updated |
| Failure semantics | restore recovery, launch handoff checks, shell status, detector and Make controls | N1–N5 recovery/controls |

## Full-review D1/D2 corrections

- **D1 terminal-sensitive launch barrier:** when a foreground launch needs a
  terminal handoff, the child may set up its group and take the tty but waits
  on a dedicated pipe before exec. The parent performs the authoritative
  `tcsetpgrp`, then commits or aborts. A parent-only handoff failure therefore
  prevents child execution; child-only failure is reported through the exec
  report. Non-handoff launches pay no extra descriptor. Parent-only,
  child-only, stage-attribution, low-fd and recovery controls are included.
- **D2 stable tty reference:** the lease opens and retains `/dev/tty` (or a
  dup of a tty stdin) independently of fd 0. Give/restore use that reference,
  so closing or replacing stdin cannot turn an unverified restoration into
  success. The reference is closed on successful release and retained only
  for pending recovery. Closed/replaced stdin, one-shot/persistent failure,
  destruction and fd-growth controls are included.
- **Certification reachability:** `test-v45-job-control` now depends on the
  CP21 contract, compiled mutants, Make failure-control and the Linux
  recovery/mutant units. The certification guard records those edges and
  removal controls fail if any is disconnected.

## R2-review D3 correction

- **D3 atomic fallback descriptor:** when `/dev/tty` is unavailable and stdin
  is a tty, the lease now duplicates stdin with `F_DUPFD_CLOEXEC` (or refuses
  the fallback on a platform without it). It never uses `dup()` followed by a
  separate `fcntl()`, which allowed a concurrent fork/exec to inherit the
  terminal between the calls.
- A compiled D3 mutant restores the non-atomic fallback. The regression forks
  an exec child during the controlled duplicate window and scans descriptors
  3–511 in that child; the mutant leaks the tty, the corrected candidate does
  not. A dead/unsuitable child cannot satisfy the scan prerequisite.
- The D3 normal and mutant tests are permanent `test-v45-job-control`
  dependencies with certification-guard removal controls.

## R3-review acceptance-harness corrections

- **H1 bounded scanner:** the D3 exec child is run with a 5 s poll/read
  deadline and a bounded `waitpid`; fork/wait/read errors are classified as
  setup failures. On timeout the child process group is SIGKILLed and reaped.
  A sleeping-child timeout control is permanent and never counts as detection.
- **H2 scanner status:** a clean scan must exit 0; a descriptor-inheritance
  witness must exit 1 with exactly one `exec_extra_fds=N` marker consistent
  with the emitted `extra_fd=` lines. Exit 23 after a clean scan, arbitrary
  nonzero exits, signals, missing/duplicate output and setup failures are
  rejected. The real D3 mutant still fails `exec-child-no-extra-fds` with
  `exec-child-valid-status` passing.
- **H3 cold targets:** every new CP21 helper/mutant recipe creates its
  `TEST_DIR` output directory; normal and mutant D3 targets build and run from
  an empty output directory.
- **Synchronized PTY oracle:** `foreground_tty` waits for the complete result
  line and the next prompt, then requires shell ownership through both
  `/proc` and `tcgetpgrp` and no remaining foreground child. `jobs_ids` waits
  for the prompt after each command. The corrected oracle passes release and
  sanitizer runs, including all eight comparator controls. Historical failed
  sanitizer transcripts that were not supplied remain explicitly unresolved.

## R4-review D3 harness corrections

- **Strict scanner grammar:** `tests/cp21_fd_protocol.h` parses complete lines.
  It requires exactly one canonical `exec_extra_fds=N` record, exactly N
  well-formed unique `extra_fd=<fd> flags=<0|1> target=<path>` records, valid
  descriptor ranges, and the documented clean/defect exit status. Embedded
  markers, `0junk`, `+0`, spaces, duplicates, contradictory records, missing
  records, partial/blank/unexpected lines and signal death are invalid
  evidence, never detection.
- **Self-validating parser table:** `tests/cp21_fd_protocol_unit.cpp` covers
  valid clean/defect transcripts and the malformed-control corpus; it is a
  dependency of the D3 target and is wired into the certification aggregate.
- **Bounded cleanup:** the scanner timeout verifies process-group ownership
  before `killpg`, kills only the scanner and known descendants, sets the test
  as a subreaper, sweeps and reaps escaped-group descendants, and reports a
  failed/uncertain cleanup instead of success. A sleeping-group child, an
  escaped-group descendant, malformed scanners, dead/missing children and
  unrelated exit statuses are permanent controls.
- **PERMANENT parser reachability:** the certification guard requires
  `test-cp21-fd-inheritance -> test-cp21-fd-protocol-unit` in addition to the
  seven existing CP21 edges.

## R5-review D3 harness corrections

- **Cleanup fails closed:** the test requires successful
  `PR_SET_CHILD_SUBREAPER`, readable `/proc`, and a unique scanner tag. A
  missing subreaper or process-inspection facility prints
  `SETUP_FAIL ... unavailable`; it never reports `cleaned=1`. Timeout cleanup
  signals only the verified scanner group and tagged descendants, reaps them,
  and reports an uncertain/failed cleanup instead of success when verification
  fails. Descendants are cross-checked by both the scanner tag and the
  test's `/proc` child tree, including a descendant that clears its
  environment; a pre-existing child is rejected. Permanent subreaper-denied
  and `/proc`-unavailable controls are included.
- **Strict target grammar:** descriptor targets must contain printable
  non-whitespace bytes only; NUL, C0 controls, DEL, spaces and tabs are
  rejected. Unit and end-to-end controls cover NUL-only, embedded NUL, spaces,
  tabs, CR, ESC, whitespace-only and valid UTF-8 targets. A compiled parser
  mutant that removes target validation is caught.
- **Mandatory parser unit:** `test-cp21-fd-protocol-unit` is `.PHONY`, cannot
  be shadowed by a file of that name, and remains a dependency of
  `test-cp21-fd-inheritance`. Deliberate parser-unit failure propagates to both
  `test-cp21-fd-inheritance` and `test-v45-job-control`; the guard requires the
  parent edge.

## R6-review D3 cleanup isolation

- **Dedicated scanner supervisor:** scanner execution and cleanup moved out of
  the test driver into `tests/cp21_scanner_supervisor.cpp`. The supervisor is
  the only parent of the scanner; it enumerates only its own descendants, so
  an unrelated child of the test driver is never signalled. The D3 driver only
  waits for the supervisor and reads its status/output files.
- **Fail-closed inspection:** the supervisor requires subreaper setup and
  readable `/proc`; per-process `stat` read failures (other than processes
  disappearing during enumeration) are reported as cleanup failure. It
  collects descendants before killing the scanner parent, then kills the
  verified scanner group, tagged/tree descendants and reaps them. A survivor or
  unreadable record cannot produce `cleaned=1`.
- **Adversarial controls:** ordinary clean/D3 mutant, sleeping child,
  same-group and escaped descendants, environment-clearing descendant,
  concurrent unrelated child that must survive, dead/missing executable,
  arbitrary exit, malformed/NUL transcripts, partial-procfs failure,
  subreaper-denied, `/proc`-unavailable, parser target mutant and Make
  shadowing/failure propagation are permanent.
- **Parser/Make corrections retained:** strict target grammar, parser unit
  mutation, `.PHONY` mandatory execution and guard edge remain unchanged.

## R7-review cleanup-evidence correction

- The supervisor enumerates the scanner subtree through Linux
  `/proc/<pid>/task/<pid>/children` files, not a global `/proc` scan. It never
  treats a failed or empty `stat` read as proof of disappearance.
- Known descendants are verified with `/proc/<pid>/stat`; ENOENT/ESRCH,
  zero-length records, permission failures and malformed records are explicit
  cleanup failures. A previously observed live descendant remains an
  obligation until `kill(pid, 0)`/reaping confirms it is gone.
- Cleanup collects descendants before killing the scanner parent, kills the
  verified group and descendants, reaps direct/adopted children, and requires
  every known PID to be gone. A survivor or unreadable record cannot produce
  `cleaned=1`.
- Permanent controls cover ENOENT, zero-length stat, failed children
  enumeration, failed SIGKILL, subreaper denial, unavailable `/proc`,
  malformed/missing children, escaped/untagged descendants and an unrelated
  test-driver child that must survive. Partial-procfs/subreaper controls fail
  closed.

## R8-review D3 acceptance split

- **Mandatory descriptor gate:** `test-cp21-d3-mandatory` runs the normal
  scanner, the genuine compiled D3 mutant, strict protocol/parser validation,
  parser-target mutation, malformed/dead/unrelated/timeout controls and the
  supervisor's direct-child cleanup. It is the certification edge. It does not
  claim arbitrary escaped-descendant containment.
- **Adversarial containment controls:** `test-cp21-fd-inheritance-mutants`
  retains escaped/untagged/environment-clearing descendants, unrelated-child
  survival, subreaper/`proc` failures, ENOENT/empty/children-file faults and
  failed-kill controls as a separate developer/safety target.
- **Evidence model:** descendant discovery uses the union of checked
  `/proc/<pid>/task/<pid>/children` recursion and checked `/proc/<pid>/stat`
  parentage. A single unavailable source is compensated by the other; if both
  are unavailable the cleanup is uncertain and never `cleaned=1`. Known
  descendants are verified with `/proc` stat and `kill(pid,0)`/reaping.
- **Explicit limitation:** non-atomic `/proc` observations cannot prove
  arbitrary escaped-descendant containment. The mandatory D3 gate does not
  claim it; adversarial controls report cleanup failure/uncertainty when
  evidence is contradictory or unavailable.

## R10 certification-integrity corrections

- **Mandatory mutant evidence:** `test-cp21-d3-mutant` and
  `test-cp21-parser-target-mutant` are separate mandatory targets. Each runs
  the intended compiled binary, requires the named behavioural witness, and
  writes a run record containing the binary path, binary SHA-256, current make
  invocation token and witness names. `test-cp21-d3-mandatory` verifies both
  records are from the current invocation and match the current binaries before
  it can pass. The guard requires the two mandatory edges and the negative
  `test-cp21-evidence-control` replaces each execution body with a no-op and
  requires the mandatory target to reject stale evidence. This detects the R9
  hollow-recipe failure; it does not claim to detect arbitrary malicious edits
  to all certification recipes.
- **Truthful cleanup evidence:** children-file and stat inspection now return
  separate completeness flags. An empty/missing children file or an unavailable
  stat record is not proof of absence. Cleanup succeeds only when stat evidence
  is complete (the live supervisor's own stat record was observed) or when a
  non-direct descendant was actually discovered and the children traversal is
  complete. Previously discovered descendants remain cleanup obligations until
  observed dead. A successful `kill` request alone is not treated as proof.
- **Both-source faults:** permanent ENOENT and empty probes make both evidence
  sources unavailable after startup while an escaped descendant survives. The
  supervisor reports `TIMEOUT_CLEANUP_FAIL descendant evidence incomplete`, not
  `cleaned=1`; the control requires the survivor to be independently observed
  before it is killed.
- **Limitation:** non-atomic `/proc` snapshots cannot prove that an undiscovered
  escaped descendant has terminated. The adversarial suite reports known
  cleanup and uncertainty; it is not a general containment guarantee.

## N1–N5 corrections

- **N1 recovery:** failed `tcsetpgrp`/SIGTTOU restoration leaves
  coordinator-owned pending tty/signal state and the saved disposition. The
  owner pointer is cleared; retries and new acquisitions first complete
  recovery. SIGTTOU stays ignored while tty restoration is pending.
  One-shot/persistent failures, retry, new acquisition and destruction after
  failure are covered.
- **N2 launch handoff:** child and parent `tcsetpgrp` results are checked.
  Child failure reports `SetupTerminal` and exits before exec; parent failure
  marks the launch failed and cancels the group. The child-effect marker is
  absent on failure, and a healthy launch recovers.
- **N3 shell status:** `ProcessResult::host_fault` distinguishes a Host
  terminal-operation failure from the child's status. A foreground/background
  external command with a Host fault returns nonzero; conditional commands no
  longer run after a restoration failure while stage/child status is retained.
- **N4 detector:** the missing-SIGCONT mutant must leave the exact child
  stopped; the detector requires `bg-state-live` as well as the named failing
  check. An unrelated SIGCONT→SIGKILL control is rejected. Dead, normal and
  unrelated fd-leak controls remain required.
- **N5 Make propagation:** fault checks use checked `set -e` recipes; mutant
  controls verify the specific rejection diagnostic; a PATH shim control
  proves each injected fault makes the enclosing target fail and stop.

## Host changes

- `ProcessGroup` job-control API: `wait_job_control`, `continue_group`,
  `give_terminal`, `reclaim_terminal`.
- `process_terminal.*`: process-wide lease, pending restoration recovery and
  SIGTTOU disposition ownership.
- `process_posix_launch`/`process_posix_sys`: checked child/parent handoff and
  `SetupTerminal` attribution.
- `process_posix_loop`: restoration result retained in
  `ProcessResult::host_fault`; handoff recovery state retained on failure.
- `shell::JobTable`: interactive mode, give-before-continue, truthful states,
  failure propagation.
- `CLI`: Host job routing for all POSIX interactive external commands; no
  legacy `ShellJobTable` routing; `host_fault` maps to a failing shell status.

## Behaviour matrix vs pre-CP20 oracle

| Area | Result |
|---|---|
| foreground command/pipeline, tty/stdin/stdout/stderr | SAME |
| background `&`, job ID/PGID notice, terminal never owned | SAME |
| `jobs`, `wait`, `fg`, `bg`, Stopped/Running/Done | SAME |
| Ctrl-C (SIGINT to foreground group, shell survives, status 130) | SAME |
| Ctrl-Z stop, terminal return, fg resume, repeated cycles | SAME |
| `exit` with a stopped job | BLOCKING (same contract) |
| completion, history, rc execution | unchanged |
| failure paths | improved and explicitly nonzero where a Host operation fails |

## Launch and tty failure controls

| Control | Expected |
|---|---|
| parent-only handoff failure (50 ms window or immediate) | no child effect; failed launch; healthy retry |
| child-only handoff failure | no child effect; failed launch; healthy retry |
| closed stdin while a group owns the tty | reclaim succeeds and the shell actually owns the saved tty |
| replaced stdin (`/dev/null`) | same |
| one-shot/persistent tty restore failure | pending, blocked acquisition, then recovery |
| destruction with pending tty recovery | no dangling owner; recovery after fault clears |
| lease descriptor cycles | no fd growth |
| compiled D1 mutant (no barrier) | parent-only check fails for the intended reason |
| compiled D2 mutant (isatty-gated restore) | closed-stdin ownership check fails for the intended reason |
| compiled D3 mutant (dup then fcntl) | exec child inherits the tty and `exec-child-no-extra-fds` fails |
| dead D3 exec child | setup failure, never counted as detection |
| D3 timeout child | bounded kill/reap, never counted as detection |
| D3 escaped-group descendant | subreaper sweep/reap, `cleaned=1` required |
| D3 unrelated scanner (clean scan, exit 23) | invalid status, never counted as detection |
| embedded / 0junk / malformed scanner output | strict-line parser rejects as SETUP_FAIL |

## Mutants and controls

| Mutant | Intended failure | Control |
|---|---|---|
| 1 | background takes the tty | `background-never-owns-terminal` |
| 2 | missing SIGCONT leaves the child stopped | `bg-state-running` with `bg-state-live` |
| 3 | stopped shown as Running | `ctrl-z-stopped-state` |
| 4 | terminal not reclaimed after stop | `ctrl-z-restores-terminal` |
| 5 | duplicate job IDs | `job-ids-unique` |
| 6 | unrelated fd leak | must not be accepted as CP21 detection |
| child-death | unrelated SIGCONT→SIGKILL | must not be accepted as mutant 2 |
| dead/normal | setup failure / no defect | must not be accepted |

## Gates

- Pre-CP20 oracle: 7/7, five release repetitions, ASan+UBSan 7/7; controls 8/8.
- CP21 contract: five release repetitions; ASan+UBSan normal path; fault
  injection for delayed tcsetpgrp, failed SIGCONT, failed handoff, failed
  restoration; terminal lease/recovery units.
- CP20 R1 22/22 and PTY suite 20/20; CP20 mutants 11/11; fd leak clean.
- v45 native runtime and job control; CP19 dispatch; v44 execution shell,
  completion PTY, history smoke and CP19 completion.
- Host tests and Host ASan/UBSan; targeted lease/recovery ASan/UBSan.
- Integrity 533 files/0 findings; runtime lint; warnings; Host source-list.

## Remaining obligations

1. **macOS:** run the portable contract, lease/recovery and descendant cleanup
   on a real macOS host. Not executed here.
2. **Windows:** MinGW cross-compile, Windows runtime and interactive console
   external/pipeline coverage. The legacy launcher is retained; not verified.
3. **Prompt:** the authoritative CP21 interval and reference-platform
   measurement are not established. Threshold remains 3 ms;
   classification **UNVERIFIED** (local pipeline observations exceed it).
4. **Reference-platform jobs:** background launch, fg return and prompt tails.
5. **Inherited Host lifecycle:** `managed_pgid_cleanup` remains
   **UNRESOLVED / POSSIBLY FLAKY**; reviewer reproduced it on the exact base
   and it failed again in the final sanitizer run (`before=0 after=1`).
6. **Integration:** CP17/CP18 combined-tree certification. Preserve CP20's F6
   ESRCH fast path during CP17 reconciliation; CP21 did not change that
   destructor.
