# Progress

_Last updated: 2026-10-17_

## Where things stand

| CP | Status | Branch / commit(s) | Notes |
| --- | --- | --- | --- |
| CP00 | complete | `runtime-2` | Baseline in `benchmarks.md` |
| CP01 | complete (Linux); Windows pending CI | `runtime-2` | Defect D1 fixed; adversarial review F1–F7 resolved or documented (`checkpoints/cp01-modified-race.md`) |
| CP02 | complete (Linux); macOS/Windows pending CI | `runtime-2` | Defect D2 fixed; review findings 1–7 fixed, 8/10a/10b open for CP16, 9 documented (`checkpoints/cp02-dynamic-reads.md`) |
| CP03 | complete (Linux); macOS/Windows pending CI | `runtime-2` | D-002 filesystem freedom; six contract tests changed (`checkpoints/cp03-filesystem-freedom.md`) |

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

1. CP04 (prepared path for top-level code) per `checkpoints.md`, on
   `runtime-2-dev`.
2. Carried to CP16 (Host ops): reads inside `async` functions, OS (not
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

None.
