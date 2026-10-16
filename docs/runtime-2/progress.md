# Progress

_Last updated: 2026-10-17_

## Where things stand

| CP | Status | Branch / commit(s) | Notes |
| --- | --- | --- | --- |
| CP00 | complete | `runtime-2` | Baseline in `benchmarks.md` |
| CP01 | complete (Linux); Windows pending CI | `runtime-2` | Defect D1 fixed; adversarial review F1–F7 resolved or documented (`checkpoints/cp01-modified-race.md`) |
| CP02 | implemented, under review | `runtime-2-dev` | Defect D2: dynamic reads recorded (`checkpoints/cp02-dynamic-reads.md`) |
| CP03 | implemented, under review | `runtime-2-dev` | D-002 filesystem freedom; six contract tests changed (`checkpoints/cp03-filesystem-freedom.md`) |

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

Checked through the file bridge: `/home/nick/Repositories/nift` HEAD is
`00dda50` = `ce8cd36` plus a version bump only; no modified tracked files; 37
untracked evidence files. The bundle and patches apply cleanly on it.

## Next action

1. Close CP02/CP03 review findings; rebase `runtime-2-dev` onto `runtime-2`;
   split into CP02 and CP03 commits; promote to `runtime-2`.
2. CP04 (prepared path for top-level code) per `checkpoints.md`.

## Open escalations

None.
