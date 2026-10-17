# Decisions (append-only)

## D-000 — Operating model (owner, 2026-10-17)
Agent-led engineering. Checkpoints pass on evidence. Escalate to the owner only
for: a significant architecture change; a major compatibility break not listed
here; a performance objective that appears unattainable; a choice between
materially different capabilities; paid infrastructure; any release, push or
deployment. Same-model reviews are not described as independent.

## D-001 — Architecture approved (owner, 2026-10-17)
Register bytecode VM, 16-byte tagged values, COW aggregates, typed handles,
one engine for scripts, shell and templates, per the Runtime 2.0 design report.
The legacy evaluator must be retired; a permanent two-engine state is not
acceptable.

## D-002 — Filesystem freedom (owner, 2026-10-17)
General filesystem operations are not confined to the project directory.
Absolute paths, `..`, symlinks and neighbouring repositories work with normal
OS semantics. Protections for Nift-owned state (metadata, managed outputs,
cleanup) remain. Restrictions for embedders are opt-in host policies and are
not described as a security sandbox. Dependency tracking must cover external
files.

## D-003 — Work location (campaign, 2026-10-17)
The owner's checkout (`/home/nick/Repositories/nift/nift`, inside the
`/home/nick/Repositories/nift` workspace) is reachable only through a file
bridge, later also a sandboxed shell; builds and tests run in a cloud clone of public
`main` (`ce8cd36`). Work is committed on branch `runtime-2` there and delivered
as a git bundle plus patch series. Before each delivery the local checkout's
HEAD is compared with `ce8cd36`; any divergence is recorded in `progress.md`.

## D-004 — Cross-platform evidence (campaign, 2026-10-17)
Only Linux can be executed in the campaign environment. macOS and Windows
verification requires the repository's GitHub workflows, which need a push the
owner has not authorised. Checkpoints therefore record "Linux verified; macOS/
Windows pending CI" until the owner pushes the branch. No checkpoint is called
cross-platform verified without CI results.

## D-005 — Gate on instructions, report wall time (campaign, 2026-10-17)
The campaign host is a shared 2-vCPU VM. Exit gates use callgrind instruction
counts (deterministic); reported results use wall-time medians with the host
fingerprint.

## D-006 — CP04 deferred (owner, 2026-10-17)
Running top-level code on the prepared path met the 8× gate (12.4×) but changed
observable behaviour: the prepared and canonical evaluators diverge (16 suite
targets, 4/44 parity probes). Owner decision: defer CP04 until the semantics
specification and differential harness (CP08) exist; do not merge the two
legacy execution paths to pass an interim gate; do not weaken tests or change
language behaviour for interim performance targets. Preserve the measurements,
experimental patch and failing cases; the divergences join the VM regression
corpus. Reassess CP05's dependency on CP04. Engineering effort goes to the
compiled runtime; CP08 is prioritised before substantial VM implementation.

## D-007 — CI push authorised (owner, 2026-10-17)
Push certified checkpoint commits to a `runtime-2` branch on the Nift
repository's `origin` (verified as `github.com/nift-dev/nift`, the owner's
checkout's origin, which contains base `ce8cd36`). No force-push, no change to
`main`, no releases or deploys; branch must contain only checkpoint commits (no
credentials, generated artifacts or CP04 experimental code). Run the Linux,
macOS and Windows workflows on it and fix failures before calling a checkpoint
fully certified; keep baseline results to separate pre-existing failures.

## D-008 — Branch promotion (owner, 2026-10-17)
`nift` and `nift-regression-suite` each carry Runtime 2.0 work on a
`runtime-2` branch. Neither `main` changes until the owner promotes both
together, so `main` of each stays consistent with v4.12.0 (the suite's
`runtime-2` encodes D-002 and fails against v4.12.0 by design).

## D-009 — Language semantics decisions (owner, 2026-10-17)
Resolves the eight `semantics.md` rows CP08 raised. The legacy interpreter is
not changed to match; these define Runtime 2.0 behaviour, and the differential
harness classifies the resulting differences as approved changes, each with a
specific rule, test and justification (never a broad suppression).

| # | Topic | Decision |
| --- | --- | --- |
| 1 | Type-error wording loop vs top level | A: one wording (top-level) everywhere |
| 2 | `const` reassignable inside loops | A: immutable everywhere |
| 3 | Built-in vs user-name precedence | **B: lexical bindings and user-defined functions take precedence over built-ins.** Built-ins must stay reachable when shadowed by some unambiguous mechanism (to be designed; no new standard-library namespace unless the wider language design justifies it). Compatibility change: affected existing programs are documented, with regression tests showing old and intended behaviour |
| 4 | Built-in resolution inside loops | A: consistent with #3 everywhere |
| 5 | Error location in called functions | A: the actual location in the function |
| 6 | `"$[x]"` inside loops | A: interpolated everywhere |
| 7 | Catchable runtime errors | A for now: preserve during the initial VM migration. Recorded future objective: ordinary runtime failures (undefined callable, bad index, type errors) become structured, catchable exceptions; fatal internal errors, memory exhaustion and termination stay distinct |
| 8 | Dynamic-scope (free name) warnings | B, later: opt-in strict-mode warning |

`semantics.md` distinguishes, per row: legacy behaviour; intended Runtime 2.0
behaviour; approved compatibility changes; deferred language improvements.

## D-010 — Certification process (owner, 2026-10-17)
- Checkpoint certification requires `make test-all` (not only `make test`),
  the `tests/runtime2` gates, the regression suite (`nift-regression-suite`
  `runtime-2`) and, from CP08 on, the differential harness as a mandatory
  regression gate; environmental baseline failures are listed separately from
  regressions.
- macOS/Windows are claimed only after the cross-platform CI workflows pass on
  the pushed commit (`checkpoint-10-cross-platform`, `v44-cross-platform`,
  `test-integrity`).
- An automated guard keeps comprehensive suites (conformance, host seam,
  C ABI) inside the certification target so they cannot be silently omitted.
- Same-binary differential runs establish the comparison infrastructure, not
  language correctness.
- Only certified commits are pushed, only to `runtime-2`; no force-push,
  no `main`, no releases. Routine checkpoint completion needs no approval;
  escalate only consequential architecture or compatibility decisions,
  genuine blockers, or publishing.

