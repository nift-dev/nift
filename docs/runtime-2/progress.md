# Progress

_Last updated: 2026-10-17_

## Where things stand

| CP | Status | Branch / commit(s) | Notes |
| --- | --- | --- | --- |
| CP00 | complete | `runtime-2` | Baseline in `benchmarks.md` |
| CP01 | complete — Linux, macOS, Windows (CI @262d3ec) | `runtime-2` | Defect D1 fixed; review F1–F7 resolved or documented; Windows coarse-clock path compiled and tested in CI (`checkpoints/cp01-modified-race.md`) |
| CP02 | complete — Linux, macOS, Windows (CI @262d3ec) | `runtime-2` | Defect D2 fixed; review findings 1–7 fixed, 8/10a/10b open for CP16, 9 documented; @input shadow probe fixed after CI (`checkpoints/cp02-dynamic-reads.md`) |
| CP03 | complete — Linux, macOS, Windows (CI @262d3ec) | `runtime-2` | D-002 filesystem freedom; contract tests changed (incl. dependency_status escaping symlink); regression suite `runtime-2` updated (`checkpoints/cp03-filesystem-freedom.md`) |
| CP04 | deferred (owner, D-006) | not committed | 12.4× met but behaviour diverges; probes in `tests/runtime2/vm-corpus/cp04/`; patch kept outside the branch |
| CP05 | complete (Linux); macOS/Windows: CI after push | `runtime-2` | Flat call in loop −10.5% (top level −4.6%); review: unbounded cache memory and latched flags fixed (`checkpoints/cp05-call-path.md`) |
| CP06 | complete — Linux, macOS, Windows (CI @262d3ec) | `runtime-2` | Template scanner dispatch: −57.8% full-build instructions, byte-identical; review found no output difference in ~37k cases (`checkpoints/cp06-scanner-dispatch.md`) |
| CP08 | implemented; review findings being addressed | `runtime-2-cp08` | Host-op trace, `tools/rt-diff`, semantics v1 (`checkpoints/cp08-semantics-harness.md` on that branch) |

`runtime-2` holds checkpoints that passed every gate. `runtime-2-dev` holds work
whose review is still open; it is rebased onto `runtime-2` before each delivery.

## Environment

- Work happens in a cloud clone of public `main` at `ce8cd36` (D-003).
- Linux only (D-004). macOS/Windows: pending CI, which requires an
  owner-approved push.
- Known environmental test failures on this host (also on the baseline): the
  sandbox runs as root (test-v43-language, test-unreadable-source,
  test-dependency-status), lacks `/usr/bin/time` (two v48 guard tests), and
  has no usable pty (test-progress-pty).

## Local checkout divergence

Checked with git through the device bridge: `/home/nick/Repositories/nift/nift`
HEAD is `00dda50` = `ce8cd36` plus a version bump touching 7 files
(HANDOVER.md, README.md, docs/guarantees/registry.json,
docs/handover/RELEASES.md, snap/snapcraft.yaml, src/CLI.cpp,
tests/version_consistency_test.py); no modified tracked files; untracked
evidence directories only. `ce8cd36` is its parent, so the bundle's
prerequisite is present, and no `runtime-2` commit touches those 7 files, so
the patches apply without overlap (not yet applied there: nothing is written
to the owner's git repository without approval).

## Next action

1. CP08 (semantics + differential harness, prioritised by
   the owner), CP05 and CP07.
2. CI (D-007): push `runtime-2` to `origin` once credentials are available
   (see below) and run the cross-platform workflows.
3. CP04 resumes after CP08 (D-006).
3. Carried to CP16 (Host ops): reads inside `async` functions, OS (not
   lexical) resolution of `link/..`, globs in `cp()`/`rm()`, recording
   `@path` targets separately from presence checks.

Notes on history:
- Commit `1ecb2ae` is titled "WIP CP03"; its content is the CP03
  implementation and the review fixes follow in `a02072e`.
- `runtime-2-dev` was rebased once (CP02/CP03 split onto the final CP01) after
  the first delivery bundle. Neither branch has been pushed; fetch the new
  bundle with forced refspecs (see `APPLY.md`). `runtime-2` itself has only
  moved forward.

## Open escalations

None. D-006 decided by the owner (defer CP04).

## Regression suite

`nift-dev/nift-regression-suite` is a certification gate for every checkpoint
(`./run-contract.sh <nift>`). On this host v4.12.0 fails 4 modules
(unreadable_source: runs as root; v44_executable_script: needs `nift` on PATH;
pagination, v45_target: timing flakes) and `incremental_new_features` is flaky
(2/20 on v4.12.0). The suite's `runtime-2` branch encodes D-002 (D-008).

## CI status (D-007)

Not yet pushed: this session has no GitHub credentials (the session's GitHub
link is missing; the owner's machine cannot reach github.com over SSH from the
sandbox). Pushing to `runtime-2` alone also does not trigger most workflows,
which run on `push` to `main`, on `pull_request`, or on `workflow_dispatch`;
cross-platform runs need `workflow_dispatch` on the branch:
`checkpoint-10-cross-platform.yml`, `v44-cross-platform.yml` and
`test-integrity.yml` all accept it.
