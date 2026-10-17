# Progress

_Last updated: 2026-10-18_

## Where things stand

| CP | Status | Branch / commit(s) | Notes |
| --- | --- | --- | --- |
| CP00 | complete | `runtime-2` | Baseline in `benchmarks.md` |
| CP01 | complete — Linux, macOS, Windows (CI @262d3ec) | `runtime-2` | Defect D1 fixed; review F1–F7 resolved or documented; Windows coarse-clock path compiled and tested in CI (`checkpoints/cp01-modified-race.md`) |
| CP02 | complete — Linux, macOS, Windows (CI @262d3ec) | `runtime-2` | Defect D2 fixed; review findings 1–7 fixed, 8/10a/10b open for CP16, 9 documented; @input shadow probe fixed after CI (`checkpoints/cp02-dynamic-reads.md`) |
| CP03 | complete — Linux, macOS, Windows (CI @262d3ec) | `runtime-2` | D-002 filesystem freedom; contract tests changed (incl. dependency_status escaping symlink); regression suite `runtime-2` updated (`checkpoints/cp03-filesystem-freedom.md`) |
| CP04 | deferred (owner, D-006) | not committed | 12.4× met but behaviour diverges; probes in `tests/runtime2/vm-corpus/cp04/`; patch kept outside the branch |
| CP05 | complete — Linux, macOS, Windows (CI @c5db14d) | `runtime-2` | Flat call in loop −10.5% (top level −4.6%); review: unbounded cache memory and latched flags fixed (`checkpoints/cp05-call-path.md`) |
| CP06 | complete — Linux, macOS, Windows (CI @262d3ec) | `runtime-2` | Template scanner dispatch: −57.8% full-build instructions, byte-identical; review found no output difference in ~37k cases (`checkpoints/cp06-scanner-dispatch.md`) |
| CP07 | complete — Linux, macOS, Windows (CI @aba3a50) as leak/resource fix; launch-cost gate **deferred, not passed** (D-011) | `runtime-2` | Anonymous capture files; pipe attempt rejected and preserved (`checkpoints/cp07-process-capture.md`) |
| CP08 | complete — Linux, macOS, Windows (evidence split across two commits, see below) | `runtime-2` | Host-op trace, `tools/rt-diff` harness (policies, minimiser, 83 self-tests), corpus, semantics v1.1; two adversarial review rounds addressed (`checkpoints/cp08-semantics-harness.md`) |
| CP09 | complete — Linux, locally certified (`make certify` @0dc60c6: 0 FAIL); cross-platform at the CP09–CP11 milestone | `runtime-2` | `src/rt` value model; review round 1: 3 defects + 4 risks fixed (reference-invalidation contract documented only), verified by the reviewer's repros ported as regression tests; no second review round (`checkpoints/cp09-value-model.md`) |
| CP10 | complete — Linux, locally certified (`make certify` @0dc60c6: 0 FAIL); cross-platform at the CP09–CP11 milestone | `runtime-2` | Script lexer/parser; review round 1: 10 defects + 2 risks fixed by Opaque fallback, new negative gate against legacy (0 failures, 28,464 programs); no second review round (`checkpoints/cp10-parser.md`) |

### CP08 certification evidence (owner-accepted 2026-10-18)

| Check | Commit | Result |
| --- | --- | --- |
| Checkpoint-10 cross-platform equivalence (Linux, macOS, Windows) | `bce5913` | pass |
| v4.4/v4.5 cross-platform (Linux, macOS, Windows) | `bce5913` | pass |
| Regression suite `runtime-2` vs core `runtime-2` | `bce5913` | pass |
| Test integrity guards (binding gates, static scan, aggregate evidence on ubuntu and macOS, fast contract suites) | `600fe99` | pass |

`bce5913..600fe99` changes no product source (`git diff -- src` is empty): the
rt-diff `trace-async-reorder` mutant fix, the certify runner and its guard, CI
annotations, private temp files in three tests, and docs (D-012). The integrity
job at `bce5913` failed only on the mutant self-test fixed in `aed991b`. No full
matrix was re-run to consolidate (D-012).

### Certification levels (D-012)

- **locally certified** — `make certify` on Linux with every result PASS or classified;
- **cross-platform certified** — the CI workflows above pass on Linux, macOS and Windows;
- **release certified** — full matrix on the exact commit to be promoted, before merge to `main`.

CP09–CP11 are certified locally; the next full matrix runs at the CP09–CP11
integration milestone.

`runtime-2` holds checkpoints that passed every gate. `runtime-2-dev` holds work
whose review is still open; it is rebased onto `runtime-2` before each delivery.

## Environment

- Work happens in a cloud clone of public `main` at `ce8cd36` (D-003); local
  certification on Linux, cross-platform via GitHub Actions on `runtime-2` (D-007, D-012).
- Known environmental failures on this host (also on the baseline) are
  classified by `make certify` (`certification.md`): root-only failures,
  missing `/usr/bin/time`, no usable pty.
- Load-sensitive wall-clock guards run exclusively and keep their thresholds
  (`certification.md` § Load-sensitive measurements).

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

1. CP09 (value model) and CP10 (parser): finish local validation, adversarial
   review and docs; promote as locally certified.
2. CP11 (VM proof of concept, GO/NO-GO): compiled Nift programs through the VM,
   compared with v4.12 and CPython on equivalent workloads.
3. Full cross-platform matrix at the CP09–CP11 integration milestone.
4. CP04 resumes after the VM decision (D-006). Carried to CP16: reads inside
   `async` functions, OS resolution of `link/..`, globs in `cp()`/`rm()`, `@path`
   targets, pipe-based execution (D-011).

Notes on history:
- Commit `1ecb2ae` is titled "WIP CP03"; its content is the CP03
  implementation and the review fixes follow in `a02072e`.
- `runtime-2-dev` was rebased once (CP02/CP03 split onto the final CP01) after
  the first delivery bundle. Neither branch has been pushed; fetch the new
  bundle with forced refspecs (see `APPLY.md`). `runtime-2` itself has only
  moved forward.

## Open escalations

None. CP07 launch cost decided by the owner (D-011, option a).

## Regression suite

CI: `.github/workflows/runtime2-regression-suite.yml` runs the suite's
`runtime-2` branch against the core `runtime-2` branch on every push to it
(D-008: neither `main` changes until both are promoted together).

`nift-dev/nift-regression-suite` is a certification gate for every checkpoint
(`./run-contract.sh <nift>`). On this host v4.12.0 fails 4 modules
(unreadable_source: runs as root; v44_executable_script: needs `nift` on PATH;
pagination, v45_target: timing flakes) and `incremental_new_features` is flaky
(2/20 on v4.12.0). The suite's `runtime-2` branch encodes D-002 (D-008).

## CI status (D-007, D-012)

`runtime-2` is pushed to `origin` (certified commits only). Workflows run by
`workflow_dispatch` on the branch, narrowest workflow first; the full matrix only
at integration boundaries, for platform-sensitive changes, or before promotion.
