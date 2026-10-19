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

## D-020 — Loud errors, automatic safe recovery; benchmark campaigns kept distinct (owner, 2026-10-18)
- Contract: errors are loud, recovery is automatic whenever it is safe, and
  success means success.
- An ordinary build failure (syntax, template or expression error, missing
  dependency, failed page) ends cleanly with a non-zero status, keeps valid
  build state and last-good outputs, and the next build works normally once
  the problem is fixed. It never requires `--repair`. `.nift/.unfinished` and
  `--repair` are reserved for genuinely interrupted or uncertain state (crash,
  forced termination), and recovery is automated wherever correctness can be
  established. `build --auto` keeps watching after ordinary errors. Delivered
  by a dedicated bounded checkpoint (feature-build-recovery), tested for partial
  page failures, concurrent builds, cleanup, process termination and
  dependency-state consistency, without weakening crash recovery, dependency
  tracking or atomic output.
- The original website-generator benchmark (nift.dev/website-generator-benchmark,
  v4.0.7, 0.165 s/10k) and the Nift Labs campaign (v4.12, 1.267 s/10k) are
  separate campaigns with their own fixtures and methods; both remain
  legitimate historical evidence, labelled with their conditions. Differences
  are not called regressions without establishing comparability.

## D-021 — Continuous pipeline and proactive delegation (owner, 2026-10-18; standing rule)
Reviews, certification and CI are asynchronous parts of the pipeline, not global
barriers. While checkpoint N is in review or certification, the coordinator
starts N+1 (and independent parts of N+2) wherever interfaces are stable,
delegating substantial, self-contained tasks with clear acceptance criteria.
Each concurrent task has its own worktree, recorded base commit, owned files and
interfaces, dependencies, required tests and integration conditions; two agents
never edit the same files at once (stabilise an interface or sequence instead).
Resource limits on the 2-vCPU host: code writing and light tests may overlap;
heavy builds, sanitizer campaigns and wall-clock measurements do not.
Certification discipline is unchanged: work developed on an earlier base is
re-integrated and re-certified on the final base; only certified work enters
`runtime-2`. Status updates show the pipeline and explain any idle capacity.

## D-022 — CP13 diagnostics decisions (owner, 2026-10-18)
1. Opt-in machine-readable diagnostics: `--format=json` / `NIFT_FORMAT=json`
   with a versioned schema (code, severity, message, primary span, context,
   full call chain, and the exact text line). `eval --json` errors are JSON.
   Normal text output stays byte-identical (the CP13 gate); call chains appear
   in JSON only.
2. Catchable errors are staged: CP13 reproduces today's caught/fatal split
   exactly; widening what is catchable is a later approved language change for
   both engines (never VM-only, D-018). Specific error codes are added now for
   index, type and other language errors instead of `internal.legacy_failure`.
3. Syntax errors are reported before anything runs (no partial output) — an
   approved change with old/new regression tests.
Coordinator decisions within existing rules: try/catch body positions are fixed
in legacy too (feature-site-dx; one meaning, D-018/D-020); legacy message
prefixes other than `return:` stay for parity until the JSON mode carries the
structure; the VM's missing colour on `error:` is a defect to fix.
D-022 refinements (owner, 2026-10-18):
- Syntax prevalidation applies to BOTH engines while legacy fallback exists: a
  program routed to legacy must not bypass it. A compilation unit is fully
  parsed and syntax-validated before any instruction runs; on failure: non-zero
  exit, accurate location and specific code, no program output, no
  program-initiated effects, structured JSON when enabled. Dynamically loaded
  units (imports) are validated when loaded, without preloading unrelated
  files. Syntax validation is not full static validation: valid programs may
  still fail at run time.
- Error model: identity (code), severity and catchability are separate
  properties; adding a code never changes catchability. Codes reuse the
  existing taxonomy where apt (e.g. index out of bounds, type error, undefined
  callable, arity, integer overflow, division by zero), are emitted by both
  engines, appear in JSON, and text stays under the CP13 gate. Tests cover
  codes and the unchanged catchable/fatal split.
- Intended long-term semantics: ordinary runtime errors become catchable via a
  separate approved language checkpoint applied to both engines; internal VM
  failures and resource exhaustion stay distinct (not ordinary exceptions).

## D-023 — CP14 built-in inconsistencies: record now, change later (owner, 2026-10-18)
CP14 natives reproduce legacy exactly (zero-difference gate). The intended
semantics are recorded for one later language-change checkpoint applied to both
engines (D-018), with old/new tests: consistent `trim` (same whitespace set as
`trim_start`/`trim_end`, Q1); character-based string indices for `index_of`/
`substr` consistent with `length` (Q2); exact integer `sum` and `range` beyond
2^53 (Q6, Q7); both `index_of` spellings on arrays and strings (Q8); literal keys
for `pick`/`omit` with explicit globbing (Q9). Context- and shape-dependent
quirks (Q3, Q4, Q5, Q10, Q11) are never reproduced by the VM: they are refused
before execution.

## D-024 — Language consistency register and cleanup checkpoint (owner, 2026-10-18)
Historical inconsistencies are tracked in `language-consistency.md` and resolved
together in a dedicated language-semantics checkpoint once the VM migration has
stabilised native APIs and values, correctness issues (integer precision,
Unicode indexing) ahead of cosmetic ones. Preserving legacy behaviour in the
meantime keeps checkpoint scope bounded; it is not an endorsement. For Unicode
the preferred direction is code-point indexing with explicit byte operations,
finalised after investigating performance, edge cases and migration. Natives
operate on the compact value representation, never through legacy
`RuntimeValue` conversions on hot paths, under per-built-in instruction guards.
Site-DX warnings stay conservative (no speculative warnings where Nift cannot
tell its syntax from legitimate content); strict mode rejects only genuinely
unresolved Nift expressions; `--auto`'s fallback to `--repair` is temporary and
must be removed or justified when build recovery integrates.

## D-025 — Multi-line return values; `runtime.*` error codes (owner, 2026-10-18)
- `return` followed by a value that continues across lines (e.g. `return {`
  with a multi-line object or array literal, an open bracket or parenthesis)
  continues to the matching close on both engines — an approved change in
  CP13, so strict prevalidation accepts nift-packages/http (`src/http.f:101`)
  and that branch works at run time. Old/new tests; bare `return` at end of
  line keeps its meaning.
- Language error codes use the semantic `runtime.*` namespace (e.g.
  `runtime.type_error`, `runtime.index_out_of_bounds`), independent of where
  the error arises (built-in, bytecode or legacy); resource and host failures
  get their own codes (e.g. limit and host-service failures).

## D-026 — Performance ambition; site-DX warning and strict contracts (owner, 2026-10-18)
- **Performance ambition.** CPython parity is the first milestone; competitiveness
  with Bun on representative scripting and automation workloads is the stretch
  goal (match or beat it on short-lived scripts, filesystem orchestration and
  native-dominated work). Cold start, warm/sustained throughput, peak memory and
  end-to-end time are measured separately; references are v4.12, CPython, Bun,
  Node, Lua and Go (compiled reference). Semantics and methodology are never
  bent for a result. Where Bun wins, the scoreboard records the likely cause
  (JIT, native library, runtime overhead, architecture). Whether Nift needs
  adaptive optimisation or a JIT is decided from that evidence, not before.
- **Disabled features cost nothing.** A disabled warning does no filesystem
  work; overhead is measured in instructions against the pre-feature baseline.
- **Strict is a verification command.** `build --strict` exit 0 means no unknown
  names anywhere in the site, including up-to-date pages; toggling strict
  without source edits keeps `build`, `status` and exit codes in agreement.
- **Output paths.** `public` and `public/` are one directory; absolute, external
  and symlinked output dirs are supported; no project-root containment.
- **Warnings stay conservative, by context not case.** No blanket case-based
  exclusion (`@Content()` in template text still warns); suppression only in
  recognised non-Nift contexts (script/style/pre/code, comments, fenced code,
  JSON keys/JSON-LD). `$[…]$` maths passthrough has a precise documented rule
  and never changes output of a resolving name.
- **Structured process results.** Conventional pipeline exit status is kept;
  results also expose per-stage status, distinguishing launch failure from a
  launched process exiting nonzero.
- The temporary `--auto` → `--repair` retry is removed once build recovery is
  integrated, verified by an end-to-end watcher test (error → fix → rebuild,
  no repair).
- **Benchmark readiness (refinement).** Load average is an indicator, not the
  criterion. Wall-clock runs follow a preflight (CPU contention and competing
  heavy jobs, memory pressure/swap/PSI, free disk, CPU frequency/thermal where
  readable, calibration-kernel stability) and run in an exclusive window;
  preflight results are kept with the raw data. Benchmark infrastructure stays
  bounded to the current milestone; the priority result is records20k at 100%
  VM once CP14/CP15 are integrated.
- **`nift serve`** (feature-serve): a thin wrapper over Python 3's
  `http.server` (argv, never a shell string) serving the configured output dir
  or `--dir` (no containment), defaults 127.0.0.1:8000; no live reload, watch,
  proxy, HTTPS or C++ server. Local development only; Python 3 is required and
  never installed automatically.

## D-027 — Integration first; JSON fast path; honest platform guarantees (owner, 2026-10-18)
- **Integration order after CP12 certifies:** CP14 natives wired into the VM,
  then CP13, then CP15; the first complete `records20k` run at 100% VM is the
  next major milestone. CP12 must not become an endless effort to perfect a
  restricted subset: after the current fix verification, findings outside the
  accepted subset are handled by refusal/narrowing or recorded for later, and
  only defects in what the VM accepts block integration.
- **Compile-analysis guards:** the CP12 compile-cost improvements (kind
  inference on a 500-function program, compilation of a 500-function chain)
  become callgrind regression guards.
- **JSON:** after CP15 integrates, parse JSON directly into VM values (no
  intermediate document), measured for throughput, allocations and memory
  against CPython, Bun and Node on equivalent workloads. Not a blocker for the
  initial CP15 integration.
- **No compatibility machinery** for contextual or source-shape-dependent legacy
  behaviour: the VM refuses such constructs and the register records them.
- **Platform guarantees are claimed only where established.** `nift serve` on
  Windows guarantees no orphaned server only when the kill-on-close job object
  is in place; otherwise it warns. The cross-platform gate tests process
  termination and child cleanup, not only compilation.
- **Strict follows substitution (refinement).** If Nift evaluates a `$[...]`,
  strict validates it wherever it appears; context only quiets speculative
  warnings; heuristic (similarity) warnings are never fatal; missing or invalid
  unknown-value metadata counts as unverified. `\$[` and the exact `$[name]$`
  maths exception are the literal mechanisms; a raw-content mechanism is a
  possible later design, not part of site-DX.
- **Watcher contract.** `--auto` never swallows an edit made during a build or
  retry (including rapid and atomic saves) and never loops on a build's own
  writes; tested deterministically with slow hooks.
- **Critical path on the heavy lock.** CP12 jobs take the lock with priority
  (`heavy.sh --priority`); other jobs wait while a priority job is queued.
- **Dogfooding re-assessment.** After build recovery and site-DX are certified,
  repeat the website-generation dogfooding assessment to check whether the
  original problems are actually gone (not to chase a score).

## D-028 — Host operations: fix legacy gaps first; bounded reads; configurable VM budget (owner, 2026-10-19)
- **Legacy gaps are fixed before CP16, not reproduced by the VM.** A small
  checkpoint (`feature-host-deps`) makes `open_bytes()` and stream reads record
  their dependencies (no page may be reported up to date with stale output,
  D-020) and removes the remaining containment in `file().copy()/.move()`
  (filesystem freedom, D-002). Both engines then share one meaning.
- **Unbounded reads fail loudly.** Reading a device, FIFO or other unbounded
  source past a fixed bound is a fatal `limit.exceeded` on both engines, never an
  out-of-memory kill.
- **VM memory budget is configurable.** The VM heap budget (currently 1 GiB)
  becomes configurable, with the default and the setting's name decided in the
  CP16 implementation; exceeding it remains a loud `limit.exceeded` that names the
  setting. Large data should still prefer streaming (D-011 modes).
- Still open for the owner from the CP16 design: O5 (language surface for the
  D-011 modes); O6/O7 already scheduled (catchable host errors, `save()` and
  symlinks). O2 (a version-2 Host-op trace covering every operation, with
  outcomes and error codes) is a coordinator decision: adopted.
- **Refinements (owner, same day).** Every filesystem read that influences a
  build records its dependency whichever API performs it (file-backed streams at
  successful open; failed or missing opens consistent with other read APIs);
  copy/move follow normal OS path semantics while `.nift/`, managed outputs and
  recovery stay protected. The whole-read bound is configurable with a
  documented conservative default and never truncates silently; streaming reads
  stay unlimited with bounded buffers; bounds are enforced while reading, not
  from `st_size`. Never-EOF FIFOs: cancellation/timeouts are supported by the
  CP16 Host-op architecture, not redesigned now. A bounded audit of all
  filesystem APIs (dependencies, containment, unbounded reads) accompanies the
  fix.

## D-029 — One process model; `run()` extended, syntax decided later (owner, 2026-10-19)
- CP16's Host layer implements one process model (capture, streaming,
  interactive input, detached execution, deadlines, cancellation, exit
  information, cleanup) without waiting for the public syntax.
- The public surface should extend `run()` with explicit options/modes rather
  than add unrelated process commands; the final syntax is chosen by the owner
  from a short comparison of concrete alternatives that follow existing Nift
  conventions, with examples for each mode.
- Process lifetime and ownership come first: ownership after `run()` returns,
  behaviour at script/build exit and on signals, pipeline cancellation,
  abandoned handles, deadlock-free draining, deadlines while blocked, and
  POSIX/Windows differences, each with explicit cleanup semantics and tests.
- Process handles use CP17's typed-handle design. Launch failure stays
  distinguishable from a launched process that exits nonzero.
- Whole reads stop retaining data at the configured bound (a small probe past
  it is allowed; unbounded allocation is not).
- **Refinements (owner).** Ownership classes: managed processes belong to their
  execution context (script, page render, build, `--auto` rebuild) and are
  cleaned up when it ends; explicitly detached processes transfer
  responsibility; processes launched during builds are managed, and a
  superseded `--auto` build leaves none behind. Cancellation distinguishes a
  direct child, a whole pipeline and a descendant tree, and cooperative
  cancellation from forced termination with bounded escalation. Documented
  guarantees are per platform and only what can be enforced (POSIX process
  groups can be escaped; Windows job objects have breakaway and inherited-handle
  caveats); no general-purpose supervisor. CP17 owns the handle
  representation. Public-API alternatives use parser-verified Nift code.

## D-030 — `run()` public syntax and process-lifetime changes (owner, 2026-10-19)
- **Syntax: alternative A.** `run()` takes a trailing options object (e.g.
  `{"mode": "stream", "timeout_ms": 5000}`) covering capture, streaming,
  interactive input, detached execution, deadlines and cancellation. Passing an
  object to `run()` is fatal today, so no working program changes meaning.
  Unknown option keys are fatal for `run()`.
- **Approved behaviour changes (old/new tests and migration notes each):**
  C1 background descendants of `run()` are terminated when their execution
  context ends; C2 SIGTERM to Nift terminates its managed children (the pinned
  `process_capture.sh` check flips); C3 each managed pipeline runs in its own
  process group.
- **Defaults:** 2,000 ms grace before forced termination; detaching inside a
  build is a loud error; cleaning up a still-running managed process at context
  end prints a warning without changing the exit status.
- Design record: CP16 `cp16-host-ops-design.md` §11–§12 (`runtime-2-cp16-design`
  @d4310bc); parser-checked examples in `tests/runtime2/cp16/syntax/`.
- **Refinements (owner).**
  - *Syntax:* an explicit `mode` where execution behaviour differs, named options
    for deadlines and cancellation; incompatible combinations are fatal; unknown
    keys are fatal with a spelling suggestion; defaults preserve every existing
    valid `run()` call; launch failure, nonzero exit, timeout and cancellation
    stay distinguishable; ownership and cleanup documented per mode; the 21
    parser-checked examples become acceptance fixtures.
  - *C1–C3:* explicitly detached processes are never touched by managed cleanup,
    but a managed process cannot escape ownership by backgrounding itself
    (platform limits documented). SIGTERM to Nift runs bounded cleanup
    (graceful → bounded wait → forced) and then exits with a signal-derived
    status. Process groups are per managed pipeline; cancelling one never affects
    other pipelines or Nift itself; exit/cancel races leave no zombies. The
    pinned SIGTERM expectation is updated explicitly under this decision.
    Release notes call out C2 prominently (use explicit detachment instead of
    relying on accidental survival).
  - *Defaults:* detachment is forbidden in builds including hooks and `--auto`
    rebuilds. The cleanup warning fires only when Nift actually terminates a
    still-running process (not when reaping an exited one), names the process and
    the reason, is not duplicated across a pipeline, and may become suppressible
    without disabling cleanup. 2,000 ms is a documented default that may become
    configurable later; no new configuration machinery in CP16.
- **Ownership by call shape (owner).** A synchronous `run()` collects all output
  and reaps every stage before returning. A streaming/started handle leaves
  responsibility with the enclosing execution context (not the caller forever);
  explicit detachment moves it outside that context. Implementation and tests
  keep these three events distinct. D-030 is complete; no further CP16 design
  expansion.
- **Read limit vs VM heap (owner).** `NIFT_READ_LIMIT` (per-operation bound on
  materialising one whole read or capture) and the VM heap budget (total
  VM-managed memory) are independent controls; both default to 1 GiB for now,
  neither tracks the other, and the docs state the difference. The remaining
  unbounded reads (command-style captures; the build engine's template/content
  loading) stay explicit follow-ups.
- **Generator honesty (owner).** VM coverage reports include the supported and
  general generators' acceptance and refusal rates by reason; any narrowing of
  the supported-subset spec is explicit, kept as must-stay-refused fixtures, and
  reported.

## D-031 — Build-output ownership; dangerous-I/O test containment (owner, 2026-10-19)
- **Ownership, not directories.** During a website build, script writes are
  refused into `.nift/` metadata and onto outputs owned by another page or
  declared build task; writes to the current page's own outputs, to unowned
  destinations (including unowned files in `public/`) and outside the project
  (OS permissions, optional `--fs-root`) are allowed. Destinations are resolved
  with filesystem semantics (symlinks, `..`, alternative spellings) close to the
  write; the remaining check/write race is documented. Overwritten pages are not
  auto-rebuilt (no compatibility machinery). Old/new tests prove the previous
  cross-page corruption and its refusal.
- **Cross-filesystem moves** fall back to copy-then-delete with explicit partial
  failure: a failed copy leaves the original intact; a failed delete after copy
  is reported as an error, never success.
- **Later (not now):** explicitly declared additional outputs per page/task, so
  ownership covers JSON exports, images and similar generated files.
- **Standing rule: dangerous I/O tests are contained.** Any test or probe that
  could produce unbounded data, memory, disk use or processes runs under hard
  limits (`ulimit -f`, `ulimit -v`, `timeout`, child cleanup), even when the
  implementation is supposed to enforce them, so one probe cannot disrupt other
  agents or invalidate certification work.

## D-032 — Benchmark tiers and string-performance priorities (owner, 2026-10-19)
- **Two benchmark tiers.** Quick preliminary benchmarks are wall-clock, minutes
  long, on a dedicated temporary Linode, with each measurement auto-calibrated to
  at least 80 ms of work, cold start separate from steady state, outputs and work
  witnesses verified, and `NIFT_VM=require`. Detailed campaigns (callgrind,
  scaling, allocations) are reserved for confirmed bottlenecks. Raw evidence keeps
  machine, flags, runtime versions and commit identifiers.
- **Claims.** Speculative results stay labelled speculative and enter the official
  scoreboard only when the checkpoint is certified and integrated; no broad
  claims of superiority over Python or Bun from preliminary workloads.
- **String priorities (after CP14 integrates; CP14 is not delayed for them):**
  cached code-point count for constant-time repeated `length()` (lazy, correct
  under copy-on-write mutation and concurrency, coordinated with the Unicode
  cleanup in the consistency register); `to_upper`/`to_lower` profiled with ASCII
  and non-ASCII separated (a fast ASCII path is fine; Unicode semantics are not
  traded for speed); allocations per field in `split()`; allocations per object
  literal. Each optimisation gets its own correctness and performance gates.

## D-033 — Remote Linux certification; handle identity vs ownership (owner, 2026-10-19)
- **Remote certification.** After the CP14 local-versus-remote comparison shows
  equivalent or stronger coverage (every target's classification compared;
  environment differences recorded), a dedicated temporary Linode becomes the
  normal Linux certification platform. Full duplicate local certification is not
  required per checkpoint unless there is a specific reason; local focused tests
  remain for development, with periodic cross-checks of environment-sensitive
  suites (timestamps, scheduling, permissions). Remote Linux certification never
  replaces macOS/Windows CI.
- **Handles (CP17).** Resource identity is separate from ownership: references
  may share a resource, but there is exactly one owner and one cleanup policy;
  no double or premature cleanup; transfer is explicit. CP16's temporary handle
  adapter is replaced completely by CP17's interface (no competing ownership
  mechanism).
- **Pacing.** CP19 stays on hold until the current integration queue clears; no
  new architectural workstreams for now.
