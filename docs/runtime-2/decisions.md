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
  Amended after CI @084f09f: the certification target is `make certify`
  (`test-all` plus every suite named in the CI workflows); the guard parses
  the workflows so a newly added CI suite cannot be skipped locally.
  Amended again (owner, 2026-10-17): `make certify` runs
  `scripts/certify.py` — isolated (exclusive targets serialised),
  classified results (PASS / FLAKY / SKIPPED-ENVIRONMENTAL / UNVERIFIED /
  BASELINE-FAILURE / FAIL; only PASS counts as a pass), tiered validation
  (quick / focused / full / CI / milestone). See
  `docs/runtime-2/certification.md`.
- Same-binary differential runs establish the comparison infrastructure, not
  language correctness.
- Only certified commits are pushed, only to `runtime-2`; no force-push,
  no `main`, no releases. Routine checkpoint completion needs no approval;
  escalate only consequential architecture or compatibility decisions,
  genuine blockers, or publishing.

## D-011 — CP07 outcome and pipe-based execution in CP16 (owner, 2026-10-17)
- CP07 is certified as a correctness/resource fix: anonymous capture files,
  no leaked `/tmp/nift-proc-*`, all existing observable behaviour preserved
  (background-process lifetime, regular-file stdout, low-fd behaviour). The
  launch-cost gate (≤ Bash + 15%) is recorded as **deferred, not passed**;
  the benchmark is not weakened.
- The rejected pipe implementation is kept as evidence (patch outside the
  branch, `cp07-pipe-attempt/`; ~31% faster, 0.87× Bash `$()`), with the
  reasons it was rejected (hang/unbounded memory with chatty descendants,
  timing-dependent capture, SIGPIPE after parent exit, leaked helper
  processes, FIFO stdout, earlier failure at low fd limits).
- CP16 gains an explicit objective: pipe-based capture and streaming in the
  VM process layer, with caller-selectable output modes (pipe capture, file
  capture, streaming, background/detached), explicit pipe ownership,
  backpressure and bounded buffering, cancellation and termination, defined
  child lifetime, cross-platform behaviour and job control. These are
  designed architecturally, not with idle timeouts. Existing APIs keep their
  documented behaviour unless a compatibility change is approved.
- Targets: pipe mode at least competitive with Bash; file-backed mode reports
  OS overhead separately from Nift overhead (no heroic tuning of OS-bound
  cost). CP16 benchmarks both modes over trivial launches, small and large
  captures, simultaneous stdout/stderr, multi-stage pipelines, background and
  long-running processes, interactive commands, cancellation, high
  concurrency and low fd limits, measuring wall time, CPU, memory and leaks.
- Principle: preserve correctness in Nift 4.x, but 4.x implementation choices
  do not constrain the VM design; every VM difference is deliberate and tested.

## D-012 — Tiered CI (owner, 2026-10-17)
Three certification levels: **locally certified** (checkpoint gates run
locally: `make certify`, adversarial review, differential, performance and
memory gates) → **cross-platform certified** (the GitHub Actions matrix) →
**release certified**. Level 1 (development) runs targeted tests only. Level 2
(checkpoint completion) does not need GitHub CI. Level 3 (the full matrix)
runs every 3–5 checkpoints, at major compiler/VM integration boundaries, for
changes that materially affect cross-platform behaviour (process execution,
filesystem semantics, thread scheduling, C ABI), and before merging or
releasing. A CI failure is captured, reproduced and fixed locally; the rerun
uses the narrowest workflow that can show the fix, never all workflows after a
test-helper or diagnostic change. Independent work (CP09, CP10, CP11) does not
wait for CI. No test is weakened; untested platforms are never reported as
passing; progress records state each checkpoint's level.


## D-013 — Engine selection before execution; replay is temporary (owner, 2026-10-18)
- Architecture rule: the engine for a compilation unit is selected **before** it
  executes. A unit runs entirely on the VM or entirely on legacy (D-001). The
  compiler refuses any unit whose behaviour it cannot reproduce exactly.
- CP11 only: bail-and-replay is allowed for enumerated, side-effect-free bail
  reasons (the VM's only effect is buffered stdout). No filesystem, process,
  environment or other host effect may happen before a replay; output is never
  committed before completion, so large output cannot truncate or change the
  exit status; deep recursion fails safely, never by crashing.
- Unexpected VM failures (internal errors, allocation failure, any unexpected
  exception) are reported as VM failures, never replayed as if legacy had run.
- Removal gate: replay is eliminated (every bail reason replaced by static
  refusal or exact VM behaviour) as an exit criterion of **CP12**, and no
  checkpoint may add an effectful VM operation while replay exists. It is not
  deferred to CP16.

## D-014 — Legacy behaviour the VM meets: three classes (owner, 2026-10-18)
Every legacy behaviour found during migration is classified as one of:
1. **VM defect** — the VM is wrong; fix it.
2. **Historical quirk to correct deliberately** — preserved (or refused, falling
   back) until a recorded compatibility change replaces it, with regression
   tests showing old and intended behaviour. Never changed silently.
3. **Semantics needing a design decision** — escalated to the owner.

Candidates recorded (class 2, not yet changed; the VM falls back today):
- Statements after `break`/`continue` in the same block run, and the jump is
  ignored (CP11 review D2).
- Built-ins win over user functions of the same name, e.g. `is_int` (CP11
  review D4). Already decided as D-009 #3 (user definitions shadow built-ins);
  to be implemented as its own change with compatibility tests.
- Inside loop bodies (and functions called from them) a declaration `b := a`
  takes its binding type from the current value, at top level from `a`'s
  declared type, so `a := 2.0; b := a; b = 2.5` errors only in a loop (CP11
  review N1). More precisely (round 2, M1): the type is fixed from the
  bindings visible when legacy *prepares* the loop or the loop-called
  function. The VM refuses such declarations statically since round 2 (no
  replay).
- Round 2 (old behaviour recorded, refused statically; intended behaviour is
  the owner's decision): a loop-called function whose body legacy's prepared
  parser rejects has its arguments evaluated twice (M2); a string-literal
  argument types its parameter from the unquoted text (M3); assignments and
  parameters are typed from text at top level and from values in prepared
  code (P4/P5 in the CP11 inventory, still replayed when they disagree).
  Full inventory: `checkpoints/cp11-vm-poc.md`, "Legacy's prepared
  evaluator".

## D-015 — Legacy is an oracle, not the language definition; CP11 subset narrowed (owner, 2026-10-18)
- The legacy interpreter identifies behavioural differences; it is not the
  permanent definition of correct semantics. Runtime 2.0 may make intentional
  breaking changes where they give a more coherent, maintainable and performant
  language — always explicit, specified, documented and tested, never silent.
- CP11 is a proof of concept: a conservative, explicitly specified subset runs on
  the VM with exact behaviour; everything else is refused by the compiler and
  falls back before execution. The subset may shrink; no new compatibility
  machinery or replay is added to reproduce accidental legacy behaviour.
  Fallback must not hide compiler defects: a supported suite runs entirely on
  the VM; an unsupported suite proves fallback happens before execution.
- Review cycles: after round-2 fixes and a focused third review, any further
  class of contextual legacy behaviour narrows the subset and is recorded for a
  later language checkpoint; no fourth open-ended compatibility cycle.
- Foundational blockers regardless of subset: complete memory-budget
  enforcement (R5) and enforced lifetime guarantees for references into
  mutable collections (CP09).
- Language-evolution candidates (proposed Runtime 2.0 semantics, to be
  specified with old/new behaviour, compatibility impact and tests before any
  checkpoint implements them):

| Legacy behaviour | Proposed semantics | Found in |
| --- | --- | --- |
| Arguments evaluated twice when a loop-called function fails preparation | Exactly once | CP11 review M2 |
| Parameter/binding type depends on the source text of the argument or initialiser | Type from the evaluated value | CP11 M3, N1, binding-type rules |
| Loop preparation types declarations against bindings visible at preparation | Consistent declarations everywhere | CP11 M1, N1 |
| Statements after `break`/`continue` run | `break`/`continue` always jump | CP11 D2 |
| Built-ins win over user functions; precedence varies by context | Consistent lexical resolution (D-009 #3) | CP11 D4 |
| Aliasing depends on argument shape (`f(o.k)` aliases, `f(a)` copies) | Explicit, documented value/reference rules | CP11 D6 |
| Some runtime errors cannot be caught | Structured, catchable runtime errors | owner |

## D-016 — VM coverage is measured, never a threshold; three separate suites (owner, 2026-10-18)
- VM coverage is a measured outcome, never a threshold maintained by adjusting
  test inputs. Correctness, coverage and fallback are tested by separate
  suites:
  1. **Correctness — supported-subset suite** (`tests/runtime2/vm_subset.py`
     SUPPORTED and the generated `vm_subset_gen.py --suite supported`):
     programs built only from the documented subset, in every documented
     context; 100 % must run entirely on the VM (outcome `vm`; no replay, no
     fallback), with 0 differences from legacy. Programs that deliberately
     trigger a documented replay condition form a separate runtime-replay
     suite (CP11 only; CP12 removes replay).
  2. **Coverage — general generator** (`vm_gen.py` through `vm_parity.py`,
     original distribution, representative Nift including unsupported
     constructs): the VM-execution percentage and the replay and fallback
     shares by reason are reported and recorded; no threshold.
  3. **Fallback — unsupported suite** (`vm_subset.py` UNSUPPORTED and
     `vm_subset_gen.py --suite unsupported`): every deliberately excluded
     construct must fall back before execution with the documented reason,
     with no VM execution (checked through `NIFT_VM_STATS`).
- Consequence for CP11: the round-2 change that steered `vm_gen.py` toward the
  narrowed subset (to keep its 20 % VM threshold) is reverted and the threshold
  removed (CP11 review round 3).

## D-017 — Pipelined checkpoints (owner, 2026-10-18)
When a checkpoint is stable enough for certification, development of the next
starts at once in its own worktree (recording its base commit); certification,
CI and review of the previous one continue independently. Next-checkpoint work
avoids unstable interfaces of the previous one; corrections are rebased in, and
a checkpoint is never certified on an obsolete base. On the 2-vCPU host only
CPU-light work overlaps certification; performance is measured on an idle host.
CP12 (replay removal) started from CP11 `bf32e4f` while CP11 certifies.

## D-018 — One meaning everywhere; first deliberate semantic changes (owner, 2026-10-18)
- Language-wide rule for Runtime 2.0: the same expression and the same function
  have the same meaning wherever they appear — top level, inside a loop, inside
  another function. A function has one compiled representation; no
  per-context variants to reproduce legacy's two evaluators.
- Where legacy's behaviour depends on context, the VM refuses the construct
  before execution (coverage may drop) and the excluded behaviour plus the
  intended consistent semantics are recorded for later checkpoints.
- Approved changes (VM differs from legacy deliberately; each with old/new
  regression tests and a narrow differential policy):
  1. Exactly-once evaluation: a loop assignment whose value reaches 2^53 is not
     re-evaluated; its calls run once.
  2. `print(path)` with a missing element evaluates the path's keys once.
  3. Errors are reported where they occur, not at the call site of a
     loop-called function or at the outermost loop statement.
- CP12 phase A removes its dual-compilation machinery accordingly.

## D-019 — AI assessments: collaborative opinions and controlled evaluations (owner, 2026-10-18)
- Two complementary kinds, never conflated, each disclosing how it was produced:
  (a) **collaborative AI engineering opinions** — agents investigate, are
  challenged with evidence and counterexamples, dogfood, and revise; this stays
  the philosophy of the public AI Opinion page; (b) **controlled comparative
  AI-DX evaluations** — pre-registered tasks, frozen scoring, hidden oracles,
  comparable treatment of competitors; required for empirical claims.
  Challenging with evidence is encouraged; pressuring towards a score is not.
- AI-DX is reported as **observed** (with current model familiarity) and
  **intrinsic** (with adequate docs and interfaces).
- Competitor scores without equivalent probing are preliminary hypotheses with
  lowered confidence; Nift's are not inflated to compensate.
- Assessment A (2026-10-18) silent-failure reproducers are preserved and each
  is investigated (intentional / defect / undocumented rule) before any change;
  template `$[...]` passthrough is not changed without studying legitimate uses.
- Investigate (not yet in scope): structured diagnostics and full stacks in
  CP13, programmatic definitions/references/scopes from the CP12 resolver, and
  CP16 effect recording as a basis for execution inspection; present trade-offs
  before expanding any checkpoint. Assessment B: different model family, bounded
  pilot first. None of this delays Runtime 2.0.
