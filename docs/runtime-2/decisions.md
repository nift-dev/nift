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

## D-034 — `run()` option details (owner, 2026-10-19)
- `mode: "pipe"` is an explicit opt-in for pipe capture (which also terminates
  leftover background processes holding the pipe); `mode: "capture"` keeps
  today's file capture.
- `stdin`: `"inherit"` (default, today's behaviour), `"null"`, `{"file": path}`
  (a recorded, bounded host read), `{"text": string}`; anything else is fatal
  with a suggestion.
- Deadlines on started handles are enforced even while the handle is idle (from
  the reaper), not only when it is next used.
- After SIGTERM, non-blocked script code may run for at most the documented
  grace + bound window while managed children are cleaned up; a context that ends
  on an error terminates its processes without the cleanup warning.
- **Refinements (owner).** Pipe capture drains stdout/stderr concurrently,
  bounds while reading, cleans up descendants holding the pipe, and keeps launch
  failure, nonzero exit, timeout, cancellation and `limit.exceeded` distinct;
  modes documented (capture / pipe / stream / detached). `{"text": …}` is fed while
  draining output, survives early exit/broken pipe without crash or hang, and is
  strings only (binary stdin later via a byte form or stream handle);
  `{"file": …}` follows OS semantics, `--fs-root` and build dependency
  recording; no shell; compatible with CP19/CP20 redirection. Deadlines use a
  monotonic clock, cancel the whole pipeline, are race-safe, record expiry even
  if the process exits during grace, and are the mechanism CP22 reuses; a
  deadline marks when cancellation begins. SIGTERM immediately marks the context
  as shutting down: no new launches, builds, renders or other externally visible
  operations; exit status is signal-derived; the window is a normal-shutdown
  guarantee, not protection from SIGKILL or crashes. Error-ended contexts
  suppress routine cleanup warnings but still report genuine cleanup failures.

## D-035 — Typed-handle policy (owner, 2026-10-19; CP17)
- **Abandoned handles** are detected by reference counting: counted handle kinds
  are wrapped in a reference-counted VM value, and dropping the last reference
  cleans the resource up promptly (with the cleanup warning for a live process).
  Requires VM support when CP17 is wired into the VM.
- **Ownership transfer** is host-internal only (e.g. async workers handing results
  to a parent); scripts can only `detach`, which is refused in builds.
- **Error codes:** using a closed handle is a catchable `runtime.handle_closed`;
  forged, cross-table and wrong-kind handles stay fatal `internal.handle_*` (the
  compiler prevents scripts from forging handles).
- **Cleanup failure** (e.g. a process that could not be killed, `Lost`) makes the
  run exit nonzero (D-020); any original error stays the primary diagnostic.
- Coordinator calls (within existing decisions): the handle capacity becomes a
  named setting reported by `limit.exceeded` (D-028 convention); nested contexts
  may use their ancestors' handles, parents and siblings may not use a child's.
- **Refinements (owner).** Last-reference release only *schedules* cleanup via
  CP16's process machinery (no blocking in VM execution); a thread-safe lifecycle
  state machine guarantees exactly-once cleanup across release, deadline,
  cancellation and context end; the reaper keeps internal ownership until the OS
  resource is reclaimed; detached processes survive their last reference. Host
  transfer is atomic, refused (and the resource reclaimed) when the destination
  is ending, and is how async workers return handles. Closed handles keep
  identity (`runtime.handle_closed`, never misread as forged); timeout and
  cancellation outcomes are preserved, not turned into `handle_closed`; `close()`
  is idempotent, other operations on a closed handle raise. Cleanup failure is
  declared only after the bounded procedure is exhausted or the OS reports a
  definitive failure; `Lost` means "termination could not be confirmed"; a
  context never reports final success with cleanup obligations unresolved;
  original errors and signal-derived statuses stay primary.

## D-036 — Roadmap expansion for unowned VM features (owner, 2026-10-20; CP12B inventory)

The CP12B coverage inventory found about 180 curated scripts blocked by features
no checkpoint owned. Decision: expand the roadmap; **CP25's zero-fallback gate
stays strict** (no accepted permanent legacy fallback).

- **CP12 phase C:** functions as values, closures/lambdas, `for`-loop compilation.
- **CP14 phase B:** callback-taking collection methods (with the F4/F5 work).
- **CP15 phase B:** remaining pure built-ins and methods.
- Process, concurrency and FFI functions go to their natural checkpoints
  (`cmd` → CP16/CP20, `mutex` → CP22/CP23, `ffi_*` → CP18), never to a
  miscellaneous checkpoint. Every refusal category gets a named owner and
  dependency (table in `checkpoints/cp12b-coverage-inventory.md`).
- Chained calls on call results (153 curated scripts) are a targeted coverage
  task inside CP12B.
- 100 % of the current corpus is a necessary gate, not proof of completeness:
  supported-generator expansion, real package testing, differential checks and
  cross-platform certification stay. The acceptance corpus is never narrowed.
  Corpus coverage and real-package compatibility are tracked separately.

## D-037 — Modules on the VM (owner, 2026-10-20; CP12B)

Approved, subject to verifying legacy semantics first and documenting any
intentional difference before implementation:

- Literal import paths resolve at compile time, relative to the importing module
  under Nift's existing path rules; module identity is canonicalised.
- Each unique module is compiled once per program. **Compilation caching is
  separate from initialisation**: legacy's observable initialisation and side
  effects (including diamond imports) are preserved unless a deliberate
  compatibility decision changes them.
- Circular, repeated, nested and missing imports are probed and tested with an
  explicit contract; imports (including missing files where legacy records
  them) are build dependencies.
- Unsupported dynamic imports are rejected before execution; no speculative
  dynamic-import support.
- Module loading uses CP16's host-read interface once available.

## D-038 — Lexical scoping (owner, 2026-10-20; L25)

Lexical scoping is the target semantics. The VM does not reproduce caller-local
dynamic lookup. The two curated scripts that depend on it become explicit
compatibility fixtures demonstrating the difference. The resolver distinguishes
lexical globals, module bindings and genuinely caller-dependent lookup; only the
last is refused before execution. Final migration diagnostics belong to the
language-cleanup checkpoint.

## D-039 — CP17 close and detach semantics (owner, 2026-10-20)

- Explicit `close()` closes the underlying resource for every reference to the
  handle; later operations through any alias raise `runtime.handle_closed`;
  repeated `close()` stays idempotent (tested).
- Dropping a reference releases it; dropping the last reference schedules
  cleanup; explicit `close()` closes regardless of remaining references.
- Failures occurring **after** successful detachment do not change the exit
  status. Failure to **establish** detachment (ownership not transferred or
  promised lifetime not established) is a reportable operation failure.
- A detached outcome appears in the shutdown report only if Nift genuinely knows
  it; Nift does not retain supervision solely to report it.
- The lifecycle cost (create+resolve+release ~1,250 → ~2,100 instructions) is
  accepted while the resolve hot path is unchanged; the guard stays, and
  optimisation waits for measurements that justify it.
- **Amendment (owner, 2026-11-10): `file()` values are exempt.** `close()` on a
  `file()` value closes its open buffer, not the value: `path()`, `exists()` and
  `open()` (reopen) stay available, and every other method reports legacy's
  ordinary error (`<method>: file is not open`, fatal, legacy's position). A
  second `close()` is legacy's `close: file is not open`; the free `close(f)`
  of a `file()` value is legacy's `close: expected stream`. Reopening resets
  the buffer exactly as legacy's `open` does. D-039 is unchanged for every other
  handle kind (streams, processes, timers, channels, ...).
  Record: `checkpoints/combined-b3-b5.md` section 54.

## D-040 — L7: value semantics for ordinary values (owner, 2026-10-20)

Evidence: `checkpoints/l7-aliasing-proposal.md` (branch `l7-design`, eff20687):
113 legacy probes; legacy aliases only for certain syntactic shapes and contexts,
which cannot satisfy D-018. All four recommendations approved:

1. **Ordinary values** (arrays, objects and other compound values) have value
   semantics, implemented with copy-on-write, in **both engines** (D-018).
   Assignment, parameter passing, returns, container extraction and closure
   capture have defined value behaviour independent of source-expression syntax.
   Copy-on-write is an implementation detail and must never be observable.
   Value semantics apply recursively: nested values never share mutation.
2. **Structs** are values too. The affected fixture and regression assertions
   are updated explicitly under L7, never silently.
3. **Maps and sets** are values. Copying a container holding CP17 handles keeps
   handle identity and never duplicates the underlying resource; mutating either
   container stays independent.
4. **`same()`** is restricted to identity-bearing types (handles and other
   shared-identity resources); on ordinary values it is a clear argument/type
   error. `==` keeps value equality. Storage sharing is never exposed.

Migration: an optional legacy aliasing audit mode is implemented. VM warnings
about modifying extracted values stay opt-in unless shown to have a low
false-positive rate. Measured impacts (including the `diff` package contract
assertion and the v46 import-worker alias test) are explicit approved
differences.

Correctness: nested copying, parameters, returns, closures, loops, async
boundaries and handle-containing containers are defined and tested. Whether
cyclic ordinary values can exist is determined: either an acyclic invariant is
established and enforced, or cycle semantics are defined explicitly — no
garbage collector to preserve a legacy quirk. Every intentional change has an
independently checked differential fixture.

Performance: keep the legacy quadratic `x = f(x)` reproducer; add guards for
large nested values, mutation after copy, and repeated assignment.

Process: isolated from CP13/CP15 compiler integration; normal independent
review and certification; CP25's zero-fallback requirement unchanged.

## D-041 — Module initialisation and contexts (owner, 2026-10-20; refines D-037)

Evidence: `checkpoints/cp12b-modules.md` (branch `runtime-2-cp12b-dev`,
eb073516): static import-graph expansion over the corpora, 86 project units and
38 project checkouts (~2,100 files with imports) found no executed program that
initialises a module twice; double imports occur only in documentation. (Static
analysis, not every conditional import path executed.)

1. **Initialise once per execution context** (option A). Importers share the
   module instance; compilation, initialisation and state ownership are separate.
   The instance has an explicit lifecycle (uninitialised, initialising,
   initialised, failed) leaving room for CP22–CP23: concurrent imports observe
   the completed instance or the same failure, never a second initialisation;
   circular imports follow the documented contract.
2. **Both engines together** (D-018): independent fixtures first, then
   initialise-once in legacy and VM, differential and migration suites, one
   integration. A temporary VM refusal (`import-reinitialised`) is allowed only
   if the engines must land separately, with a recorded removal condition; it is
   never part of the module design.
3. **Context boundaries:** script run, page render, build hook, each
   auto-rebuild pass and each isolate get their own instances; a page render
   never inherits mutable module state from another page or the build. The
   compiled-module cache is immutable and shareable, keyed by canonical path,
   content hash, compiler version **and every compilation setting that affects
   meaning** (language modes, target configuration, feature flags). Imports
   during a page render contribute to that page's dependency record.
4. **Exports follow D-040:** ordinary exported values are copied (copy-on-write)
   and are not live mutable bindings; exported functions keep access to their
   module's lexical private state; CP17 handles keep shared identity. The
   specification states this explicitly.

Verification: independently specified tests for repeated, diamond, circular,
nested and stateful imports, page-render isolation and rebuilds, in both
engines; corpus and real-package gates preserved. No further module-design round
unless implementation evidence raises a genuinely open question.

**Amendment to #2 (owner 2026-11-11 05:41, D-068 #10).** Initialise-once per canonical module identity is the
intended semantics. Legacy does not change during this campaign, so "both engines together" no longer applies to it:
the VM's eventual initialise-once is an intentional difference from legacy, and the removal condition of the
temporary `import-reinitialised` refusal is now VM-only (the VM implements initialise-once). A bounded VM correction
is still to be investigated; the row stays PENDING-OWNER.

## D-042 — Shell file commands and exactly-once command dispatch (owner, 2026-10-20; CP19)

Evidence: `checkpoints/shell-fs-commands-review.md` (branch `shell-fs-review`,
ed355b82). The shell turns `cp a b` into `cp("a","b")` and, if that call fails
for any reason, re-runs the line as the external command: `rm -i a` deletes
without prompting, `rm nope` reports success, `rm a d1` executes twice,
`rm [ab].txt` silently does nothing; the same fallback applies to `mkdir`,
`touch` and user functions. Native `cat` reads whole files into memory.

1. **Shell invocations of `ls`, `cp`, `mv`, `rm`, `cat` resolve to external
   executables through `PATH`.** No `lst`/`cpy`/`mve`/`rmv`/`catf` names.
2. **Exactly-once dispatch (central CP19 acceptance requirement):** a command is
   classified before execution, never after observing whether an attempt
   succeeded. A failed native call is never retried as an external command; the
   failure status is preserved and no hidden duplicate side effects occur. This
   applies to every command, including `mkdir`, `touch` and user functions.
3. **Deterministic precedence**, documented and tested: explicit Nift
   function-call syntax, shell functions/aliases, `PATH` executables, explicit
   executable paths, an explicit external-command escape (`command`) where
   supported. Shell-state built-ins such as `cd` stay.
4. **Compatibility:** website shell documentation updated with migration notes;
   common GNU/uutils commands and flags tested; `.f` scripts keep their
   native-function interfaces; on Windows a useful command-not-found diagnostic,
   never a silent native substitute.
5. **Tests:** permanent regressions for all five (`rm -i` never silently
   deletes; missing files fail as the external command does; partial failures
   never execute twice; globbing applied exactly once; pipelines and
   stdin/stdout/stderr consistent), plus a general exactly-once side-effect test
   covering failed native calls and user functions.
6. **Scheduling:** approved CP19 work; does not interrupt CP15, host-deps or CP12B.
   The preliminary benchmark stands as evidence.

**Pending (owner refinement, same day):** whether the native scripting functions
move into an explicit `native.*` namespace (`native.cp(...)`, multi-source forms
preserved) with the old global names deprecated under language cleanup, instead
of staying as `cp()` etc. A bounded feasibility and migration investigation is
running; no public API change until decided.

## D-043 — Linode capacity by budget, not instance count (owner, 2026-10-20; supersedes the 2-instance cap of D-033)

The fixed two-instance cap is removed. Temporary Linodes are provisioned by
workload demand (initially 3–4 concurrent, more when productive), under the
**US$10 aggregate campaign budget**, which is enforced on **committed spend** =
accrued cost + maximum remaining cost of every live campaign instance to its TTL
(`linode.py` refuses a create that would exceed it; shorten TTLs, use a cheaper
type, or ask the owner). Every instance (Claude's or DeepSeek's) carries the
`nift-rt2-campaign` tag, an owner/purpose tag and a TTL tag, and is visible in
one inventory; other owners' deleted-instance spend is recorded in
`external-spend.txt`. TTLs, evidence retrieval, verified deletion, credential
protection and pinned SSH host keys stay mandatory. No idle capacity; workers
are reused for consecutive jobs and deleted when their queue is empty.
Independent workloads (CP12B, CP17, L7, memory investigation) get their own
workers; performance baselines run on uncontended workers. DeepSeek may
provision within these limits without per-operation approval.

Also decided (owner, same day):
- **D-041 interpretations approved:** a repeated import rebinds the same
  exports; a failed module initialisation stays failed within its context, and
  later imports report the original failure without re-running side effects.
- **L7 cycles (D-040 addendum):** ordinary values are acyclic — a value never
  contains itself directly or indirectly (shared copy-on-write storage between
  distinct values is not a cycle; identity-bearing handles/functions may refer to
  internally cyclic resources). Enforced at mutation boundaries with
  `runtime.reference_cycle` and tests for direct and indirect cycles, without a
  full graph traversal on ordinary assignments; the implementation's cost is
  measured and reported.

## D-044 — DeepSeek-first implementation; filesystem functions (owner, 2026-10-20)

1. **Division of labour.** No new Claude implementation subagents by default;
   existing agents (CP12B, CP16, CP17, L7, host-deps, CP15 integration) finish
   their current checkpoints. New implementation work goes to DeepSeek with a
   handover giving the exact base SHA, decisions, owned files, acceptance tests
   and prohibited areas; DeepSeek works autonomously (own worktrees and
   Linodes under D-043, no pushing). Claude owns architecture, acceptance
   contracts, file ownership, review of finished deliverables, adversarial
   review where needed, Linode certification and integration. Certification
   gates are unchanged. Opus review/repair cost per DeepSeek assignment is
   tracked; delegation is adjusted if the cost exceeds the benefit.
   First assignment (ASCII case conversion, patches 1b61324/5e14453) delivered;
   next candidate: a bounded CP15B slice once CP15A integrates.
2. **Filesystem scripting functions keep their global names** (`cp()`, `rm()`,
   `mkdir()`, `touch()`, …); D-042's call-syntax/shell-word distinction is
   sufficient once dispatch is deterministic. No `native.*` or abbreviations.
   **`native::` approved (owner, 2026-10-20), scheduled with L6 after
   CP25-critical work:** `native::<name>(...)` explicitly calls a public Nift
   built-in, bypassing user-function shadowing; unqualified calls follow lexical
   resolution (L6); unknown qualified names are a clear diagnostic; `native` is a
   reserved built-in namespace only (no general namespaces); covers all public
   built-ins where L6 shadowing applies, never internal C++ functions; both
   engines; the existing global names stay and the ~735 call sites are not
   migrated. (`::` is unused today: legacy rejects it as malformed; the VM lexer
   has no `::` token.)
3. **Filesystem defects to fix** (language cleanup / filesystem), with expected
   semantics specified first and regressions that fail on the old code:
   `rm()` on a directory (semantics to be specified, not inherited from GNU),
   `cp()` with an unmatched glob (clear error per a documented glob contract),
   `rm()` of a missing file (report failure), `touch()` on an existing file
   (update mtime), user functions named like built-ins (L6). Consequential
   compatibility choices go to the owner. Fast native implementations and
   multi-source forms are preserved.
4. **Process ownership:** agents terminate only processes they own, by verified
   PID/process group, never by command-name matching.

## D-045 — Syntax compatibility is a strict requirement (owner, 2026-10-20)

Runtime 2.0 preserves Nift's established syntax as implemented by legacy Nift
and documented at https://nift.dev. It is a faster, more consistent
implementation of Nift, not a new language that resembles it.

- No Python-, JavaScript-, C- or other-language-inspired syntax because it is
  easier to compile. Before implementing or extending any syntax, check (1) the
  documentation at nift.dev, (2) current legacy behaviour, (3) the Runtime 2.0
  parser/compiler behaviour.
- Functions: templates `@fn(name(args)) { … }`; standalone `.f` scripts
  `fn(name(args)) { … }`; bodies use Nift statement syntax with single braces;
  semicolons are optional (nift.dev/docs/functions-fragments.html).
- Applies to CP12B, CP12C, L7, closures, modules, structs, loops, native calls
  (`native::`) and every later checkpoint, and is referenced in every DeepSeek
  handover.
- Verification: executable syntax fixtures derived from the published
  documentation (documentation examples become regression fixtures wherever
  practical), run against legacy and the VM; unsupported syntax is refused before
  execution, never silently reinterpreted; intentional owner-approved changes are
  distinguished from compatibility regressions; where legacy and the
  documentation disagree the discrepancy is recorded and decided before any
  change. Syntax compatibility is part of the relevant certification gates.
- Active implementations are checked for syntax discrepancies during their
  normal review; no separate large audit and no interruption of running jobs.

## D-046 — D-045 reconciliation decisions (owner, 2026-10-20)

From CP12B's three-way check (nift.dev, legacy, approved semantics;
`cp12b-modules.md`/`cp12b-structs.md` @390847ea) and stage 4's oracle:

1. **Declaration typing (stage 4).** Established textual declaration typing is
   the rule on every path: a double-declared binding never becomes an int because
   its value is integral; copying a parameter into a declaration preserves its
   declared type. Legacy's loop-path value typing is recorded as an inconsistency
   (register), not a feature. The VM keeps refusing where the type cannot be
   established. Oracle, cross-type tests and the nine planted defects stay.
2. **M1 — exports: D-041 #4 is reversed.** `export(name)` exports the binding
   (shared access to the module's storage location), as nift.dev documents and
   legacy does; the copy rule rested on an incorrect description of legacy.
   Values assigned into the location follow D-040 value semantics. Repeated
   imports within a context preserve the same exported binding locations.
   Initialise-once per context and failure caching (D-041) are unchanged.
3. **S1 — structs: D-040 value semantics retained** as a deliberate
   compatibility change from the documented reference semantics. Before L7
   integrates: inventory the documented examples and fixtures that rely on
   implicit struct sharing, provide migration guidance, and confirm important
   shared-state patterns (e.g. recursive fragment state, shared traversal state)
   stay expressible with approved mechanisms; any capability that would be lost
   comes back to the owner. `copy()` stays valid if practical, documented. Prepare
   nift.dev changes; publish with the release.
4. **M2 — module function scoping: D-038 lexical retained.** Free names resolve
   through the definition environment; imported functions keep access to their
   module's private lexical bindings. Deliberate compatibility change;
   documentation updates prepared.
5. **M3 — trailing `//` comments** are a legacy parser defect: accepted in both
   engines, with nift.dev-derived fixtures after declarations, assignments,
   calls, returns and closing delimiters; validation stays before execution.
6. **Process:** every proposed language-semantics change carries a minimal
   executable legacy reproducer and the relevant documentation excerpt (two
   decisions so far rested on inaccurate descriptions of legacy).

## D-047 — Script-context `@` syntax; struct migration; C2/C3 status (owner, 2026-10-20)

1. **`@fn(...)` is invalid in standalone script contexts** (standalone `.f`
   scripts, script packages, the REPL, `@script` blocks) — an intentional
   syntax-tightening under D-045, not a legacy correction. Canonical forms:
   scripts `fn(name(args)) { … }`, templates `@fn(name(args)) { … }`. The rule
   follows the **parsing context, not the file extension** (a file used as a
   template or fragment keeps template syntax). Both engines reject it before any
   execution or side effect; the offending package fixture is corrected to
   `fn(...)`; old/new executable examples, migration guidance and a nift.dev
   clarification ship with the release. Usage check (2026-10-20): `@fn` in `.f`
   files: 0 in 79 project scripts, 0 in 10 package files, 5 in the repository's
   own tests — rare and test/agent-generated, so a clean break.
   Other `@` forms are **not** banned automatically; inventory of `@`-prefixed
   script directives in legacy: `@for @import @if @fn @while @script @item
   @content @return @input @fragment @struct @paginate @markup @json @enum @type
   @ent @context` (+ internal `@__*`). Findings needing a policy decision:
   nift.dev documents bare `import(...)` as canonical in script land with
   `@import(...)` "accepted for compatibility" (13 project scripts and 3 package
   files use `@import`); `@struct(Name)` is the **only** struct form nift.dev
   shows, and all package files use it.
2. **Struct migration (D-040/D-046 #3) approved without a new shared-reference
   mechanism.** Identity-dependent graphs use ids into an owning collection or
   handles; migration examples are executable and in documented syntax. A
   reference type is evaluated separately only if a concrete use case shows the
   existing mechanisms are inadequate.
3. **C2** accounting rule (first interning context on a thread pays, once)
   accepted provisionally; CP23 revisits thread-local atom lifetime and context
   isolation for long-lived contexts.
4. **C3 stays open** until `join` and `replace` are both integrated and
   certified, the heap-limit-zero ambiguity is corrected (explicit "disabled"
   state; zero means no budget left), the permanent bypass reproducers pass, and
   the shared reservation mechanism is verified against the documented budget
   semantics. The `vm.cpp` change is sequenced when ownership permits.
   *Status 2026-10-21: closed.* `join` and `replace` integrated and certified
   (@7120b01); heap-limit-zero corrected with an explicit no-budget value
   (`Context::kNoHeapLimit`), certified @f6a09c7; the bypass reproducer is the
   permanent `heap-limit-zero` test (fails on the old code); `reserve_result`
   and `over_budget` test against the limit itself, as documented in the L7
   step-2 record §6.4–6.5. No `vm.cpp` change was needed: the VM already
   passes 0 for "no room left".


**Amendment (owner 2026-11-11 02:53, D-067 #3).** See the amendment at the end of D-048: the VM-only refusal of
plain `@fn(...)` in script context, with legacy unchanged, is approved.

## D-048 — Script context uses scripting syntax only; `@` directives are template syntax (owner, 2026-10-20)

Supersedes the "other `@` forms are not banned automatically" part of D-047 #1.

1. **Rule.** `@`-prefixed directives are template syntax. In script parsing
   context (standalone scripts, script packages, REPL input, executable script
   blocks; decided by parser context, not file extension) only the canonical
   scripting statements are accepted. `@fn`, `@import` and `@struct` are
   rejected there, with **no deprecated compatibility aliases**. Rejection
   happens before any execution or side effect, in both engines.
2. **Remaining directives.** Each of the other `@` forms accepted in script
   context today (`@for @if @while @script @item @content @return @input
   @fragment @paginate @markup @json @enum @type @ent @context`, internal
   `@__*`) gets its canonical scripting equivalent established from the actual
   grammar of both engines, never by mechanically stripping `@`. A form with no
   scripting equivalent is listed as such. The inventory, with per-form legacy
   reproducers and doc excerpts (D-046), comes back for review before the
   rejection is implemented for those forms.
3. **Template text is untouched.** Template-parsed content, nested templates,
   string literals, comments and generated template source keep `@` syntax.
   The migration is parser-aware and targeted. No bulk textual removal of `@`.
4. **Migration.** Package files, project scripts and repository fixtures that
   use `@fn`/`@import`/`@struct` as script directives are migrated, with
   behaviour verified before and after. No test is removed to make a suite pass.
   Packages are verified against the Runtime 2.0 build. Released Nift 4.x
   behaviour is not changed. Updated packages are checked against the release
   versions they claim to support, and are not published if they need an
   unreleased runtime.
5. **Documentation (D-045).** nift.dev changes are prepared but not published
   before the release: remove the `@import`-in-scripts compatibility promise,
   switch struct docs and package examples to the verified canonical script
   form, and add a short migration note on the intentional tightening.
6. **Delegation and sequencing.** This work, `@fn` rejection and trailing `//`
   comments (D-046 #5) form one DeepSeek parser assignment. DeepSeek owns the
   parser files, syntax fixtures and fixture/package migrations it is given.
   Claude reviews independently and runs certification. It starts only after
   CP15 integrates and parser ownership is free, and does not interrupt CP15,
   CP17, CP12B, L7 or their certification.

### D-047/D-048 inventory correction (2026-10-20)

The D-047 figures "`@struct` in 7 package files, `@import` in 3" were measured
on the **nift-packages snapshot used by `make certify`** (`nift-packages.tgz`:
seven native-binding packages, commits of 2026-09-20), not on the current
package repositories. DeepSeek's audit of the 25 current package repositories
(2026-10-20) finds package functions already written `fn(...)` and structs
`struct(...)`, no executable `@fn`/`@struct` script directives (two `@struct`
occurrences are tests asserting the form is absent), and `@import(...)` as the
only script-directive form in use (393 occurrences in 90 files). Two
consequences: the certify package snapshot is stale and still uses
`@struct`/`@import`, so it must be refreshed from the migrated repositories
before D-048 enforcement lands (otherwise certification would test obsolete
sources); and the D-048 rule itself is unchanged.

### CP15B scope correction (coordinator, 2026-10-20)

DeepSeek's CP15B inventory (on CP12B `3b632fff` and `4f5a00df`) found no viable
pure built-in slice; the "~57 methods" in the forecast misread D-036's count of
curated *scripts*. Reconciliation by receiver: (1) `method:<name>` reasons for
names that already have a native (`join`, `to_string`, `push`, `stringify`,
`contains`, …) come mostly from the inventory's chain masking of a refused
receiver; the receiver is the blocker, and `owner()` will classify them by
receiver instead of by name. (2) Collections `map()`/`set()`/`stack()` and their
mutators are values under D-040; legacy's collection handle strings are an
implementation detail, not the contract; their VM representation depends on the
L7 value layer, so they form a later L7-dependent CP15B slice. CP17 typed handles
remain for identity-bearing resources only. (3) `same()` stays with L7, applying
D-040 with old/new compatibility tests; no new decision. (4) CP15B slice 1 is its
other D-036 reason: literal spellings and string interpolation. No corpus or
acceptance criterion changes.

### D-043 amendment: Linode budget US$20 (owner, 2026-10-20)

The campaign's Linode ceiling is **US$20 total** committed spend (accrued plus
live campaign instances to their TTL plus recorded external spend), replacing
the US$10 ceiling. It is not an additional US$20. Necessary certification or
development work does not stall when spend passes US$10; ask the owner before
exceeding US$20. Existing controls are unchanged: inexpensive instance types,
pinned host keys, TTLs, prompt deletion after evidence is retrieved (verified
via the API), shared accounting across Claude and DeepSeek. Recorded in
`scratchpad/infra/linode.py` (`BUDGET_USD = 20.0`).

### CP16 legacy `run()` process-group exception (owner, 2026-10-20; provisional)

Legacy `run()` children stay in Nift's process group during the transition
(two existing contracts need it: a killpg of Nift's group reaches them, and a
hook child survives a SIGKILL of Nift alone). Legacy `run()` therefore does not
guarantee background-process cleanup at context end or a dedicated process
group/terminal ownership; SIGINT/SIGTERM cleanup of its children is required
and tested. The Host layer's own `run()` keeps every CP16/CP17 lifecycle,
cleanup and ownership guarantee and is never weakened for legacy. Tests assert
by ownership class (no contradictory "nothing left behind" for permitted legacy
descendants) and include an unrelated process surviving Nift's signal handling.
**Removal condition: CP25 cannot complete while any execution path relies on
this exception.** Not a broader redesign of legacy process management.

**Amendment (owner, 2026-10-20): no legacy subreaper.** The Linux-only
legacy subreaper (child adoption and reaping for legacy-launched descendants)
is withdrawn and is to be removed from the CP16 branch; no further engineering,
review or certification effort goes into legacy process management. Legacy
`run()` keeps its historical behaviour, including its existing background-child
lifetime and signal-handling limitations. Those limitations are temporary
compatibility exceptions under this record, not satisfied CP16 guarantees;
legacy tests assert the historical behaviour by ownership class and claim no
cleanup guarantee for legacy-launched descendants. The Host layer's process
ownership, cleanup, cancellation and resource guarantees are unchanged.
Inexpensive correctness fixes that benefit both engines may stay; a
legacy-only, Linux-specific adoption mechanism does not qualify. The CP25
removal condition now covers the legacy launcher and all of its exceptions.


**Amendment (owner 2026-11-11 02:53, D-067 #3; also amends D-047 #1).** The VM refuses plain `@fn(...)` in
script context while the legacy interpreter keeps accepting it, with no legacy-side change. This VM-only refusal is
the approved behaviour (inventory row `fn-flags`, approved).

## D-049 — Package compatibility targets Runtime 2.0 (owner, 2026-10-20)

There are no existing package users to protect, and Runtime 2.0 replaces the
legacy engine, so package compatibility work targets Runtime 2.0 and later
Nift releases rather than historical 4.x minimum versions.

1. Runtime 2.0 is the package compatibility baseline.
2. The historical minimum-version campaign (v4.6.0–v4.12.0) stops. Its
   evidence is kept as a record, including the finding that the published
   "Requires Nift 4.6.0 or later" is untrue for fuzzy, redis, semver, url,
   csv and diff. No engineering time goes into package compatibility with
   Nift 4.6–4.12.
3. The canonical `import()` migration, the MDX template corrections and the
   package tests are kept.
4. All 25 packages stay in scope, including `csv` and `diff`. Failures on
   historical releases are not grounds to drop them.
5. Package test failures on Runtime 2.0 are used to identify missing runtime
   capabilities or package defects.
6. README minimum-version wording is set once the Runtime 2.0 release version
   is known. Until then no README claims compatibility that has not been
   tested.
7. Package CI exercises supported future Nift versions, including the latest
   runtime development branch where practical. Compatibility is a continuous
   regression-testing guarantee, not a historical version number.

Package compatibility does not wait for CP25: packages are tested against the
developing VM now, and certified finally against the release candidate. No
package pushes are authorised yet. This is not a new package-certification
project; existing package tests run against the new runtime as part of normal
development.

## D-050 — Modified mode returns to v4.12 semantics (owner, 2026-10-23)

Modified mode uses the v4.12 timestamp rule: rebuild when a dependency is
missing or its mtime is `>=` the page-info mtime. Page-info is stamped with the
filesystem's own clock (`refresh_modified_time_now`), which is retained because
v4.12's user-space stamp permanently missed 999/1,000 immediate edits. The
CP01 timing records, tick hashes, `.nift/clock` lifecycle, render-clock mutex
and repeated checks are removed. Edits during a running build, metadata-
preserving replacements, multiple writes in one timestamp tick, backdated
mtimes and backwards clock steps are explicitly outside the modified-mode
contract; hash mode remains the reliable opt-in. The `platform()` invalidation
and D-020 recovery are retained. No replacement detection machinery may be
added under this decision.

## D-051 — Default filesystem containment is removed (owner, 2026-10-23)

Ordinary Nift programs are unrestricted by default. The default project-root
containment on `@path`/`@pathto` targets and the requirement canonicalisation
in `ProjectInfo::build_reasons` are removed; they added two `weakly_canonical`
resolutions per requirement on every no-op build without enforcing a user
policy. Package and metadata integrity checks, custom-output safety, watch-list
scoping and path resolution remain. `--fs-root`/`NIFT_FS_ROOT` is the explicit
opt-in restriction and is enforced for `@path`/`@pathto` and expression-level
filesystem operations. No caching or replacement containment machinery may be
added. `@path` targets outside the output tree keep the existing deployment
warning and dependency tracking.

## D-052 — CP20 launch gate: no regression against pre-CP20 (owner, 2026-10-23)

1. CP20's binding launch-performance gate is **≤ 1.03 × pre-CP20** (`e1f2f59f`)
   for a single external command, `true|true|true` and `true|cat|cat`, measured
   with the approved interleaved/paired-round method on four dedicated vCPUs
   with GCC 13.3.
2. The earlier roadmap objective "Launch ≤ Bash + 10%" is **deferred, not
   passed**. It is carried to the separate Host performance checkpoint
   (P1/vfork) and remains visible for CP27 reporting. CP20 does not optimise
   for it and must not claim Bash parity.
3. The Bash comparison is reported for information only.

## D-054 — VM-owned stateful sessions do not fall back (owner, 2026-10-27; CP19)

Once a shell or interactive session becomes VM-owned, all subsequent script
units use that session's persistent VM state. Ownership is an explicit session
invariant, independent of the current contents of visible variables. Emptying
the namespace does not permit an engine change.

1. Compile refusal occurs before execution, produces no Host effects or partial
   namespace changes, and preserves all existing bindings, closures, Errors,
   handles and other live state. The session remains VM-owned after refusal.
2. No unit in a VM-owned session falls back to legacy. No cross-engine transfer
   of live closures, handles or executable values is required or authorised.
3. Runtime errors retain the established VM exception and side-effect semantics.
   They are not compile refusals and never cause replay on legacy.
4. Independent stateless `NIFT_VM=1` execution retains its separately approved
   pre-effect engine-selection policy. `NIFT_VM=require` continues to refuse
   unsupported code before effects.
5. Initial session admission must be explicit and stable. Explicit VM-off
   compatibility execution must remain distinguishable from release-supported
   VM-owned entry points and must never count as VM execution coverage.

This is an observable compatibility change for stateful mode-1 sessions:
unsupported later units are refused rather than evaluated on legacy. Test
witnesses must verify state preservation as well as the absence of effects,
including a refusal after the visible namespace becomes empty. This decision
does not change Error identity, provenance, handler selection or resource
cleanup obligations, and does not authorise temporary CP17 Host-context APIs.

Identifier check: D-054 was absent from both this register and the authoritative
integration tip `8e65196b` when the owner decision was recorded. D-053 remained
unassigned at that time; it now records the CP24 deferral (below, recorded
2026-10-29 for the owner decision of 2026-10-26).

## D-055 — CP22/CP23 concurrency boundary (owner, 2026-10-28)

Runtime 2.0 preserves the established real-parallelism contract of `thread()` and `async`. Cooperative scheduling is not an equivalent replacement for parallel execution.

1. **CP22 owns coroutine infrastructure:** cooperative scheduling, futures, suspension and resumption, timers, cancellation, Error propagation, future lifetimes, the event loop, and nonblocking Host-operation integration.

2. **Cooperative admission requires proof:** the VM may execute an existing `async` program cooperatively only when compiler analysis establishes that its observable behaviour is compatible with the approved parallel semantics. This includes the effects of shared mutable state, aliases, captured Cells, globals, Host operations, timing, output ordering and cancellation. Otherwise, compilation refuses before execution. No speculative replay or partial effects are permitted.

3. **CP23 owns genuine parallel execution:** real parallel `thread()`, parallel `async` for CPU-bound and blocking workloads, `mutex`, `atomic`, capture-only isolates, channels, cross-isolate ownership and parallel execution certification. Work-stealing is optional if another implementation satisfies the required semantics and performance objectives.

4. **No permanent parallelism refusals:** CP23 must eliminate refusals caused by missing parallel execution for legacy-supported concurrency features. Reclassifying those refusals under unrelated diagnostics does not satisfy this requirement. VM execution, rather than legacy fallback, is mandatory for supported programs.

5. **Performance:** CP23 must compare legacy and VM execution for CPU-bound interpreted and native workloads at 1, 2, 4 and 8 concurrent tasks. A quantitative scaling acceptance threshold must be approved before CP23 certification. No unspecified performance factor constitutes a passing gate.

6. **Dependencies:** CP22 development may proceed from CP12, but its nonblocking Host integration requires the accepted CP17, CP19 and CP21 interfaces. Those integrations remain mandatory CP22 completion requirements and may not be deferred to CP23 merely to close CP22.

7. **Certification:** CP22's cooperative-admission proof requires independent adversarial review. CP23 must independently establish genuine parallel execution, cancellation, resource lifetime, correctness and race freedom. Existing compatibility, mutation, sanitizer and platform requirements remain in force. Neither checkpoint may claim full acceptance while its mandatory requirements remain incomplete.

8. **Release gate:** CP25's zero-fallback requirement depends on CP23 completing the supported parallelism migration. This decision changes checkpoint ownership and sequencing, not the required final Runtime 2.0 capabilities.

## D-056 — Managed resource exception-unwind semantics (owner, 2026-10-28)

**Decision:** Runtime 2.0 adopts uniform exception-unwind release semantics for managed files. Legacy evaluator-specific rollback behaviour is not normative.

1. **Exceptional abandonment.** When an Error unwinds execution, managed files exclusively owned by the execution frames or scopes actually abandoned by that unwind are closed and discarded silently. Buffered but unwritten changes are not flushed.

2. **Normal completion.** A managed file abandoned by normally completed execution remains a reportable resource obligation. Normal block, loop-iteration and function completion must not defer abandonment merely because stale VM registers retain references.

3. **Surviving ownership.** Resources reachable through surviving aliases, outer bindings or valid ownership transfers remain live. An Error caught within a surviving execution context does not release unrelated resources owned by that context.

4. **Try/catch boundaries.** An aborted try or catch body constitutes exceptional abandonment within its relevant scope. A normally completed body follows normal resource reporting rules.

5. **Error integrity.** Cleanup must preserve the primary Error, its identity and its propagation semantics. Cleanup must not convert failing execution into false success or suppress independently reportable obligations from completed execution.

6. **Resource safety.** Release must be exactly once, must not write discarded buffers, and must not depend on successful allocation during destruction or emergency teardown. Handle-table and Host cleanup must preserve these guarantees under injected allocation failures.

7. **Compatibility.** Legacy remains evidence for legitimate language behaviour and genuine errors, not authority for inconsistent textual-versus-prepared evaluation boundaries. Intentional corrections must be explicitly classified, documented and tested against the approved Runtime 2.0 contract.

8. **Certification.** Acceptance requires named normal-completion, exceptional-unwind, nested try/catch, loop, function, alias-survival, report-multiplicity, unsaved-write and allocation-failure witnesses; independent mutation controls; and applicable sanitizer and platform gates. A passing legacy differential count alone does not establish compliance.

**Scope:** CP17 + N2 managed-resource semantics. This decision does not authorise unrelated language changes, integration, pushing or release.

## D-053 — CP24 deferred post-release (owner, 2026-10-26; recorded 2026-10-29)

CP24 (template compiler and Host-op dependency recording) is deferred until
after the initial Runtime 2.0 release: "parked, not abandoned". It is neither
failed nor accepted for default replacement.

1. The release keeps the existing website template renderer as the default; the
   experimental CP24 adapter stays out of `runtime-2`. Website builds that do not
   use scripting must not pay for the VM.
2. CP24's exit gate is unchanged and applies whenever CP24 (or any other template
   replacement) resumes: byte-identical output, no slower than official v4.12.0
   and same-commit legacy on the same work, RSS no more than v4.12 + 10%, and
   explicit owner approval.
3. Website build time and memory on the retained renderer remain release
   non-regression obligations against v4.12.0 (reported in CP27).
4. CP17's routing tripwire (build, page and hook code never reach the VM) stays
   in force for the release and is part of release certification.
5. The CP24 feasibility evidence (frozen candidates, benchmarks, inventories)
   is preserved; nothing else in the correctness, performance or certification
   requirements is relaxed.

Recording note: the 2026-10-26 proposal suggested D-053 for this decision; no
other decision has used the identifier.

## D-057 — CP25 split: CP25-R release readiness and CP25-L legacy retirement (owner, 2026-10-29)

With CP24 deferred (D-053), templates and hooks keep running on the legacy
evaluator, so the original CP25 ("fallback count zero; delete legacy evaluator",
zero fallbacks on all corpora) cannot complete before the release.

1. **CP25-L** is the original CP25, unchanged: zero fallbacks on all corpora for
   one full test cycle and the legacy evaluator deleted. It depends on CP24 and
   on CP25-R, and stays a visible, deferred obligation in the release ledger.
2. **CP25-R** is a separate release-readiness checkpoint: bring the supported
   scripting and shell surface to the agreed release contract, remove
   unnecessary legacy fallbacks, preserve intentional refusals, and produce a
   fully classified refusal inventory. Its gates are recorded in
   `checkpoints/cp25r-scope.md`.
3. CP25-R is never reported as completion of CP25 or CP25-L, and deferred
   requirements are never labelled as passed.
4. CP25-R does not remove the legacy evaluator, does not change the template
   engine, and may not regress website performance.
5. A refusal disappears only when the VM genuinely supports the behaviour, with
   equivalent results and error behaviour and VM-only execution counts.
   Admission rules are not weakened to meet a count, and refusal-before-effects
   guarantees are kept.
6. CP26 removes only scaffolding for paths retired by CP25-L; scaffolding the
   retained legacy engine still needs stays.
7. Default engine routing (whether `NIFT_VM` unset selects the VM for scripts)
   is not decided by this record.

D-036 remains the record that no permanent legacy fallback is accepted: the
template engine is a scheduled post-release retirement (CP25-L), not a
permanent fallback.

## D-058 — Writes to immutable bindings (owner, 2026-10-29, final 2026-10-30; CP25-R)

Refines D-009 #2 (const is immutable everywhere) and applies D-009 #5 (errors
at the actual location). The contract is the same in every context: top
level, functions, loop-evaluator code and loop-called functions.

| Operation | Contract |
| --- | --- |
| Plain assignment to a const | Rejected before the right-hand side is evaluated |
| Compound assignment to a const | The right-hand side and the operation are evaluated first (their side effects and errors, e.g. division by zero, come first); then the write is rejected |
| `++` / `--` on a const | Legacy's order: the numeric-operand check, then the const Error |
| Const declared inside a loop | Immutable (D-009 #2) |
| Error location | The write itself (D-009 #5) |

This is legacy's top-level behaviour (`semantics.md`: top-level const
unchanged). The owner's earlier proposal (2026-10-29) to reject compound
assignments before their value too is superseded: it would have changed
established top-level behaviour.

Approved differences from legacy's loop evaluator: it never enforces a const
declared in the loop; for a const declared outside it, it evaluates a plain
assignment's value before rejecting and rejects a compound assignment before
its value; it reports a write in a loop-called function at the call site.

Tests: `tests/runtime2/cp25/const/` (loop cases against an oracle, the same
layout run once outside a loop, with legacy shown to differ where it does);
`semantics/loop_const.f` with its `.r2` goldens, approved in the VM lane by
`tools/rt-diff/policies/d009-const-vm.json`.

## D-059 — D-018c subcase: arithmetic errors inside `$[...]` (owner, 2026-10-30; CP25-R)

An arithmetic Error (division or modulo by zero, non-integer modulo
operands, 64-bit overflow, non-finite result) raised by an operation inside
an interpolation is reported by the VM at the failing operation. Legacy
reports it at the interpolated string, or, when the string is the single
argument of `print`, at the print statement with a `print: ` prefix; that
prefix may be omitted for such an error. Message text (prefix aside), error
kind, catchability, evaluation order and earlier side effects are unchanged;
errors outside interpolation keep their existing policies.

Recognised only by `vm_policies.interpolation_origin` ("D-018c
interpolation"): one diagnostic each, same file, an arithmetic message equal
after removing the prefix, legacy's position at that string or at a
one-argument `print` of it, no nested interpolated string, and exactly one
operation inside the string that can raise the message, located
independently from the AST, at the VM's position. Controls:
`tests/runtime2/vm_policy_negative.py`.

## D-060 — Thread context on uncaught Host errors (owner, 2026-10-30; CP23 R4 integration)

1. An uncaught Host-operation Error raised inside a thread and reported
   through `join` keeps one `thread: ` context prefix per genuine thread
   boundary on the VM (the CP23 R3/R4 thread-reporting contract); legacy
   reports the bare Host message (`sha256_file: cannot open path`). This is
   an approved diagnostic difference. The production diagnostic is not
   changed and no legacy golden is changed.
2. Recognised only by `vm_policies.thread_host_context` ("CP23 thread
   host-error context", rt-diff class `cp23-thread-host-context`): the
   Host message is identical after the prefixes and is an approved Host
   failure (sha256_file's "cannot open path" only); the location is equal
   or the D-018c failing call (checked independently of the prefix); the
   failing function is only ever started as a thread, at one depth equal
   to the prefix count; the program has no try/catch; stdout and exit are
   equal. A caught or synchronous Host error never acquires the prefix
   (checked on the VM: synchronous `sha256_file` reports the bare message,
   a caught one is caught on both engines).
3. Error identity, code, catchability and provenance are unchanged.

Controls: `tests/runtime2/vm_policy_negative.py`.

## D-061 — `exit` in a terminal startup file (owner, 2026-10-31; VM default)

1. A bare `exit` in `~/.niftrc` or in the file of `nift -i file` is an
   unsupported Nift statement, not a request to end the session (it is never
   the shell's `exit` command). On a terminal the VM refuses it before it
   runs: the refusal names it ("initialisation from <file> was interrupted at
   line L (`exit`); later statements in it were not run"), the file stops
   there and no later statement of it runs, the effects of earlier
   statements are kept, and the session continues at a usable prompt.
2. Legacy fails on it at startup ("unknown value or malformed expression:
   exit") and ends the session with status 1. This departure is approved
   explicitly. Without a terminal the VM refuses the file as a whole: none
   of its statements runs, not even those before the `exit`, and the exit
   status is 1. Only the exit status matches legacy: piped legacy runs the
   statements before the `exit` (their output and effects happen) and then
   fails on it. (Correction after Review 8: this point previously said
   "as in legacy".)
3. Migration note: a startup file that relied on `exit` ending the session
   no longer does so on a terminal; remove the line (or end the session from
   the prompt). Recorded in `compatibility.md` (Approved breaks) and
   `tests/runtime2/vm_default/REFUSAL_MATRIX.md`.

Witness: `tests/runtime2/vm_default/vm_default.py` §18c (`.niftrc` and
`-i file` on a pty: earlier effect kept, nothing later runs, the prompt then
works; legacy under `NIFT_VM=0` exits 1; piped exits 1).

## D-062 — Script `args`/`cmd` on the VM: modules and assignment (owner, 2026-10-31; weekend args/cmd)

Context: `checkpoints/weekend-args-cmd.md` (the six real-run fallback units of
the G03 study, all supported on the VM).

1. **Modules: a TEMPORARY limitation, not a language rule.** A module that names
   `args` or `cmd` is refused before any effect, with the stable reason
   `import-scope:invocation-binding`. This must not become established
   behaviour: legacy lets an imported function called from the entry script see
   the entry's bindings, and that is worth preserving. Obligation: the packages
   checkpoint tests module top-level evaluation against an imported function
   called from the entry script, and lifts or rules on this refusal.
2. **Assignment: an approved semantic change (not legacy parity).** A write to
   the injected bindings (`args = …`, `cmd = …`, an element of `args`, a compound
   assignment or increment) is a program error rejected before execution, with
   the reason `invocation-assign:<name>`: nothing of the program runs, the
   statements before it included. Legacy fails fatally at the statement, after
   the earlier statements ran; that timing is not reproduced. The diagnostic is
   worded as a program error ("cannot assign to `args`: the script invocation
   bindings `args` and `cmd` are read-only"), never as missing VM functionality
   ("not supported by the Runtime 2.0 VM yet"), with no legacy instruction and
   no request to report it. The same reason is used in VM sessions.
3. A top-level `args := …` / `cmd := …` in the entry script still replaces the
   binding (legacy's `is_script_invocation` exception); it is not a write.
4. Scope: only the injected bindings. General assignment semantics (const,
   immut, catch bindings) are unchanged. `args.push(…)` and the other mutating
   methods keep legacy's runtime error ("cannot mutate const array: args").
   Migration: declare a new binding (`my_args := args`) or replace the binding
   with a top-level declaration before any use. Recorded in `compatibility.md`
   (Approved breaks).

Witness: `tests/runtime2/args_cmd/args_cmd.py` §3 (writes, with an earlier file
effect and output that must not happen; module refusals) and §4 (session).

## D-064 — Coroutines in programs that import modules (owner, 2026-10-31; weekend `coro-module`)

(Numbered D-063 on weekend/r2; renumbered at the refsem-a integration, where
D-063 is the weekend collections decision. References below and in the
coro-module records were updated.)

(D-062 is claimed by `cand/args-cmd`.)

1. Under the parallel task model (the default), a coroutine unit with static
   imports runs on the VM as its linked (inline) program would. The
   cooperative model (`NIFT_VM_COOPERATIVE_THREADS=1`) keeps CP22's
   whole-unit `coro-module` refusal.
2. **Kept refused before any effect: `coro-module-init-task`.** Code in a
   module's top-level initialisation that may start a task: a `thread()` call,
   a direct call of a named async function, a named call that may reach
   either, or a call through a value (a binding, a member such as `o.f(x)`, a
   method or built-in given a function value) while some function value in
   the unit may start a task. Lambda bodies created there count as
   initialisation code. Creating an async lambda there is not itself a task
   start; calling it during initialisation is.
3. **Kept refused before any effect: `coro-module-capture`.** A task whose
   code may read or write an imported module's top-level binding: directly,
   through functions it reaches by name, through function values and lambdas
   passed into it, or through a call through a value while some function value
   or lambda in the unit may touch module state. A call is through a value by
   its structure (a callee that is not a resolved function, and any method or
   built-in call given an argument that may be a function), never by the
   method's name. The analysis does not track which value reaches which call:
   one such function value anywhere makes every call through a value suspect.
   It is also refused when a module binding may hold a handle.
4. Out of scope, unchanged:
   - persistent-session coroutines (`session:coroutine`);
   - CP24;
   - metadata Stage A/B;
   - the `Value` redesign;
   - package resolution;
   - `args`/`cmd` in modules.

The independent review of `fff92edd` added two refusals of programs that
legacy itself fails: `coro-module-private-async` and
`coro-module-before-import`. Their classification is pending an owner ruling.

Review of `32537bb6` (owner ruling 2026-11-01): a named async function used as
a value, including stored in an object literal, stays refused before execution
(`coro-async-value`, a base CP22 rule; the object-literal form had escaped it).
Independently of that base rule, the module rules treat such a value as a task
start and a task root wherever it is called, and `coro-module-private-async`
also refuses a call through a value in module code while one exists, or
anywhere while a module's one does (legacy: "module-private async callables are
unsupported", also for `o.f(3)` from the entry). Async lambdas are not refused
for being called from module code: legacy runs them. Direct calls of exported
async functions from the entry stay admitted.

Record: `checkpoints/weekend-coro-module.md`. Witnesses:
`tests/runtime2/coro_module/` (`make test-runtime2-coro-module`, mutants
`make test-runtime2-coro-module-mutants`).

## D-NEXT-packages — Package imports and import-time program errors on the VM (owner, 2026-11-01; number assigned at integration)

Context: `checkpoints/weekend-packages.md` (phase 1 contract, owner rulings Q1–Q6).
Supersedes D-062 #1 for imported modules; D-062 is kept as historical evidence.
Guiding line: an unsupported VM capability is a feature refusal; an invalid program
is a program error (legacy text, stable reason id, truthful position, exit 1, no
"not supported yet").

1. **Import-resolution errors are rejected before running** (Q1). This covers:
   - a missing or uninstalled package, and every other package-resolution step;
   - a missing imported file;
   - an import cycle;
   - a package-path escape.

   Each is reported at the real import statement, with legacy's text and frame
   prefixes. No user-program side effect happens, including from statements before
   the import. This is an approved difference: legacy runs the earlier statements;
   the text, position and exit status are the same. An import that a top-level
   `return` leaves unreached is still rejected. The diagnostic says so, and does not
   claim the import ran.

   Scope: whole-program execution only.
   - Runtime errors in successfully imported modules, successful imports'
     initialisation order and run-once rules, interactive startup files and
     persistent sessions are unchanged.
2. **Package lock** (Q2).
   - Legacy's lock file and mechanism (`.nift/.packages-transaction.lock`, a shared
     descriptor lock).
   - Created atomically if `.nift/` exists and the file is missing; never truncated
     or replaced.
   - Held for the resolution critical section and released on every path.
   - An acquisition failure is a clear error before anything runs.
   - `.nift/` is never created by an import; nothing is installed, modified or
     published, and no other artifact is written.
   - This lock housekeeping is the single allowed exception to "no side effects
     before execution". It is package-management state, not a user-program effect.
3. **Legacy texts** (Q3), including the pathless `file does not exist`.
4. **`args`/`cmd` in modules** (Q4/Q5), resolved by binding identity, never by
   spelling alone.
   - An imported function sees the entry's binding where static resolution proves it
     is that binding.
   - Module top-level or init-time reads of the injected binding are refused before
     any effect (`import-scope:invocation-init`, program-error wording).
   - A module's private `args := …` is supported, as in legacy, with ordinary binding
     rules.
   - Ambiguous or unprovable access is refused before any effect.
   - No unrestricted module-global access and no module-binding redesign. The scope of
     the private-binding support is reported in the checkpoint before it is
     implemented.
5. **Module program errors** (Q6). A module-level `return <value>`, `export-missing`
   and `export-duplicate` are program errors refused before running, with legacy text,
   truthful position, exit 1 and a stable reason id. So is `export(<non-name>)`
   (`export requires a binding name`). A bare `return` (legal in legacy) stays a
   feature refusal.
6. **Follow-ups** (owner, 2026-11-01).
   - The private `args`/`cmd` support uses a narrow linker rename, limited to those two
     names and the declaring module's own binding. It is not general name-clash
     renaming. Messages still quote `args`/`cmd`.
   - `PackageTransaction` gets a separate VM-only read-lock function that never
     creates `.nift/`. Legacy call paths stay byte-identical, verified by the legacy
     package tests under `NIFT_VM=0`.

Witness: `tests/runtime2/packages/` (phase 2).

## D-063 — map, set and stack are values on the VM (owner, 2026-10-31; weekend collections)

(D-062 is claimed by `cand/args-cmd`; renumber at integration if needed.)

Owner rulings for the weekend collections candidate (`cand/collections`,
`checkpoints/weekend-collections.md`):

1. **Value semantics, an intentional departure from legacy** (where
   collections are references). `b := a` gives an independent copy; copying is
   **deep** and the same for assignment, function arguments, returns, closure
   capture (values read through a closure), container insertion and
   extraction, struct fields, `for` values and thread arguments, returns and
   captures. Copy-on-write is allowed only as an unobservable optimisation.
   This applies D-040 #3 ("maps and sets are values") to stacks as well.
2. **Self-insertion snapshots**: `m.set(k, m)`, `k.push(k)`, `s.add(s)` store
   the receiver's value before the call; ordinary values stay acyclic (D-040
   addendum). Legacy's "would create a cyclic reference" error disappears.
3. **`same()` follows D-040 #4**: identity only for identity-bearing values.
   Collections (like arrays, objects and structs) are ordinary values and
   `same()` on them is legacy's error text "same: operands must be
   identity-bearing values" (legacy: `true`/`false` by storage). Callables and
   file/stream handles compare by identity (legacy-equal). **Owner ruling
   2026-11-01:** mutex, atomic, channel, future and thread compare by
   identity too (approved difference; legacy errors): the same resource or
   task is `true`, independently created ones `false`, by the stable handle
   (kind, id, generation) or the task record, never a reusable address; no
   ownership, transfer or lifetime change. Compatibility limitation: the
   arguments must be plain names (`same:argument-shape`, refused before any
   effect in every program): legacy reads a string argument as a binding name
   (`same("f", "f")`) and may evaluate other shapes twice.
4. **Everything observable apart from copying is legacy's**: constructors
   (exact `map()`/`set()`/`stack()`), methods and their return values, key and
   member rules (bool, number, string, and legacy's tagged strings that are
   values here: collections structurally, callables by identity), insertion
   order, stack order (iteration top first, `stringify` bottom first),
   equality per kind, `type()` "collection", the binding type "string",
   `stringify`/`prettify`, `for` forms and errors, and every error text,
   position and (un)catchability. Anything not modelled is refused before
   execution (checkpoint §3), never reinterpreted.
5. **Value kind.** `rt::Kind::Collection = 15` uses the **last free value of
   the 4-bit kind tag**; one tag serves map, set and stack (the sub-kind is in
   the object). A later kind needs the tag mask widened (tag bits 4-6 are
   unused). No `Value` redesign.
6. Threads may carry collections (a copy, `WireKind::Collection`), where
   legacy refuses them ("non-transferable resource"): part of the approved
   value-semantics break. Stores into shared state (`mutex(v)`, `mx.set(v)`,
   `send`) of a value holding a collection at any depth stay refused
   (`collection-argument:`); the atomics and `channel()` take only numbers
   or bools.
7. **Snapshot iteration (owner ruling 2026-11-01; a deliberate Runtime 2.0
   semantic change):** `for ((k, v) : m)` visits the entries captured at loop
   entry, in the documented insertion order (no new order guarantee);
   insertions, removals and value updates by the body do not affect the
   active snapshot. Legacy iterates the live entry vector over the count at
   entry (appended entries are skipped in both; it shows updates, shifts
   after a removal and is undefined when the loop outlives one). A removed
   entry is still visited; a body that removes the visited key again
   (`mp.remove(2)` on each pass) fails with "remove: key not found" on the
   second visit, which is this semantics (guard with `contains`).

Migration (`compatibility.md`, Approved breaks): code that relied on two
names sharing one collection must pass the collection back (`m = fill(m)`,
`return m`) or keep it in one binding; `copy()`/`deepcopy()` are no longer
needed to separate copies; `same()` cannot test collection sharing; a map
loop that mutates its map sees the entries of loop entry (re-read the map
after the loop, or guard a removal with `contains`); write `same(a, b)` with
binding names, never `same("a", "b")`.

Witnesses: `tests/runtime2/collections/approved/*.f` (VM golden and legacy
witness for each difference), `parity/*.f`, `refuse/*.f`, the heap checks and
sixteen mutation controls (`make test-runtime2-collections`,
`test-runtime2-collections-mutants`).

## D-NEXT-refsem — Reference semantics, Phase A: maps, sets, stacks and struct instances are references (owner, 2026-11-01)

(Placeholder identifier: the number is assigned at integration.) Owner
decision of 2026-11-01 (option 2: reference semantics with cycle creation
refused, in two phases; rulings D1-D9). Phase A is an internal engineering
milestone, not approval to release mixed semantics; Phase B (arrays and
objects) needs its own plan and owner approval. Record:
`checkpoints/weekend-refsem-a.md`.

**Supersedes** (the earlier decisions stay as historical records and are not
edited; every changed fixture, row and witness cites this decision):

- **D-063** #1 (collections are values), #2 (self-insertion snapshots), #3 as
  far as `same()` on a collection is an error, and #6 as far as it presents
  thread copies as the general value rule;
- **D-046** #3 and **D-040** V9 (struct instances as values; structural `==`
  for structs);
- the **D-040 addendum**'s "acyclic by construction" basis, for the reference
  kinds (acyclicity is now checked);
- the CP15 W11 reading of `copy()`/`deepcopy()` as the identity on plain
  values (`copy` is shallow, `deepcopy` a graph copy).

Not superseded: D-040 for arrays and objects (copy-on-write values until
Phase B) and its #3 handle rule (a copy never duplicates a resource); D-063
#4, #5 and #7 (re-affirmed by 6 below); D-055 and the CP23 ownership rules.

1. **Kinds.** Maps, sets, stacks (`rt::Kind::Collection`) and struct
   instances are identity-bearing mutable objects. Arrays and objects remain
   values until Phase B (9). Numbers and booleans are values; strings are
   immutable values; handles keep their identity.
2. **Within an isolate** assignment, declaration, synchronous arguments,
   returns, closure captures (cells), container storage and extraction,
   struct fields and `for` element bindings share the object: a mutation
   through any alias is visible through all (legacy-equal: legacy's
   collections and structs are tagged references). `same()` is identity
   (legacy-equal). `==` keeps legacy's rules: structural for collections,
   **identity for structs** (D4; `P() == P()` is false, `copy(p) == p`
   false). A map()/set()/stack() field initialiser is evaluated afresh for
   every instance: no two instances share one collection.
3. **Copying is explicit (D3).** `copy(v)` is shallow (Nift v4.2's
   contract): a new outer instance or collection whose fields, keys, values,
   members or elements are shared. An array or object is a value until Phase
   B, so `copy` returns the value itself, indistinguishable from a new outer
   array with shared elements. `deepcopy(v)` copies every reachable array,
   object, instance and collection once and preserves the sharing inside the
   copied graph; callables, handles and futures keep identity. Both reserve
   the heap budget before building. The two-argument filesystem
   `copy(src, dst)` is unchanged. Approved difference: legacy v4.4+ copies
   deeply in `copy`, and its `deepcopy` duplicates a collection reached twice.
4. **Cycles are refused (D1).** A store that would make a reference-kind
   object reachable from itself (through fields, keys, values, members,
   elements, arrays, objects, closure cells or resource payloads) is refused
   **before** anything changes, with a **catchable** Error, code
   `runtime.reference_cycle`: legacy's text where legacy refuses the same
   store ("set would create a cyclic reference", "push would create a cyclic
   reference", "assignment would create a cyclic reference: <field>"), else
   "reference cycle: the stored value would contain itself". There is no
   general garbage collector; refused graphs leak nothing. **Intentional
   limitation, prominently documented:** legacy accepts a cycle through an
   array, an object or a closure (`m.set(1, [m])`, `m.set(1, {"k": m})`, and
   the familiar JavaScript pattern of an object holding a closure that
   captures that object, `m.set("f", () => m.size())` inside a function); the
   VM refuses them. A closure over a top-level binding reads it by name (no
   heap edge) and is accepted. Legacy's refusals are fatal; the VM's are
   catchable (approved difference).
5. **Isolate boundaries.** Thread and parallel-async arguments, captures,
   globals and results cross as one alias-preserving graph copy
   (`TransferGraph`, unchanged): aliased arguments alias one copy in the
   worker; nothing mutable is shared between isolates; refcounts stay
   isolate-local; CP23 ownership and handle rules unchanged. Struct
   instances cross too (legacy refuses). **Cooperative async (D7):** at task
   creation (not first run) the arguments and the globals the task reads are
   copied as one alias-preserving graph, before any task state exists (a
   budget stop or failed allocation leaves the scheduler unchanged); handles
   and callables keep identity; the CP22 admission rules (no collection
   methods, struct code or captures in task code) stay. Equivalence with the
   parallel path is shown by differential fixtures run under both
   schedulers; any shape not covered stays refused by the existing
   cooperative admission.
6. **Iteration (D5).** A `for` loop over an array, map, set or stack visits
   the membership at loop entry; the body's mutations change the container,
   not the active iteration; elements are references. Maps take an explicit
   O(n), budget-checked snapshot (sets and stacks were already materialised;
   arrays are copy-on-write values). Re-affirms D-063 #7.
7. **Keys (D6).** A map, set or stack used as a map key or set member, for
   lookups too, is refused before any effect (`collection-mutable-key`):
   their contents may change after insertion, which a hash index cannot
   follow. Ordinary keys (bool, number, string) and callables (identity) are
   unchanged; struct identity keys stay refused (`collection-identity-key`).
   JavaScript-style identity keys are a future design question.
8. **Sound inference under aliasing.** Collection contents are tracked per
   allocation site (each `map()`/`set()`/`stack()` call or field
   initialiser), refined per string-literal map key, never per binding;
   struct field types are per struct type; a struct receiver is admitted only
   when the inference proves exactly one struct type. A collection of
   unknown origin, or a persistent session, reads as "anything".
9. **Arrays and objects: the forward-compatibility guard (D2).** Until Phase
   B, the VM refuses every unit whose output could differ between value and
   reference semantics for arrays and objects
   (`reference-guard:<route>`, `rt/reference_guard.h`): a mutated array or
   object must be exclusive (never copied out, its parts never copied out
   when mutated below the top, every inflow fresh; the `x = f(x)` idiom
   allowed). The six D-040 witnesses become refusals. Accepted programs keep
   their output when Phase B lands.
10. **Legacy (D8).** `NIFT_VM=0` is unchanged. Every VM difference is an
    approved difference (`compatibility.md`, cp25 classification).
11. **Certification findings (D9).** All rt-lint findings fixed. The
    checked-build relocation (`aggregate_store.h`) still moves storage of at
    most 256 elements on every mutation (every stale-reference control is
    small and is caught at its next use) and larger storage once every
    size/256 mutations, so the 200,000-element stress fixtures stay linear
    under sanitizers and keep their full size and budget coverage.

Migration (`compatibility.md`): to keep an independent value use `copy()`
(shallow) or `deepcopy()`; use `same()` to test sharing; a function that fills
a collection passed to it now fills the caller's (`m = fill(m)` keeps
working); build self-referential structures from ids into an owning
collection.

Witnesses: `tests/runtime2/refsem/` (`make test-runtime2-refsem`, fifteen
mutation controls `test-runtime2-refsem-mutants`); the D-063 fixtures that
became legacy-equal moved to `collections/parity/`, their value-semantics
goldens kept in `collections/superseded/` (`INDEX.tsv`).

**Review round 1 (independent review of `ad9d842f`, 2026-11-01).** The
review rejected the candidate; these rulings amend 4, 6, 7, 9 and 11 above
(the text above is kept as first written).

- **9 amended: a whole-program guard.** The first guard reasoned per site
  and missed mutations that run while a binding is lent (a callee mutating a
  global its parameter aliases, a closure passed along with the binding, a
  module function, a later session statement) and closures that capture a
  parameter. It is replaced by a rule over the whole linked unit (modules are
  linked into one unit, `module_link.h`): for every array/object binding that
  is mutated in place anywhere (every binding, in a session),
  (a) the exclusivity rules above (inflow, contents, retention) still hold,
  with retention widened: a parameter any closure captures escapes, an
  argument to an unresolved or callable callee is retained, a native given a
  callable value that is not a lambda literal retains its operands' parts;
  (b) **lent** (new route `reference-guard:lent`): while the binding's value
  or a part of it is lent (to a call's parameter, a native and its
  callbacks, a `for` loop), no mutation of it may be written inside that call
  or loop (outside the lent expression itself), and if the binding is
  referenced from any other body (function, method, lambda; a session's
  later statements) the call or loop may not run user code;
  (c) a loop binding or a native callback's parameter is a part of its
  operand; a closure capturing it retains that part; a loop over a fresh
  iterable (literal, constructing call) binds an exclusive value;
  (d) a user function used as a value (or any function in a session) may not
  mutate its parameters; aggregates held by struct instances are unknown
  roots. Proof sketch (`checkpoint` §5): in the copy model and in Phase B
  the outputs differ only if an object is mutated through one holder while a
  second holder observes it; (a) excludes every second holder outliving an
  operation, (b) every mutation during a lending, whose holders end with it.
- **6 amended (D5 for arrays).** Array (and object) iteration is the
  copy-on-write value in Phase A; an array mutated while its loop runs is
  refused by (b), since Phase B would iterate the live array. Array snapshot
  iteration as a reference rule lands with Phase B.
- **4 amended.** A cycle closed by a store into a captured cell (closure
  capture, upvalue) raises the catchable `runtime.reference_cycle` too; the
  cell, the stored value, reference counts and heap accounting are left
  unchanged.
- **7 amended.** An object as a map key or set member is refused under its
  own reason, `collection-object-key` (it was reported as a struct or handle).
- **11 amended (relocation).** Larger storage also moves on the first
  mutation after a reference or pointer into it was handed out (a header bit
  set by the reference-returning accessors), so "take a reference, mutate
  once, use it" is caught at every size; these moves draw on a per-thread
  credit refilled by every mutation, so the moved volume stays linear.
- **Cost (owner guidance of 2026-11-01: the guard is a temporary internal
  measure).** CP25-R corpora (589 shared units, 608 execute entries): no
  refusal beyond the four the first guard already made. `tests/` (1,757
  `.f`): the new refusals are the loop-mutation and aliasing probes
  (`for-s2`, `cp12c-probes`, `capture_origins`, `closures-s1`). Three common
  shapes are newly refused although Phase B would print the same: an in-place
  update of the array a `for` iterates (`for (x : a) { a[i] = x * 2 }`), a
  write through the loop binding (`for (row : grid) { row.push(0) }`, which
  already writes through on the VM and in legacy), and a global array that
  helper functions mutate, passed to a function that only reads it. Admitting
  them needs index reasoning, a call graph and a place model of loop
  bindings; the guard
  is not grown further pending the owner's choice (keep it, narrow arrays and
  objects further, or bring Phase B forward).

**Review round 2 (independent review of `964901ae`, 2026-11-01; the owner's
last guard round).** Amends round 1 above (kept as written).

- **Guard: collection iteration.** A `for` loop over a map, set or stack
  (or a `copy`/`deepcopy` of one, or a call returning one) binds elements of
  an unknown holder: the collection's elements alias every other holder in
  Phase B. The first guard treated such a binding as exclusive (`fresh`
  short-circuits on a non-aggregate kind). Mutating it is refused
  (`reference-guard:collection-element`); retaining, capturing or lending
  it counts as an unknown holder; extracting from it is a non-fresh inflow.
  Only literals and constructing calls bind exclusive values (as round 1
  stated). In a session, only top-level bindings are treated as mutated by
  later statements (a function's or lambda's locals are reached only by the
  unit's own code), and a session recompile's capture initializer (the
  existing captured cell's value) is not a new inflow.
- **Cycles (4).** A store into a captured array by insert, splice, an
  expression push or an element store moved the array out of its cell before
  the cycle check, so a caught refusal left the cell empty. The check now
  runs before the value leaves the cell (`CellTake` carries the stored
  value's register); the graph, reference counts and heap accounting stay
  unchanged, and later reads, mutations and cleanup behave.
- **Relocation (11).** Internal transient reads (VM element reads, sizes,
  iteration) use non-lending views; `array_mut` no longer counts its own
  mutation as a hand-out. Guarantee, exactly: a mutation of storage above 256
  elements moves it when a reference or pointer was handed out since its last
  move and the thread's relocation credit (2^21 elements to start, plus 512
  per mutation) covers its size; otherwise once every size/256 + 1 mutations.
  Ordinary VM reads no longer spend the credit.
- **Sessions (a Phase A defect, review of the Linode run).** The session
  symbol table stored kinds in 16-bit fields; `kKindStruct` (bit 23) and the
  collection kinds were truncated, so an instance (or collection) binding came
  back as a string in the next statement and was refused. The fields are
  32-bit now; instance and collection bindings work across statements
  (legacy-equal). An instance or collection with no identity-bearing content
  has no callable shape (it made struct schema replacement refuse).

**Review round 3 (independent review of `22685af2`, 2026-11-01; owner
addendum: a bounded correction, no guard changes).** Amends rounds 1 and 2
above (kept as written).

- **Cycles (4): validate the whole mutation, then commit.** A splice that
  removes elements (count > 0) on a captured array stored the removal into
  the cell before the replacement was evaluated and checked, so a refused
  splice still removed elements (the review's repro: `[1,2,3]` became `[3]`).
  The splice now removes and inserts on a copy and stores once: the store
  checks the complete result (every replacement, including the last) against
  the receiver before the cell changes, so a refused splice leaves the
  receiver and its reachable graph byte-for-byte unchanged, in statement,
  expression, closure and captured-cell forms. The other array mutators
  (insert, push, element store) check the stored value before the take (round
  2); map set, stack push and struct field stores check before the store. A
  global array is read by name from closures, so storing into it closes no
  cycle. Mutant R22 (the removal stored first) is caught.
- **Presentation on mixed struct receivers.** `stringify()`, `prettify()` and
  `highlight()` on a value inferred to be a struct instance or another kind
  (an array, map, set, stack or scalar) were admitted and stopped after
  output with an internal failure (exit 70). They are refused at compile time
  before any effect (`presentation-mixed-struct:<method>`), wherever the call
  sits (top level, function, closure, imported module). This is dynamic
  dispatch of presentation methods over mixed struct/collection receivers:
  a candidate for Phase B or a later VM capability checkpoint, not a
  permanent limitation. Exact collection receivers, collection/array mixes and
  unconstrained values (`env()`, decoded JSON) keep the generic path.
- **Presentation of containers holding struct instances (coordinator).**
  Phase A admits instances inside containers, and the VM serializer cannot
  render one (legacy: `[C{v:0}]`, private fields skipped; privacy is not on
  the run-time instance), so `[C()].stringify()` stopped after output with an
  internal failure. In a unit or session that declares a struct, a
  presentation method on a value that may be a container is refused at
  compile time (`presentation-struct-contents:<method>`); conservative, a
  Phase B / VM capability candidate. An audit of every content-walking
  operation found no other crashing path (checkpoint section 15).
- **Phase B requirements (recorded, not implemented).** Every
  cycle-capable mutation of arrays and objects goes through one shared
  protocol: validate the complete proposed mutation, commit it, then update
  bookkeeping. Phase B separates static admission from runtime dispatch, so a
  valid program is not refused only because inference is imprecise.

**Review round 4 (independent review of `2640ab48`, 2026-11-01; F1-F3
owner-approved; no guard changes).** Amends round 3 above (kept as written).

- **Presentation of struct instances (F1).** Round 3 refused only receivers
  inferred to be a struct *and* something else, so an exactly-struct receiver
  that is not a plain name (`C().stringify()`, `a[0]`, `m.get(k)`,
  `st.pop()`, a call result, a loop element, a session binding) reached the
  serializer and stopped after output with an internal failure. In a unit or
  session that declares a struct, a presentation method on a value whose
  inferred kinds include a struct instance is refused at compile time
  (`presentation-struct:<method>`, renamed from `presentation-mixed-struct`),
  decided from the receiver's kinds whatever the expression; containers keep
  `presentation-struct-contents`. Compatibility gaps to eliminate (Phase B /
  VM struct rendering).
- **Splice argument order (F2, coordinator ruling under the owner's
  correctness-over-legacy policy, owner-approved; the owner may reverse it).**
  Every splice evaluates the receiver and all arguments, validates bounds,
  the replacement and cycles, prepares the result without touching the
  original and commits once; on failure the array is unchanged (captured,
  global or local alike). Argument side effects are not rolled back.
  Legacy's remove-before-evaluate order is an approved difference. No other
  mutator evaluates an argument after a partial mutation (audit in the
  checkpoint, section 16). Mutant R23 (only captured splices evaluate first)
  is caught.
- **Known over-refusal (F3).** The struct-unit container rule refuses
  containers that hold no instance (the nift-packages/http `capabilities()`
  shape); a temporary Phase A limitation Phase B must eliminate, the program
  and its valid output preserved as `tests/runtime2/refsem/pending/`.
- **Phase B (recorded only).** Operation admission from the semantic receiver
  and argument types through one operation-level admission interface, not
  per syntactic form.
- **Pre-existing, reported.** Presenting a `mutex()` or `channel()` value
  (alone or in a container) stops after output with an internal failure on
  weekend/r2 as well; not part of this change.

## D-065 — Named release deferrals (owner, 2026-11-10 13:26)

The owner approved these deferrals by name for the initial Runtime 2.0 release. Each one is a VM
limitation, not a removed feature. Under the VM default (`NIFT_VM` unset), a program that uses one
is refused before any part of it runs, with `not compiled for the VM (<reason>)`, and is never
silently run on legacy. The legacy interpreter (`NIFT_VM=0`) keeps running these constructs as
before. `make test-runtime2-named-deferrals` (tests/runtime2/named_deferrals.py) pins every
refusal and the legacy channel.

1. **Delayed imports inside functions** (`fn(f()) { import("./m.f"); … }`, reason
   `import-nested`). Top-level, relative, chained and package imports run on the VM. Only an import
   executed inside a function body is deferred. It needs run-time module loading with D-041's
   initialise-once identity and package ownership checks.
2. **`run()` and `cmd()`** (process execution: `callable:run`, `callable:cmd`, and their handle
   methods such as `method:run` and `method:pipe`). This keeps the earlier release-scope decision
   that b5-refsem.md cites. That decision had no decisions.md entry until now. D-029, D-030 and
   D-034 continue to define the legacy behaviour.
3. **VM-native FFI** (`ffi_open` and the FFI calls: `callable:ffi_open`). FFI keeps working on
   legacy and on non-VM paths (docs/ffi.md, D-036).
4. **VM-native timers** (`timer()` and its handle methods: `callable:timer`, `method:start`,
   `method:stop`, `method:elapsed`, …). Timers keep working on legacy.
5. **`project_root()`** (`callable:project_root`), with CP24 under D-053. Project builds keep
   using the retained renderer and legacy scripting.

They are recorded in the CP25-R inventory (tests/runtime2/cp25/classification.tsv, status
`deferred-named`) and in docs/runtime-2/known-limitations.md.

## D-066 — The assignment family's evaluation order (owner, 2026-11-10 16:35; checkpoint 1)

The owner chose option C of combined-b3-b5.md section 94 for `o["k"] = v`, `g[i][j] = v` and
`a[i] op= v` on the VM. H-A and D2 are the contracts; the 13:26 "value-before-key quirk" wording
was wrong and is withdrawn.

1. **A store** (`a[i] = v`, `o["k"] = v`, `g[i][j] = v`): the receiver is evaluated and
   retained, then the value, then the keys in their established order, then the store is
   validated and committed. The existing `a[i] = v` does not change. This is legacy's order.
2. **A compound update** (`a[i] op= v`, `o["k"] op= v`, nested): the target is resolved and its
   value read before the right-hand side, then the right-hand side, then the operation and the
   commit under the mutation contract, into the container and at the key read first. The target
   is never looked up again (D2). Legacy reads at the old key and writes at the new one: an
   approved difference.
3. The cycle refusals of these stores (B5, `runtime.reference_cycle`) and loop-variable writes
   that change the iterated array (B5) are approved differences under the existing decisions.

Recorded in compatibility.md ("The assignment family") and known-limitations.md; tests:
`tests/runtime2/assign_family.py`, mutants `tests/runtime2/assign_family_mutants.py`.

## D-067 — Rescan rulings: positions, deferrals, required features, dead code (owner, 2026-11-11 02:53)

The owner's rulings on the post-checkpoint-1 rescan (docs/runtime-2/checkpoints/rescan-post-cp1.md, C111-C116).
Each item is cited in the CP25-R inventory as "D-067 #n (owner 2026-11-11 02:53)". Checkpoint 1 is not reopened.

1. **Diagnostic positions.** The 7 refusals that report a position other than legacy's (`context:struct-field`,
   `coro-module-before-import`, `fn-called-before-declaration`, `for-missing-write`, `string-arg-type`,
   `import-opaque:legacy-runtime-error`, `import-limit`) are fixed to legacy's position (status `implement`). The 10
   "more precise" positions (`builtin-arity:deepcopy`, `coro-future-flow:ForBinding`, `coro-future-flow:Throw`,
   `coro-module-private-async`, `interpolation:non-scalar`, `method:nope`, `non-transferable`,
   `static-error:push-arity`, `type:member`, `type:script-return`) fall under one common diagnostic decision: a VM
   diagnostic may point at the actual offending construct when legacy points at an enclosing statement. They become
   approved only after an independent review verifies, for each, that the VM position identifies the actual offending
   construct; until then they are "approved-pending-review" and stay PENDING-OWNER.
2. **Deferred from the initial release** (status `deferred-named`, each with its refusal row and a
   known-limitations entry):
   a. the three assignment forms excluded from checkpoint 1: `a[0]++` (`incdec-target`), a compound assignment used as
      a value (`expression:assignment-target`), a future stored through a nested path (`coro-future-flow:Assign`);
   b. the three session-only refusals (`builtin-receiver:stringify`, `presentation-struct-method:*`, `session:opaque`);
      their tests stay; an unsafe behaviour in a session would be a separate defect;
   c. legacy dynamic scope (`free-name`, `free-name:dynamic-caller`; lexical scoping is the target, D-038);
   d. the CP24 project and template APIs, under the existing deferral D-053 (and D-065 #5);
   e. dynamic imports (`import-dynamic`). D-037 approves rejecting unsupported dynamic imports before execution; this
      item records their deferral from the initial release (D-037 is not broadened).
3. **`@fn` (amendment to D-047 #1 and D-048 #1).** The VM refuses plain `@fn(...)` in script context while the legacy
   interpreter keeps accepting it; there is no legacy-side change. This VM-only refusal is the approved behaviour
   (`fn-flags`, approved).
4. **Import depth.** An import chain 33 to 63 modules deep runs on the VM, where legacy fails at depth 33 ("maximum
   template parse depth exceeded"): approved, subject to cycle and resource safety. The VM refuses depth 64
   (`import-limit`, kMaxModuleDepth, module_graph.h:122): a deliberate resource bound of the recursive module-graph walk
   (rescan-post-cp1.md section 11a). Pinned by tests/runtime2/import_depth.py.
5. **Not ratified.** Async self-recursion (`qualified:legacy-await-chain-defect`) and an import reached by two paths
   (`import-reinitialised`) stay PENDING-OWNER, under investigation.
6. **Sort-comparator portability** stays a mandatory Linode verification item.
7. **Required for the initial release** (status `implement`, with a sequence slot): postfix spellings; nested function
   declarations; documented redeclaration behaviour; `?.` / `?[]`; binding-only const objects;
   variadics/spread/destructuring (CP3); `export`; ordinary struct functionality (CP2: declaration model and
   construction in loops; CP2b: struct identity); and the previously approved bytes, `warn`/`err`, read-only path and
   small-capability items ("remaining approved features").
8. **Host-import guard.** The 27 `module-host-op:*` refusals are implement items (sequence slot 2), with the bounded
   repair of rescan-post-cp1.md section 11 as the starting design.
9. **Investigate first** (stay PENDING-OWNER, direction recorded per family): irregular comparisons, interpolation of
   unproven kinds, loop-context built-ins, futures in ordinary expressions, capacity limits, and the remaining singles.
10. **Family grouping** is approved for the uniform builtin-arity, builtin-receiver, unknown-method and
    unknown-callable rows, evidence-preserving: each family row states its contract; the per-program rows stay, with
    their evidence, marked "member of <family>"; exceptions with a distinct category or semantics stay separate. No row
    is deleted.
11. **Dead code (correction; Option A of rescan-post-cp1.md 13.1 is withdrawn).** The VM rejects unconditionally invalid
    code even when it is unreachable: malformed syntax, malformed declarations, invalid assignment targets, and
    constructs the language contract defines as compile-time errors. It does not reject a program because an
    unreachable branch contains an execution-dependent error (an undefined name, an unavailable platform-specific
    operation, a run-time type error, an unsupported host op): such an error is raised only if execution reaches it
    (`if (false) { nope() }` / `print("success")` prints success). An unsupported VM feature in an unreachable branch
    should ideally not refuse the program; if reached, it is an ordinary error, never a silent legacy fallback.
    Correct platform-conditional execution is a Runtime 2.0 requirement. The 00:29 early-refusal policy (D-013)
    applies to genuinely static errors only. The 321 class (ii) rows are reclassified under this rule
    (docs/runtime-2/checkpoints/dead-code-admission.md).

## D-068 — Rulings on the dead-code plan, the host guard and the slot order (owner, 2026-11-11 05:41)

The owner's rulings on the 11 decisions of docs/runtime-2/checkpoints/dead-code-admission.md (C118-C121, final
review REVIEW-C120) and rescan-post-cp1.md. Each item is cited in the CP25-R inventory as "D-068 #n (owner
2026-11-11 05:41)". Slot 1 is authorised to start. Items 1-11 are the 11 decisions; item 12 records the slot-1 scope.

1. **Dead-code plan** (dead-code-admission.md at c15807c0): approved as **slot 3**, after the host guard. The
   allow-listed rows are marked with their slot. The implementation preserves all six conditions of the soundness
   review (dead-code-admission.md 5.7a); it stops for an owner decision if it needs substantial new compiler
   architecture; the name scan covers struct field names explicitly (5.2, C121).
2. **Host-import guard, Option A** (dead-code-admission.md 5.5, rescan-post-cp1.md section 11): approved as **slot 2**,
   about 150-220 lines of src, as reviewed: a leading import block in the entry; call-free, host-free module
   initialisation (with `import`/`export` exempt and non-host, callback-free natives allowed), checked transitively;
   **a permanent explicit classification of every built-in** as host or pure; **unknown effects are refused**; negative
   controls for host effects, callbacks and transitive imports.
3. **Undefined-call deferral under full name certainty** (5.2): approved for slot 3, with the distinction between the
   11 built-ins a binding of their name shadows in legacy and the 50 where the built-in wins kept. Nift's actual
   name-resolution rules are preserved; the complete name scan and its negative controls are mandatory.
4. **Sessions stay conservative in this checkpoint:** session units do not defer. This is a scope boundary of the
   checkpoint, not a permanent rule. The start-up file of `nift -i script.f` must be classified correctly as a session
   unit.
5. **The 10 S constructs are rejected even when unreachable** (a deliberate departure from legacy). `jump-outside-loop`
   is S: `break`/`continue` with no lexically enclosing loop is a static error; a `break` inside a valid loop that never
   runs is fine. S is not extended to run-time- or platform-dependent validity.
6. **Uncertain types: conservative refusal.** The 19 mixed rows stay class (i) capability gaps; they are never narrowed
   by the dead-code plan.
7. **Positions of the five mixed rows** (`coro-future-flow:ForBinding`, `coro-future-flow:Throw`,
   `interpolation:non-scalar`, `type:member`, `type:script-return`): the precise positions are approved for their
   certain-type cases under D-067 #1, with positions and categories pinned. Their statuses stay PENDING-OWNER because of
   the union gap. A change of position caused by unrelated compiler transformations remains a defect.
8. **Ordinary-language coverage is slot 7:** `postfix-spelling`, `statement:Export`, `statement:FnDecl` (with
   `fn-redeclared` under "documented redeclaration behaviour") and `declare-const`. The checkpoint has four
   independently reviewed sub-changes and stops if nested functions need broad closure restructuring.
9. **Async self-recursion:** the VM meaning is conditionally approved. A separate safety checkpoint addresses the
   task-memory defect; the CP23 qualification (`qualified:legacy-await-chain-defect`) stays open, and the row stays
   PENDING-OWNER, pending that checkpoint.
10. **`import-reinitialised`:** initialise-once per canonical module identity is the intended semantics; legacy does
    not change during this campaign. Recorded as an amendment to D-041 #2 (the VM's eventual initialise-once is an
    intentional difference; the removal condition is VM-only). A bounded VM correction is still to be investigated;
    the row stays PENDING-OWNER. Its tests must cover diamond imports, relative-path aliases of one module, repeated
    imports, initialisation side effects and cycles; different files must not be conflated.
11. **Slot order:** 1 infrastructure (inventory-infrastructure corrections); 2 host guard; 3 dead-code admission;
    4 CP2; 5 CP2b; 6 big-int conditions, destructuring, variadics; 7 ordinary-language coverage; 8 remaining bounded
    capabilities; then the async-memory safety checkpoint; then certification.
12. **Slot 1 scope** (inventory-infrastructure corrections; authorised to start now). User-facing diagnostics are
    preserved; classification completeness has negative controls; slot 2 does not begin until slot 1 is accepted. The
    concrete scope, as approved (sources: #9 of the owner's 05:41 attachment and the integrator brief that relayed it):
    a. the inventory tool's compile settings match real script runs (`cooperative_switches` in
       refusal_inventory.cpp, as script_runner.cpp does), with the F12 rows that vanish retired, `method:join`'s
       remaining sightings classified, and a mutant that reverts the limits;
    b. the multiplication refusal `type:*` renamed to an unambiguous id (`type:mul`), `type:*` removed as a family
       row (the family grouping covers only builtin-arity, builtin-receiver, method and callable), every `type:<op>`
       reason with its own exact row and an unlisted one failing G1;
    c. the two message-shaped reasons (`async(...)`/`await(...)` "was removed") replaced by stable ids
       (`removed-builtin:async`, `removed-builtin:await`).
    **Not approved:** renaming `non-transferable`, and the nesting-emitter rename (parser and compiler nesting limits).

**Text completion (C124).** C122 recorded these rulings from the owner's 05:41 attachment (1120cc1e) without some of its
text; the slot-1 review (REVIEW-C122-C123-SLOT1, finding 8) listed the omissions, which C124 adds above: in #1 the six
review conditions, the architecture stop rule and the struct-field-name requirement; in #3 name-resolution
preservation and the mandatory name scan and controls; in #7 the unrelated-transformation rule; in #10 the
import-identity tests; and #12, the slot-1 scope. The owner's attachment numbers its items differently from this
record (its #9 is the slot-1 scope and its #11 the `import-reinitialised` item); the tsv cites this record's
numbering.

## D-069 — Slot 1 accepted; status vocabulary; three findings (owner, 2026-11-11 11:07, attachment aa18728e)

The owner's ruling on the slot-1 freeze (4ff6c100, nift sha256 `78027d95...e1b495`). Each item is cited in the CP25-R
inventory as "D-069 #n (owner 2026-11-11 11:07, attachment aa18728e)". Quotations are from the attachment
(`aa18728e-attachment.txt`, its decision table, its detailed sections and its "Forwardable ruling"); C126 replaced
C125's paraphrase with them (review of C125, R3).

1. **Accept slot 1.** "ACCEPT, subject to correcting the inventory record." "Accept Slot 1 at commit `4ff6c100`.
   Production binary SHA-256: `78027d95…e1b495`. The independently reviewed production changes and C124
   test/documentation repairs are accepted. One inventory correction remains mandatory before Slot 2 starts. Do not
   reopen the production implementation."
2. **`builtin-arity:join`.** "YES — session-only, pending." "Restore this row from retired to pending, class E2. The
   refusal is reachable during ordinary `nift -i` execution, while the equivalent VM script succeeds. Preserve the
   reproducer and legacy comparison. Do not automatically apply the existing session-presentation deferrals. This is
   a separate interactive thread-operation finding." ("This is a different capability: thread joining and argument
   handling in interactive sessions.")
3. **Cooperative threads.** "YES — retain the eight retirements." "Confirm `NIFT_VM_COOPERATIVE_THREADS` as a test-only
   switch, not a supported public runtime setting. The eight rows that appear exclusively under this switch may
   remain retired from the ordinary capability inventory. Retain their complete evidence and coverage in relevant
   test-mode gates." "However, they should retain their evidence and remain covered by the applicable test-mode
   gates. **Retired from user-facing capability decisions does not mean exempt from testing.** If tests using that
   switch fail, they must still be reported accurately."
4. **Inventory implementation statuses.** "YES, where implementation was explicitly authorised." "Change rows
   explicitly approved for implementation from PENDING to IMPLEMENT. Distinguish approved-but-not-implemented,
   implemented-awaiting-review and accepted. Do not change deferrals or genuinely pending decisions. Preserve the
   owner-decision references and checkpoint assignments." The five statuses to keep separate: "Approved for
   implementation, not yet implemented. Implemented, awaiting review. Implemented and accepted. Approved deferral.
   Pending owner decision." "Do not mark a capability implemented merely because its checkpoint has been scheduled."
   *Applied (C125, corrected in C126):* only rows still PENDING-OWNER whose implementation an owner ruling explicitly
   authorises changed: the slot-3 dead-code members (D-068 #1, #3) and the 7 L-defer callables (dead-code deferral
   only). The D-067 #7 required features and the slot-2, slot-6 and slot-7 rows were already `implement` before C125
   (C117, C122) and did not change. The family rows `method:*` and `callable:*` stay PENDING-OWNER (C126).
5. **Historical refusal identifiers.** "NO — preserve historical evidence." "Do not modify frozen evidence containing
   `type:*`. Retain historical checksums and original text. Add a documented mapping from the historical
   multiplication refusal `type:*` to the current `type:mul`. Use the current name in present-day documentation and
   tests."
6. **Struct-to-async transfer.** "YES — new pending compatibility/safety finding." "Add a new pending inventory and
   release-audit finding. The VM accepts a struct transfer that legacy rejects as non-transferable. Do not approve
   this difference yet. Investigate ownership, copying/sharing, mutable fields, opaque handles, aliases, concurrent
   access and resource lifetime. Determine whether this is an intentional safe semantic improvement or an unsound
   transfer. Escalate any demonstrated safety defect. Do not alter production semantics without a separate
   decision." The review is to establish: "Whether the struct is copied, shared or transferred. Whether its fields
   can contain mutable references or opaque handles. Whether the receiving task can observe or mutate data owned by
   the caller. Whether the operation respects B2/B5 mutation and ownership contracts. Whether the behaviour is
   deterministic under concurrent execution." "**Do not treat successful execution as proof of safety.**" "This
   investigation can proceed independently of Slot 2, but any unsafe transfer finding must be escalated."

**Governance (the attachment's "Next").** "Slot 1 is accepted; Slot 2 (the reviewed host-import guard) may start after
the required bookkeeping correction. Maintain the sequential implementation, freeze, independent-review and
owner-acceptance workflow. Outstanding Linode and cross-platform gates remain mandatory. No integration, push, tag or
release without explicit approval." Slot 1 production is not reopened.

Mapping onto the inventory vocabulary (tests/runtime2/cp25/inventory.py): approved-not-implemented = `implement`;
implemented-awaiting-review = `implemented-awaiting-review`; implemented-accepted = `implemented-accepted`; approved
deferral = `deferred-named`; pending owner = `PENDING-OWNER` (`approved` and `retired` keep their meanings).

## D-070 — Static-error rows approved; struct-to-async closed under D-NEXT-refsem #5 (owner, 2026-11-11 11:49, attachments 1d728bb0 and 0a4e481b)

Each item is cited in the CP25-R inventory as "D-070 #n (owner 2026-11-11 11:49)". Quotations are from the two
attachments (0a4e481b: "Both decisions confirmed"; 1d728bb0: "Struct-to-async: existing approval confirmed").

1. **Ten statically invalid constructs (the S rows): approved.** "APPROVE changing all ten rows to `approved`, citing
   D-068 and the 05:41 owner ruling. These are inherently invalid constructs that may be rejected before execution,
   even when unreachable. This approval does not cover execution-dependent errors or valid operations that the VM
   cannot yet compile. Preserve the distinction established for Slot 3 dead-code admission." The example of an S
   construct is `if (false) { break }` ("A `break` outside a lexically enclosing loop is invalid even when
   unreachable"); the example outside it is `if (false) { nope() }` / `print("success")`, which "must remain eligible
   for deferred error handling under the approved Slot 3 design. The inventory should distinguish these categories
   explicitly."
   **Note (owner 2026-11-11 14:23, attachment bbd6a7a0; C127 accepted).** All ten rows have semantic approval under
   D-068. Eight are `approved`. Two keep `implement`: `number-literal` ("diagnostic wording work remains") and
   `import-opaque:legacy-runtime-error` ("error-position work remains"). They "retain their implementation status
   because their diagnostic corrections are incomplete, not because their underlying early-refusal semantics are
   unapproved." "Do not mark unfinished work complete merely to match the literal wording of the earlier approval."
2. **Struct-to-async transfer: already approved.** "ALREADY APPROVED under D-NEXT-refsem #5, dated 2026-11-01. Close
   the inventory row as an existing approved semantic difference. Do not create a new approval. Retain the
   independent investigation and compatibility evidence. The recursive transfer-safety requirements for future
   struct field types remain mandatory during the struct checkpoints." (1d728bb0: "Close the new inventory row as
   ALREADY APPROVED, citing D-NEXT-refsem #5 and the existing compatibility record. Retain the investigation
   evidence. Do not create a new semantic exception or modify production code. Preserve the existing approval's
   scope." "This is not a new exception or an expansion of the previous approval.")
3. **Checkpoint 2 transfer requirements (mandatory).** "When struct fields gain support for additional value types,
   verify transfer safety recursively. Tests must cover structs containing: File and stream handles. Mutexes.
   Mutable collections. Lambdas and captured references. Other nested structs. Test both `async()` and `thread()`
   where applicable. A non-transferable nested value must not become transferable merely because it is wrapped in a
   struct. Failed transfers must not partially publish or partially transfer the object. Preserve the established
   B2/B5 ownership and reference semantics. The expanded field-type support must not silently broaden D-NEXT-refsem
   #5 to unsafe transfers." ("A failed nested transfer must not partially publish the struct or transfer some fields
   before discovering a prohibited handle. The failure should be atomic with respect to the transfer operation."
   "Also preserve the approved distinction between copying values across task boundaries and sharing mutable
   references within an execution context.")
4. **Slot 2.** "Continue the authorised host-import guard implementation. Ensure built-ins that can invoke user code
   indirectly are not classified as pure. Preserve the reviewed read-order guarantees, explicit built-in
   classification and effective negative controls. No additional production scope is authorised." ("The guard must
   classify that operation according to its possible effects, not merely its name. The existing tests for shadowed
   built-ins and indirect user calls should establish this.")
5. **Workflow.** "Apply the two inventory status corrections as records-only work. Continue Slot 2 through freeze,
   independent review and owner acceptance before starting Slot 3. No integration, push, tag or release without
   explicit approval."

## D-071 — Slot 2 accepted; host-deps, the seven restricted host operations, the handle mutant; slot 3 authorised (owner, 2026-11-11 17:11, attachment 29e9426f)

Each item is cited in the CP25-R inventory as "D-071 #n (owner 2026-11-11 17:11)". Quotations are from attachment
29e9426f ("SLOT 2: ACCEPTED WITH RECORDED QUALIFICATIONS"), section "Forwardable decisions for Claude", unless marked
otherwise. The review is REVIEW-C129-SLOT2 (verdict ACCEPT).

0. **Slot 2: ACCEPTED.** "Accept Slot 2 at commit `d79d556a`. Production binary SHA-256: `fff2376c…73bf2ec`. The
   independently reviewed host-import guard is accepted. This approval covers the frozen production implementation
   and reviewed test changes. It does not authorise integration, push, tag or release."
1. **Host-dependency suite.** "Approve checkpoint acceptance despite the 24 known unrelated refusals. The failing
   programs contain no imports, and the Slot 2 guard cannot apply to them. Preserve the 24 failures under their
   existing capability groups. Do not classify the host-dependency suite as PASS or treat the failures as approved
   release deferrals. Record that fresh baseline reproduction was unavailable because the Slot 1 binary was not
   present. These failures remain subject to final release qualification." (Section 1: "**Do not mark the
   host-dependency suite PASS.**" "The affected cases must remain on the consolidated certification inventory.")
2. **Seven restricted host operations.** "Approve the existing module read-order restrictions for: `cp`, `remove`,
   `cat`, `file`, `getenv`, `is_dir`, `exists`. The approval applies to unsafe execution before or during module
   loading. It does not approve refusing those operations in contexts where Slot 2 now safely supports them. Record
   the unsafe-context restriction as approved and the reviewed safe execution capability as implemented and
   accepted. Split inventory rows or use context-specific dispositions where necessary. Do not conceal unrelated
   refusals behind these approvals." (Section 2: "**Do not use a blanket `approved` status that could conceal a new
   refusal in an otherwise safe context.**" "In an unreachable branch: Governed by the separate Slot 3 dead-code
   contract.")
3. **File-handle mutation control.** "Accept the repaired negative control for checkpoint purposes. Retain a Low
   test-quality finding because the mutant is detected by a 120-second deadlock timeout rather than a specific
   assertion. Before release, verify deterministic timeout behaviour, reliable completion of the unmodified build
   and causal attribution to the mutant. Improve the diagnostic if practical, without starting another broad harness
   campaign."
4. **Slot 3: AUTHORISED.** "Begin the previously approved dead-code admission checkpoint using the independently
   reviewed plan at `c15807c0`. Preserve its six review conditions, full name-resolution checks, struct-field
   callable handling, built-in shadowing rules and conservative behaviour for uncertain types. Execution-dependent
   errors in unreachable code must not trigger whole-program refusal where the approved analysis establishes safe
   deferral. Inherently static errors remain early errors. Interactive sessions retain their existing admission
   policy for this checkpoint. Use effective mutation controls and precise regression tests. Stop if implementation
   requires broad compiler restructuring or violates the approved soundness assumptions."
5. **Verification and governance.** "Freeze and independently review Slot 3 before seeking owner acceptance.
   Preserve the outstanding Linode, Windows/macOS and v4.12.0 performance requirements. No integration, push, tag or
   release without explicit owner approval."

## D-072 — The cat output order (P1), the file trace order (P2), the host guard for all 27 operations (owner, 2026-11-11 18:00, attachment e3c9cee8)

Each item is cited in the CP25-R inventory as "D-072 #n (owner 2026-11-11 18:00)". Quotations are from attachment
e3c9cee8, section "Forwardable owner decisions", unless marked otherwise.

1. **`cat` output ordering: P1 RELEASE BLOCKER.** "Classify the VM's incorrect `print`/`cat` output ordering as a
   genuine correctness defect. Do not approve it as a semantic difference. Schedule a separate, bounded production
   repair checkpoint immediately after Slot 3 is accepted. Do not interrupt or expand the current Slot 3
   implementation. The required invariant is that output operations targeting the same logical stream preserve program
   execution order. Investigate the buffering mechanism and other direct-output host operations. A bounded
   flush-before-direct-write correction is acceptable if independently verified. Do not disable VM output buffering
   globally. Require exact-byte legacy comparisons for piped and file-redirected output, mixed output operations,
   functions, loops, imports and failure paths. Include an effective deliberately broken build that restores the
   original ordering defect. Preserve existing performance thresholds; no re-baselining." (Section 1 also requires
   tests for "Multiple consecutive `cat` calls" and "Error paths where buffered output must still be preserved", and
   "I'd want the implementer to verify the buffering mechanism before changing it.")
2. **File-open/read trace order: P2 INVESTIGATION.** "Record a separate P2 finding. Determine whether the discrepancy
   reflects instrumentation order or actual OS-operation order. Establish whether it changes observable file
   behaviour, errors or permissions. Do not approve the difference yet. Escalate to a correctness defect if actual
   file operations are incorrectly ordered. No production changes are authorised under this investigation."
3. **Host-import guard: ALL 27 OPERATIONS.** "Extend the previously approved context-specific module read-order
   restriction to all 27 identified host operations. This is a family-level approval of the unsafe execution context,
   not blanket approval of refusing these operations everywhere. Preserve the Slot 2 effect classification: Unsafe
   execution before the last import: approved refusal. Unsafe execution during module startup: approved refusal. Safe
   execution after imports: supported and tested. Unreachable branches: governed by Slot 3's approved deferred-error
   semantics. Newly introduced host operations: require explicit effect classification. Retain the per-operation
   evidence and regression tests. Do not mark safe post-import refusals as approved."
4. **C130/C131.** "Accept the independent review of C130, subject to the queued narrow wording correction concerning
   the scratch Slot 1 rebuild and its unrecorded fingerprint. Retain C131's seven post-import regression tests. Keep
   historical evidence unchanged." (The wording correction is C132, 08c8f206.)
5. **Workflow.** "Continue Slot 3 without scope expansion. After Slot 3's independent review and owner acceptance,
   perform the bounded output-order repair. Keep the trace investigation read-only. Maintain the existing
   release-blocker inventory, performance guards and required Linode/cross-platform certification. No integration,
   push, tag or release without explicit owner approval."

## D-073 — /dev/stdout restored; worker standard streams; output-order design; L6 investigation; sorted_set; channel() (owner, 2026-11-11 22:23, attachment 5ca07c68)

Quotations are from attachment 5ca07c68, section "Forwardable decisions for Claude". Items 1-4 belong to other
workstreams and are recorded here only; C136 applies #5 and #6.

1. **Restore `/dev/stdout`: APPROVED.** "Authorise restoring `/dev/stdout` to its expected symlink targeting
   `/proc/self/fd/1`, but only inside the verified isolated work environment. [...] Verify the restored symlink and
   rerun all affected gates. Invalidate any post-damage evidence that depended on `/dev/stdout`. [...] Separately
   investigate `save()` replacing symlinks or special filesystem targets. Classify it provisionally as P1 filesystem
   safety until the actual risks are established. Do not change production filesystem semantics under that
   investigation." (The coordinator restored and verified it at 22:23; this implementer did not touch it.)
2. **Worker standard-stream handles: APPROVED** (a narrow, explicit refusal in `thread()` and `async()`; for the
   output-order checkpoint).
3. **Output-order repair: APPROVED** ("approximately 120-160 production lines and 450-550 test lines"; "Keep this
   checkpoint after Slot 3 acceptance.").
4. **L6 `native::`:** read-only investigation only ("Do not implement L6 yet.").
5. **`sorted_set`: APPROVED FOR REPAIR.** "Fix the missing legacy built-in name in the current Slot 3 tranche or a
   narrow follow-up before tranche acceptance. Preserve the established built-in precedence contract. Add regression
   and mutation controls, and audit the relevant built-in-name inventory for similar omissions. Do not broaden
   name-resolution architecture."
6. **`channel()`: APPROVED DIFFERENCE.** "Approve the existing Runtime 2.0 `channel()` built-in as an intentional
   VM-only capability. Record the compatibility difference narrowly. Do not waive the surrounding channel safety
   tests. Where legacy cannot execute the operation, use explicit VM-specific correctness expectations. Keep
   unrelated differences as failures."
7. **Slot 3 status.** "C135 remains under independent re-review. The first undefined-callables tranche is not accepted
   yet. The broader Slot 3 deferred-error work remains incomplete. Resolve the soundness findings and invalidated
   host-test evidence before seeking acceptance."
8. **Governance.** "No new production implementation checkpoint may start before the current checkpoint's required
   review and owner acceptance. [...] No integration, push, tag or release without explicit approval."

## D-075 — Slot 3 undefined-callables tranche accepted; L6 Option B approved and scheduled (owner, 2026-11-12 01:27, attachment 21eb1a83)

Quotations are from attachment 21eb1a83, section "Forwardable decisions for Claude".

1. **Slot 3 undefined-callables tranche: ACCEPTED.** "Accept C136 (`e640f808`), production binary SHA-256
   `4bb09e9e…41c0b909c4`. This acceptance covers the independently reviewed undefined-callables tranche only. It does
   not close all of Slot 3. The remaining deferred-error families remain outstanding. Approve C137 as a separately
   verified records-and-tests correction, provided the runtime binary remains unchanged and inventory coverage is
   preserved. Retain the 24 host-dependency failures as open release-gate findings."
2. **L6 `native::`: OPTION B APPROVED** for exactly `cat`, `cp`, `mv`, `rm`, `ls`, `mkdir` and `touch`: "Introduce
   their `native::` qualified forms. Remove their unqualified native built-in bindings from VM-default execution. Do
   not remove unrelated unqualified built-ins." "Function calls must never implicitly launch external executables."
   "User-defined functions with the seven names must remain possible." "Add the seven qualified calls to both VM and
   legacy. Preserve legacy's existing unqualified native functions so it remains a stable historical oracle. Record
   the VM-only removal of unqualified native bindings as an explicit Runtime 2.0 breaking change. Do not introduce
   runtime namespace objects or a general namespace system. Use bounded compile-time qualification resolving to
   existing native IDs." Qualification must not bypass module-import effect restrictions, admission and
   name-resolution rules, ownership or transfer restrictions, file read limits or memory accounting, host-call tracing
   and dependency observations, or thread/async restrictions. Migration: "Maintain a complete manifest of actual
   call-site changes. Preserve historical certification evidence." Website: "Do not clone, fetch, reset or modify the
   website checkout during the runtime implementation."
   **Schedule (not started):** "Finish the remaining Slot 3 work. Then complete and independently review: 1.
   Output-order repair. 2. `save()` filesystem-safety repair. 3. L6 `native::` migration. Proceed to the struct
   checkpoints afterward. Every production checkpoint requires its own freeze, independent review and owner
   acceptance. No integration, push, tag or release without explicit owner approval."
