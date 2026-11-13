# Runtime 2.0 platform subsets for the first cross-platform certification (D-077 #4)

Owner, 2026-11-12 04:25 (D-077 #4):
- The first certification covers Linux, macOS and Windows.
- **FreeBSD is out of the first certification.** It is a separate platform qualification after the baseline: a
  decision about certification scope, not about support. The FreeBSD CI (`freebsd.yml`) and the portability fixes
  (#1-#3) exist on `main` only. They are to be merged and qualified after the baseline is established.
- The Windows VM test subset is written down here before any CI runs. The macOS scope is written down with equal
  precision where it excludes anything.
- There are no blanket skips: every exclusion names the construct that does not exist, or does not mean the same,
  on that platform.

## Source of truth and how a run reports

- **The table is data.** It is `tests/runtime2/platform_subset.py` (`ENTRIES`, `MAKE_TARGETS`). The table below is
  its `--markdown` output, and `--check` verifies it:
  - every exclusion has a concrete reason, never a platform name alone;
  - nothing is excluded on all three platforms;
  - every script it names exists;
  - **every test script of `make test-rt-vm`'s recipe is in the table**. A new suite cannot be left out silently:
    `--check` fails on it (negative control run: an extra script in a copy of the recipe is reported).

  `make test-runtime2-platform-subset-table` runs `--check` inside `test-rt-vm`.
  `tests/runtime2/certification_guard.sh` now includes the new workflow, so its scripts and make targets must be
  reached by `certify-targets`. Without that target the guard fails (negative control run).
- **What a run prints.** `platform_subset.py --platform P --nift BIN` runs every included entry and prints PASS or
  FAIL. It prints **EXCLUDED** with the reason for every excluded entry: an exclusion is never counted as a pass.
- **How skips are classified (review L3, N1).** The runner classifies every skip line with **certify's own
  classifier** (`scripts/certify.py` `skipped_assertions`: `SKIP_MARKERS`, `ZERO_SKIPS`, `KNOWN_CONDITIONAL`), the
  same one the Linux campaign uses. It is imported, not copied. On Windows a stand-in `pwd` module is installed,
  because certify imports `pwd` and the classifier does not use it. The classes:
  - **NOTE:** a line certify marks as not required. Examples are the documented design boundary that host_deps.py
    always prints (`SKIP [modified] edit during streaming is outside the v4.12 modified-mode contract`) and another
    platform's own message. These are not skips.
  - **EXPECTED SKIP:** a required skip that this page lists for that platform and entry (below).
  - **UNEXPECTED SKIP:** any other skip line, including `SKIPPED`, lowercase `skipped`, TAP `# SKIP`, `NOT RUN` and
    `UNVERIFIED`. It makes an entry that exits 0 **PARTIAL**, not PASS, and the run exits 2 when nothing failed.
  - So PARTIAL means "an unexpected skip".
  - Silent internal skips cannot be seen by any runner. The known ones are vm_subset's RSS check without
    `os.wait4` and vm_adversarial's flock no-op where `fcntl` is missing, both on Windows. They are noted in the
    table.
- **Whole-suite self-skips are exclusions.** A suite that skips itself entirely on a platform is excluded there with
  its reason, rather than run and counted: named_deferrals on macOS and Windows.
- **`platform_subset.py --selftest`** (in `test-runtime2-platform-subset-table`) checks the classification on
  certify's real classifier:
  - a design-boundary line is not PARTIAL;
  - `SKIP`, `SKIPPED`, lowercase, TAP `# SKIP` and `NOT RUN` lines are PARTIAL;
  - a `0 skipped` summary is not;
  - an expected macOS skip is PASS on macOS and PARTIAL on Linux;
  - a failing exit stays FAIL;
  - an excluded entry is never run.

  Mutating the not-required branch, or the exclusion check, makes the selftest fail (shown).
  A unit binary that was not built is a FAIL, not a skip.
- **The workflow.** `.github/workflows/runtime2-cross-platform.yml` is dispatch-only, not pushed and not triggered.
  On each of ubuntu-latest, macos-latest and windows-latest (MSYS2 UCRT64) it:
  - builds nift and the unit binaries;
  - runs the table;
  - runs the make targets `test-v45-embed-contracts`, `test-rt-value`, `test-runtime2-kind-bits` and
    `test-rt-source-map`;
  - on the Linux job only, runs the required certification guard `test-runtime2-certification-guard` (D-090).

  The action versions are the ones the existing certified workflows use. **They are to be pinned to commit SHAs
  before the push.** That needs a lookup, and no GitHub access was authorised here.
- **Status of the Windows and macOS columns: expected, CI-verified only.** They were derived statically: each
  harness was read for POSIX-only constructs (`resource`/RLIMIT, `preexec_fn`/`pass_fds`, `os.mkfifo`, `pty`,
  `fcntl`/`termios`, `/proc`, `/dev/zero`, shebang fakes, `ulimit`, GNU `timeout`). None of them has run on Windows
  or macOS for this candidate. A failure there is a certification finding, to be classified like any other: it is
  never turned into an exclusion without a recorded reason and review.

## Platform scope of the strongest Linux evidence (review L4)

The VM-vs-legacy corpus differential (`vm_parity`), `host_deps` and read-limit enforcement (`read_limit.py`,
`read_limit_controls.py`) are **certified on Linux only, and on macOS only where this subset includes them**:
- macOS: `vm_parity` and `host_deps` yes; the read-limit suites no;
- Windows: none of the three.

The Windows and macOS certification claims must carry this as a headline limitation. On Windows the per-suite VM
entries below cover the VM, but not the corpus differential itself.

## Counts

| Platform | Entries run | Excluded, with a reason |
|---|---:|---:|
| Linux | 89 | 0 |
| macOS | 86 | 3 (read_limit, read_limit_controls: /proc and RLIMIT_AS; named_deferrals: Linux-only programs) |
| Windows | 71 | 18 (listed in the table) |

### Expected environmental skips (EXPECTED_SKIPS)

Each one is the exact line, matched per platform and entry. Any other skip is PARTIAL.

| Platform | Entry | Expected skip line | Reason |
|---|---|---|---|
| macOS | file_copy_move_freedom | `SKIP cross-device move (/dev/shm missing or on the same filesystem)` | Darwin has no /dev/shm: the cross-device move cases need a second (tmpfs) filesystem |
| macOS | build_write_ownership | `SKIP cross-device move cases (/dev/shm missing or on the same filesystem)` | same |
| Linux | — | none: hosted ubuntu runners are not root, and they have /dev/shm (tmpfs), git, cc and GNU time | |
| Windows | — | none: the entries that print environmental skips (vm_host, vm_host_memory, packages, host_deps, the ulimit scripts) are excluded there | |

A skip the platforms turn out to need beyond these (for example the GNU `/usr/bin/time` options in vm_host_memory on
macOS) shows up as PARTIAL or FAIL on the first dispatch. It is then classified explicitly, never absorbed.

Plus the 4 make targets on all three platforms, and the required certification guard on Linux (D-090). The guard
checks the certify plan, which is platform independent; the table gives the macOS and Windows reasons. It is FAILED
today (since C140c: three skip phrases in tests/runtime2/suite_status.sh were never listed), and is expected to PASS
once the candidate fix lands.

## The subset

| Entry | Area | Linux | macOS | Windows | Note |
|---|---|---|---|---|---|
| `rt-vm-unit` | compiler/execution | run | run | run |  |
| `rt-vm-globals-unit` | compiler/execution | run | run | run |  |
| `rt-vm-modules-unit` | compiler/execution | run | run | run |  |
| `rt-vm-prepared-parse` | compiler/execution | run | run | run |  |
| `rt-lint` | compiler/execution | run | run | run | source lint, platform independent |
| `legacy-names` | compiler/execution | run | run | run |  |
| `vm_parity` | core semantics | run | run | **excluded**: Windows: rt-diff runs each case in its own POSIX session and kills the process group (start_new_session, os.killpg), and vm_parity bounds the child with RLIMIT_AS/RLIMIT_FSIZE through preexec_fn | the VM-vs-legacy corpus differential; on Windows the per-suite entries below cover the VM |
| `vm_adversarial_timeout_selftest` | core semantics | run | run | **excluded**: Windows: the harness writes a `#!/bin/sh` fake executable and chmods it; Windows cannot exec a shebang script; it also relies on SIGKILL of the fake |  |
| `vm_adversarial` | core semantics | run | run | run | its machine-wide flock is a no-op where fcntl is missing (Windows) |
| `vm_subset` | core semantics | run | run | run | its RSS check is skipped where os.wait4 is missing (Windows), by the suite itself |
| `vm_subset_gen` | core semantics | run | run | run |  |
| `vm_builtins` | core semantics | run | run | run |  |
| `vm_host` | file operations | run | run | **excluded**: Windows: the harness passes a trace descriptor as fd 3 with preexec_fn/pass_fds (os.dup2 in the child); Python's subprocess has no preexec_fn on Windows; it also creates POSIX symlinks | macOS already runs it in v44-cross-platform |
| `vm_records_coverage` | core semantics | run | run | run |  |
| `vm_path_wording` | core semantics | run | run | run |  |
| `vm_known_refused` | core semantics | run | run | run |  |
| `vm_engine_selection` | core semantics | run | run | run |  |
| `vm_former_replay` | core semantics | run | run | run |  |
| `vm_d018` | core semantics | run | run | run |  |
| `vm_context_consistency` | core semantics | run | run | run |  |
| `vm_policy_negative` | core semantics | run | run | run |  |
| `vm_review_cp12` | core semantics | run | run | run |  |
| `vm_golden` | core semantics | run | run | run |  |
| `vm_int_bounds` | core semantics | run | run | run |  |
| `vm_inventory` | core semantics | run | run | run |  |
| `vm_chain` | core semantics | run | run | run |  |
| `vm_modules` | core semantics | run | run | run |  |
| `module_host_order` | file operations | run | run | **excluded**: Windows: the harness passes a trace descriptor as fd 3 with preexec_fn/pass_fds (os.dup2 in the child); Python's subprocess has no preexec_fn on Windows |  |
| `vm_structs` | core semantics | run | run | run |  |
| `vm_globals` | core semantics | run | run | run |  |
| `semantics_modules` | core semantics | run | run | run |  |
| `semantics_modules-structs` | core semantics | run | run | run |  |
| `modules_d041` | core semantics | run | run | run |  |
| `cp12b_inventory-selftest` | core semantics | run | run | **excluded**: Windows: the harness writes a `#!/bin/sh` fake executable and chmods it; Windows cannot exec a shebang script (the NIFT_VM=classify-all wrapper) |  |
| `safety` | memory/mutation safety | run | run | run |  |
| `safety-fuzz-selftest` | memory/mutation safety | run | run | **excluded**: Windows: the harness writes a `#!/bin/sh` fake executable and chmods it; Windows cannot exec a shebang script (fake nift binaries that must fire each check) |  |
| `safety-fuzz` | memory/mutation safety | run | run | run |  |
| `b3_structs` | memory/mutation safety | run | run | run |  |
| `b4_dispatch` | memory/mutation safety | run | run | run |  |
| `b5-evalorder` | memory/mutation safety | run | run | run |  |
| `b5-evalorder-controls` | memory/mutation safety | run | run | run |  |
| `b5-operators` | memory/mutation safety | run | run | run |  |
| `b5-operators-controls` | memory/mutation safety | run | run | run |  |
| `refsem_a` | memory/mutation safety | run | run | run |  |
| `cycle_refusal_routes` | memory/mutation safety | run | run | run |  |
| `collections` | memory/mutation safety | run | run | run |  |
| `closures_s1` | memory/mutation safety | run | run | run |  |
| `for_s2` | memory/mutation safety | run | run | run |  |
| `vm_host_memory` | memory/mutation safety | run | run | **excluded**: Windows: the cases read from a FIFO created with os.mkfifo, which Windows does not have |  |
| `cp22-legacy` | core semantics | run | run | run |  |
| `cp22-vm` | core semantics | run | run | run | the normal binary only: the CP13B switch binary is Linux-campaign only (see the doc) |
| `cp22-refuse` | core semantics | run | run | run |  |
| `cp22-default` | core semantics | run | run | run |  |
| `cp22-vm-only` | core semantics | run | run | run |  |
| `cp22-trace` | core semantics | run | run | run |  |
| `cp22-binding` | core semantics | run | run | run |  |
| `cp22-controls` | core semantics | run | run | **excluded**: Windows: the harness writes a `#!/bin/sh` fake executable and chmods it; Windows cannot exec a shebang script (a dead `#!/bin/sh` callee) |  |
| `cp22-print_order` | core semantics | run | run | run | the output-order repair adds Windows std-stream identity code: CI-verified only |
| `cp23-compatibility` | core semantics | run | run | run |  |
| `coro_module` | core semantics | run | run | run |  |
| `named_deferrals` | core semantics | run | **excluded**: macOS: named_deferrals.py pins its refusals with Linux-only programs (FFI through libc.so.6, an `echo` executable) and on any other platform prints one SKIP line and exits 0 without checking anything | **excluded**: Windows: named_deferrals.py pins its refusals with Linux-only programs (FFI through libc.so.6, an `echo` executable) and on any other platform prints one SKIP line and exits 0 without checking anything |  |
| `deadcode_admission` | core semantics | run | run | run |  |
| `args_cmd` | core semantics | run | run | run | creates one symlink: windows-latest runs as an administrator, so os.symlink works there |
| `import_depth` | core semantics | run | run | run |  |
| `precise_positions` | core semantics | run | run | run |  |
| `capture_origins` | core semantics | run | run | run |  |
| `literals` | core semantics | run | run | run |  |
| `literals-decimal` | core semantics | run | run | run |  |
| `vm_default` | core semantics | run | run | **excluded**: Windows: the suite drives nift through a pseudo-terminal (pty.fork) and SIGKILL |  |
| `packages` | core semantics | run | run | **excluded**: Windows: the suite takes an fcntl lock, creates POSIX symlinks and SIGKILLs a stuck child |  |
| `diagnostics` | core semantics | run | run | run | needs GNU timeout(1): the workflow puts coreutils' gnubin on PATH on macOS |
| `diagnostics-changes-old` | core semantics | run | run | run |  |
| `diagnostics-changes-new` | core semantics | run | run | run |  |
| `diagnostics-codes` | core semantics | run | run | run |  |
| `diagnostics-json` | core semantics | run | run | run |  |
| `d040_witnesses` | core semantics | run | run | run | needs rt-parse-dump |
| `cp25r-differential` | core semantics | run | run | run |  |
| `cp25r-post-cert-refusals` | core semantics | run | run | run | D-077 #1 refusal contracts; a missing corpus is NOT RUN (exit 2), never a pass |
| `approved_refusal-selftest` | core semantics | run | run | run |  |
| `rt_parser_corpus` | compiler/execution | run | run | **excluded**: Windows: the deep-nesting check runs the parser under `ulimit -s 1024` (bash), which MSYS2 cannot set for a native process | needs rt-parser-unit, rt-parse-dump and rt-legacy-oracle |
| `fs_freedom` | file operations | run | run | run | symlinks: the workflow sets MSYS=winsymlinks:nativestrict on Windows |
| `dynamic_reads` | file operations | run | run | run |  |
| `dynamic_reads_review` | file operations | run | run | run |  |
| `host_deps` | file operations | run | run | **excluded**: Windows: every run is bounded by RLIMIT_AS/RLIMIT_FSIZE (the POSIX-only `resource` module, through preexec_fn); the bound is the point of the suite |  |
| `file_copy_move_freedom` | file operations | run | run | **excluded**: Windows: every run is wrapped in `ulimit -f/-v` and GNU `timeout`; MSYS2 bash cannot set RLIMIT_AS (-v) | macOS: CI-verified (Darwin may reject `ulimit -v`) |
| `build_write_ownership` | file operations | run | run | **excluded**: Windows: every run is wrapped in `ulimit -f/-v` and GNU `timeout`; MSYS2 bash cannot set RLIMIT_AS (-v) | macOS: CI-verified (Darwin may reject `ulimit -v`) |
| `read_limit` | file operations | run | **excluded**: macOS: the harness reads /proc (VmPeak/VmHWM of the child, leftover producers by cmdline, zombies), which Darwin does not have, and RLIMIT_AS is not enforced by the Darwin kernel, so the address-space bound proves nothing | **excluded**: Windows: every run is bounded by RLIMIT_AS/RLIMIT_FSIZE (the POSIX-only `resource` module, through preexec_fn); the bound is the point of the suite; it also needs /proc, mkfifo, /dev/zero and /dev/full | D-077 #3 contract; Linux certifies the read limit |
| `read_limit_controls` | file operations | run | **excluded**: macOS: the harness reads /proc (VmPeak/VmHWM of the child, leftover producers by cmdline, zombies), which Darwin does not have, and RLIMIT_AS is not enforced by the Darwin kernel, so the address-space bound proves nothing; the paced FIFO uses FIONREAD (termios) | **excluded**: Windows: every run is bounded by RLIMIT_AS/RLIMIT_FSIZE (the POSIX-only `resource` module, through preexec_fn); the bound is the point of the suite; it also needs mkfifo, fcntl/termios and /dev/zero |  |
| `read_limit_contract_controls` | file operations | run | run | **excluded**: Windows: every run is bounded by RLIMIT_AS/RLIMIT_FSIZE (the POSIX-only `resource` module, through preexec_fn); the bound is the point of the suite | macOS: CI-verified (Darwin may ignore RLIMIT_AS; the controls do not depend on it) |
| `make test-v45-embed-contracts` | embedding | run | run | run | already run on all three in v44-cross-platform |
| `make test-rt-value` | memory/mutation safety | run | run | run | value layer, heap budget and OOM units |
| `make test-runtime2-kind-bits` | compiler/execution | run | run | run |  |
| `make test-rt-source-map` | compiler/execution | run | run | run |  |
| `make test-runtime2-certification-guard` | certification integrity | run | **excluded**: macOS: the guard reads the GNU make database of the whole certify plan through scripts/certify.py; it certifies the plan itself, which is platform independent, so the Linux job runs it (D-090) | **excluded**: Windows: scripts/certify.py imports the POSIX-only pwd module and the guard drives GNU make -p and bash over the POSIX plan; the plan is platform independent and the Linux job runs it (D-090) | required (D-090); FAILED since C140c (three unlisted skip phrases in tests/runtime2/suite_status.sh); expected PASS once the candidate fix lands |

## Already covered on Windows and macOS by the existing workflows

These run on all three platforms today and stay as they are. On the VM default they are VM coverage too.

- **`checkpoint-10-cross-platform.yml`:**
  - the v4.9/v4.10 build, selector, filesystem and JSON contracts (`test-v410-*`, `test-v49-*`, `test-v48-*`,
    `test-runtime-value`);
  - the consumer snapshots;
  - process hardening;
  - native SHA-256;
  - `nift serve`: Linux full, macOS core, Windows through `serve_windows_lifecycle.ps1`;
  - the ownership unit test;
  - the normalised behavioural corpus, compared across the three platforms.

  On Windows, the POSIX-only `test-dependency-status` and `test-dependency-path-identity` steps have native
  Windows variants. `test-ownership-concurrency` is POSIX-only.
- **`v44-cross-platform.yml`:** the execution shell, language foundation, FS-TYPE, restricted shell, CP19 dispatch,
  automation and hooks, the native runtime wall, the embedding contracts, local packages and version consistency.
  - The full CP19 dispatch suite (pty/coreutils) is POSIX-only; Windows runs `test-cp19-s1-dispatch-unit`.
  - The CP16 VM host-natives differential (`vm_host.py`) is POSIX-only.
- **`gate6ar-ffi.yml` and `gate6b-bytes.yml`:** FFI and bytes, including MinGW `warnings-check`.
- **`test-integrity.yml`:** Linux and macOS.

## Linux-campaign only (not platform exclusions)

These gates are certified only by the B21 Linux campaign (docs/runtime-2/certification-r2-plan.md). They need a
toolchain, a build or a privilege that the hosted macOS and Windows runners do not provide, or the owner has placed
them on Linode:
- the GCC 15.2 toolchain preflight;
- ASan, UBSan and TSan lanes (MinGW has no ASan/UBSan runtime; Makefile: "Windows runs the unsanitized builds only");
- the mutation gates (`*-mutants`, `read_limit_mutants.sh`);
- the CP13B switch binary lanes (`test-runtime2-cp22`'s switch half, `cp22/adversarial.py`);
- `test-cp23-certify` (Linux-gated in the Makefile);
- the CP21 terminal-recovery lanes (Linux-gated);
- root-only reruns;
- valgrind;
- the website-build benchmark against v4.12.0.

## Platform notes

- **Comparator sort (F5, release blocker).**
  - `method:sort` with a comparator compiles only when `__GLIBCXX__` is GCC 15.2's (`src/rt/compiler_collection_sort.cpp`).
  - On macOS (libc++) and Windows (MinGW GCC) the VM refuses comparator sort before any effect, and the subset
    certifies that refusal there.
  - The working comparator sort is certified only on the GCC 15.2 Linux lane.
- **Output-order repair (step 1 of D-077 #5, pending).**
  - The repair adds standard-stream identity checks for platform equivalents ("don't assume /dev on Windows",
    D-072 #1).
  - Its Windows identity code is **CI-verified only**: no Windows host is available to this project outside
    GitHub Actions.
  - `cp22-print_order` and the output-order tests the repair adds belong in this table, run on Windows, when the
    repair lands.
- **macOS and RLIMIT_AS.** Darwin may ignore or reject RLIMIT_AS. The entries that set it, but whose assertions do
  not depend on it, run on macOS and are CI-verified:
  - `vm_parity`, `host_deps` and `read_limit_contract_controls` set it through Python;
  - `file_copy_move_freedom` and `build_write_ownership` set it through `ulimit -v`.

  The two read-limit suites whose point is the address-space bound and `/proc` are excluded on macOS.
- **macOS and GNU `timeout`.** `diagnostics/run.sh` and the shell harnesses call `timeout`. The workflow puts
  coreutils' `gnubin` on PATH on macOS.
- **Windows symlinks.** The workflow sets `MSYS=winsymlinks:nativestrict`, as the FS-TYPE and SHA-256 steps of the
  existing workflows do. `windows-latest` runs as an administrator, so `os.symlink` works.

## Open questions (owner)

1. Whether to add the `nift-regression-suite` `runtime-2` contracts on macOS as well. Today
   `runtime2-regression-suite.yml` runs them on ubuntu only.
2. Whether `nightly-deep.yml` (parser fuzz with sanitizers) is dispatched once on the frozen tip.
3. When FreeBSD is qualified after the baseline, whether the same table gains a `freebsd` column. The table and
   `--check` are written to take one.
