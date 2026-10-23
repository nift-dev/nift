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
- Scripted/non-interactive `&` uses the Host `JobTable`: each group is a Host
  `ProcessGroup`, `wait`/`wait_all` reap it, `jobs` reports its state, and the
  table terminates live groups on shell teardown. Interactive foreground and
  background jobs stay on the legacy `ShellJobTable` until CP21 (J1/R2).
- `SpawnStage` gained `stderr_to_stdout`; the POSIX child applies it after
  stdout setup, so `2>&1` works in any stage (including a middle pipeline
  stage) and merges into the stage's actual stdout destination.
- Interactive foreground and background commands use the complete legacy
  job-control path (process group, terminal hand-off, Ctrl-C/Ctrl-Z, `fg`,
  `bg`, `jobs`) until CP21. Scripted commands use Host; pipeline stages have
  no terminal ownership and SIGINT semantics remain as before.
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
batch. The R2 correction defers exec-report reads until all pipeline stages
are forked (H2); the final interleaved 40-round Linode run (GCC 13.3,
`g6-dedicated-4`, temporary HOME) is:

| Workload | CP20 R2 / pre-CP20 |
|---|---:|
| single | **1.011×** |
| 3-stage | **1.015×** |

Both are within the P2 ≤1.03 gate. The earlier R1 candidate was 1.36–1.63× on
3-stage because it blocked on each stage's exec report before forking the
next. Interactive (PTY, legacy path) is at parity with pre-CP20. Bash is not
claimed as matched: the final Linode single/3-stage ratios to Bash are about
1.16–1.23 and 1.09–1.15 respectively.

## Mutants

`NIFT_CP20_MUTANT=1..7` compile in `src/CLI.cpp`/`src/shell/Spawn.cpp`:
M1 buffers pipelines (streaming), M2 treats `>>` as `>`, M3 ignores `2>&1`,
M4 leaks env assignments, M5 makes `wait` return immediately, M6 reports a
middle-stage failure as the pipeline status, and M7 is a genuine never-reaped
background group caught by the `/proc` zombie check. All seven are rejected by
the ordinary CP20 suite.

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
  legacy launcher for every context on Windows (W1). Interactive Windows now
  falls back to that launcher (F1); the Windows CI fixture covers console
  pipeline bytes and forced-interactive routing, but was not run locally.
- H2 is implemented and met the P2 gate on the final 40-round Linode run.
- The leading-redirection quirk remains a reported legacy/Bash difference, not
  an independent Nift change.
