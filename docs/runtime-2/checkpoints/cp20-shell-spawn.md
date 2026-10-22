# CP20 — shell spawn, streaming pipelines and redirection on Host ops

**Base:** `runtime-2` `e1f2f59f090328fbbb835aa659ff6a2d68195c64` (CP19 integrated).
**Branch/worktree:** `cp20-shell-spawn` / `/tmp/opencode/cp20-shell-spawn`.
**Delivery:** local commits only; no push, merge, tag or integration.

## Implementation

The shell's external-command path now uses the CP16 Host process layer instead
of `ProcessSpec`/`nift_run_pipeline`:

- `src/shell/Spawn.{h,cpp}` converts the shell's existing `ProcessSpec` stages
  to `host::SpawnStage`, selects `OutputMode::Inherit` for the shell, and runs
  foreground pipelines via `ProcessGroup::start_for_wait`/`wait`.
- `src/shell/Spawn.cpp` owns a shell `JobTable` for background `&`: each group
  is a Host `ProcessGroup`, `wait`/`wait_all` reap it, `jobs` reports its
  state, and the table terminates live groups on shell teardown. The legacy
  `ShellJobTable` remains in `JobControl.cpp` for non-shell callers.
- `SpawnStage` gained `stderr_to_stdout`; the POSIX child applies it after
  stdout setup, so `2>&1` works in any stage (including a middle pipeline
  stage) and merges into the stage's actual stdout destination.
- Shell process-group semantics are preserved: a simple foreground command
  gets its own process/terminal group; pipeline stages stay in Nift's group so
  SIGINT to that group still reaches every stage. Background jobs get their
  own group and are cleaned up through the job table.
- Windows keeps the legacy launcher because the Host `ProcessGroup` Windows
  slice is explicitly unsupported. The new Host field and the `src/CLI.cpp` /
  `src/shell/*.cpp` files cross-compile on MinGW with `-Werror`; the POSIX
  streaming/job tests are POSIX-only.

## Behaviour and redirection parity

The shell parser and CP19 classifier are unchanged. All existing redirection
forms are preserved: `<`, `>`, `>>`, `2>`, `2>>`, `2>&1`, per-stage env
assignments, glob expansion, and background `&`/`jobs`/`wait`.

Nift remains authoritative where it differs from Bash:

- `2>&1 > out.txt` is order-insensitive in legacy Nift (both streams go to the
  file); Bash applies `2>&1` before `>` and leaves stderr on the terminal. CP20
  preserves Nift's result.
- A leading redirection (`< in.txt cat`) is classified as the command `<` and
  reports `nift: <: command not found`; Bash runs `cat`. The same result was
  measured on the CP19 base, so CP20 reports the difference rather than
  changing Nift semantics. This is the deferred CP19 leading-redirection
  quirk.
- A stdout redirection on a non-last stage is parsed but not applied by the
  legacy pipeline (the non-last stage always writes to the pipe); CP20 keeps
  that result.

## Streaming

`tests/cp20_shell_spawn.py` runs
`sh -c 'echo first; sleep 1; echo second' | cat | cat` twenty times, with and
without `2>&1`, and requires the first line within 300 ms while the producer is
still running. Host pipes are not captured or buffered by the shell. The
normal and sanitized suites pass 20/20 for both variants; local first-line
delivery is ~10 ms.

## Launch cost

`tests/cp20_launch_bench.py` executes 200 real `/usr/bin/true` launches per
batch, five batches, median per launch; it verifies that the external binary
exists and never substitutes a shell built-in. On the certification Linode
(`g6-dedicated-4`, Ubuntu 24.04, GCC 13.3, instance 108854855, created
2026-10-22T05:15:46Z, deleted and API-verified absent 2026-10-22T05:31:46Z,
~16 min, about US$0.03):

| Case | Nift | Bash | Delta |
|---|---:|---:|---:|
| single `/usr/bin/true` | 0.996 ms/launch | 0.897 ms/launch | **+11.0%** |
| 3-stage `true | true | true` | 2.101 ms/launch | 1.458 ms/launch | **+44.1%** |

The +10% target is missed. The profile points at per-stage Host bookkeeping:
each stage adds an exec-report pipe, a pidfd (when available), process-group
setup and event-loop polling, while Bash's `fork`/`exec` path does not. No
benchmark tuning or Host redesign was done; this is reported for the owner and
Claude's certification decision. Local GCC 15 numbers are noisy and higher
(single ~20%, 3-stage ~164%), so the Linode numbers are the reported target.

## Mutants

`NIFT_CP20_MUTANT=1..6` compile in `src/CLI.cpp`/`src/shell/Spawn.cpp`:
M1 buffers pipelines (streaming), M2 treats `>>` as `>`, M3 ignores `2>&1`,
M4 leaks env assignments, M5 never reaps a background group, M6 reports a
middle-stage failure as the pipeline status. `make test-cp20-shell-spawn-mutants`
builds each and requires the intended failing test; all six are rejected.

## Gates

- CP20 suite (normal + 6 mutants): PASS.
- CP19 dispatch (normal + 4 mutants), `tests/shell_dispatch.py`,
  `cp19_completion.py`, v44 execution/restricted shell smoke, `serve_smoke.sh`:
  PASS.
- `test-test-integrity`, `test-rt-lint`, `warnings-check` (GCC 15 local):
  PASS. MinGW `-Werror` compile of `src/CLI.cpp`, `src/shell/*.cpp`,
  `process_win.cpp`, `process_posix_launch.cpp`: PASS on the Linode toolchain.
- `test-runtime2-rt-diff`, `test-runtime2-vm-corpus`, `test-rt-value`: PASS.
- `test-rt-host` on CP20: the pre-existing `subreaper_reaps_orphans` test fails
  (0/20). In a scratch worktree with Claude's `f006343d` FIFO fix cherry-picked,
  it passes 20/20 and the full `test-rt-host` gate passes; the CP20 branch does
  not contain that prerequisite fix.
- ASan/UBSan CP20 suite: PASS.

## Limitations

- Windows Host process execution remains unsupported; the shell keeps its
  legacy launcher there. The new Host field compiles, but Windows streaming/job
  CP20 tests are deferred to the Windows Host slice.
- The launch-cost target is not met (see above).
- The leading-redirection quirk remains a reported legacy/Bash difference, not
  an independent Nift change.
