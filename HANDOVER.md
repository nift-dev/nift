# Nift development handover

This is the entry point for humans and coding agents developing Nift. Read the
project `README.md` for user-facing usage; read this file before changing the
implementation. Deeper institutional context lives in `docs/handover/`.

## Authority

For current implementation behavior, source and tests are authoritative. The
standalone `nift-regression-suite` is the implementation-independent executable
contract. Current user documentation and the source branch of the Nift website
describe public usage. Git records exact history. These handovers preserve
development practice, architectural rationale, and decisions that are not cheap
to reconstruct from those sources.

When sources disagree, investigate. Do not force current code to match an old
handover statement, and do not dismiss a mismatch as stale prose without checking
whether it is a regression.

## Current identity

- Product: **Nift**, a website generator and dependency-aware website build layer.
- Current executable identity: `Nift v4.0.4` (development), following the public v4.0.3 release.
- Language/toolchain: C++17 and Make.
- Output convention for modern projects: `public/`.
- Current branch: `main` in this checkout.
- Current project phase: **hardening plateau reached** after Checkpoints 0–10.
  The bounded 4.0.2 `init` extension/platform-target feature is released and its
  native Linux/macOS/Windows Actions contract is green. The bounded v4.0.3
  language/pagination/installer programme is implemented and hardened through its
  dedicated checkpoints; current work is final release reconciliation, downstream
  distribution verification, provider dogfooding, and field evidence. The Store-built
  strict Snap candidate has been installed and validated successfully against an
  ordinary project including project-local `.nift/` state; strict confinement is now
  the settled 4.0.3 package boundary.
- Public documentation: the separate `nift-dev.github.io` repository.
- External contract: the separate `nift-regression-suite` repository.
- Embedded minifier: `minifypp/`, synchronized with standalone Minify++.
- Embedded JSON parser: `jsonic/include/json.h`, synchronized with standalone Jsonic++; `src/Json.h` is a compatibility wrapper.

Do not habitually reduce Nift to “a static site generator.” Nift generates
website artifacts, but those artifacts may contain client applications, consume
APIs, or form a frontend for a dynamic backend. “Website generator” is the
settled general term.

## The compact mental model

```text
tracked content + templates + structured build-time data
                         ↓
          dependency-aware composition
                         ↓
        optional final-output minification
                         ↓
                   ordinary files
```

Nift provides the glue without trying to become the universe. HTML, CSS,
JavaScript, React, Vite, backend services, npm, Make, and deployment tools remain
themselves. Nift owns the small build-time job it can perform precisely.

## Six things not to misunderstand

1. The modern simplification was deliberate. Do not restore LuaJIT, ExprTk,
   mutable template scripting, arbitrary shell execution, or build hooks merely
   because older Nift had them.
2. Dependencies and requirements are different. A dependency can change output
   bytes; a requirement protects a checked project relationship. Missing concrete
   paths make the referrer stale, while a currently tracked producer owns its own
   output/build state and does not transitively stale or fail pages that link to it.
3. The external regression suite is a black-box behavioral contract, not a copy
   of C++ internals.
4. Standalone Minify++ owns the independent minifier identity. Nift consumes it
   through `<minify/Minify.h>` and minification remains opt-in.
5. Standalone Jsonic++ owns the independent JSON-parser identity. Nift vendors its
   exact `include/json.h` as `jsonic/include/json.h`; parser changes originate in Jsonic++,
   synchronize into Nift, and then pass Nift JSON/schema/parser/incremental contracts.
6. Green tests are the start of confidence, not permission to stop reasoning.
   Read relevant implementation, attack assumptions, preserve reproducers, and
   test interactions.

## Repository map

- `src/`: Nift implementation.
- `tests/`: implementation-local focused and integration tests.
- `benchmarks/`: reproducible large-project performance fixtures.
- `minifypp/`: embedded standalone-style Minify++ subtree.
- `jsonic/`: vendored Jsonic++ public header used by Nift core.
- `ARCHITECTURE_RULES.md`: concise current architectural review checklist.
- `PERFORMANCE.md`: retained performance rationale and checkpoint evidence.
- `docs/PLATFORM-TARGETS.md`: current `nift init` extension/hosting-target contract and provider ownership boundaries.
- `ReleaseNotes.md`: public implementation checkpoint history.
- `docs/handover/PROJECT-CONTEXT.md`: historical and product mental model.
- `docs/handover/ARCHITECTURE.md`: implementation architecture, subsystem
  boundaries, safety/performance invariants, and source-reconciliation checklist.
- `docs/handover/DEVELOPMENT.md`: expected development/checkpoint workflow.
- `docs/handover/TESTING.md`: validation architecture and historical bug lessons.
- `docs/handover/DECISIONS.md`: settled, rejected, and unresolved decisions.
- `docs/handover/RELEASES.md`: release-candidate and public-action guidance.
- `docs/handover/PACKAGING.md`: release artifacts, package recipes, store
  publication workflows, credentials, and the external Flathub relationship.
- `docs/handover/PENDING-WEBSITE.md`: internal queue of implementation-driven
  website changes that must be completed during release preparation.
- `docs/handover/ROADMAP.md`: living production-readiness risk assessment.

### Detailed subject handovers

The documents above are the fastest operational route into the repository. These
deeper living documents retain chronology, debate, examples, rejected paths, test
matrices, and implementation cautions:

- `PROJECT-HISTORY.md`: detailed Nift history and institutional context.
- `ARCHITECTURE.md`: durable codebase model and concrete orientation exercise.
- `PARAMETER-INTERPOLATION.md`: `$[...]` behavioral contract and task context.
- `PARAMETER-INTERPOLATION-IMPLEMENTATION.md`: source-aware implementation plan.
- `PROJECT-CONTRACTS.md`: config-declared project contract semantics, rejected alternatives, route-contract distinction, and required executable evidence.
- `CHECKPOINTS.md`: evidence-based development checkpoint methodology.
- `WEBSITE-CHECKPOINTS.md`: website work coordinated with product checkpoints.
- `HANDOVER-MAINTENANCE.md`: document ownership and maintenance architecture.
- `ECOSYSTEM-OVERVIEW.md`: cross-project orientation and repository roles.
- `ECOSYSTEM-HISTORY.md`: detailed institutional context for the ecosystem.
- `ECOSYSTEM-ROADMAP.md`: shared production-readiness principles.

All are living documents subordinate to current repository evidence. Reconcile and
revise them rather than preserving stale transfer language as immutable history.

## Build and validation entry points

```bash
make
```

Focused targets are defined in the Makefile, including JSON, schema, parser,
control-flow, dependency, requirement, path-safety, persistence/concurrency, and
Minify++ integration tests. Inspect the current Makefile rather than relying on a
copied command list. The external contract runs from its own repository:

```bash
NIFT_BIN=/absolute/path/to/nift ./run-contract.sh
```

Performance entry points currently include `make benchmark-10k`,
`make benchmark-memory-10k`, and `make test-tracking-scaling`.

The current Makefile does not define named ASan/UBSan targets. When sanitizer
validation is appropriate, derive flags from the current build safely and record
the exact command/workload used; do not claim sanitizer evidence from a normal
build.

## Development checkpoint rule

A checkpoint is a coherent, evidence-backed state suitable to become the next
trusted baseline. It is not automatically a commit, tag, version, release, push,
or deployment.

```text
trusted baseline → bounded work → evidence → checkpoint candidate
                → reconciliation → validated checkpoint
```

For substantial changes, report baseline provenance, scope, tests added,
focused/full validation, risk-specific checks, performance/memory evidence where
relevant, website/docs/handover impact, repository state, and remaining limits.
The newest files do not become trusted merely by existing.

## Public-action boundary

Inspection, local implementation, tests, benchmarks, local website builds, and
candidate preparation are normal development work. Do not push, tag, publish a
release, deploy a website, alter public versions, or perform destructive
repository restructuring without Nick's explicit approval. Do not create commits
unless requested.

## Jsonic++ synchronization

Nift no longer treats `src/Json.h` as an independently owned parser. The canonical
parser is standalone Jsonic++ `include/json.h`; Nift mirrors the standalone project under `jsonic/` and consumes its public header at
`jsonic/include/json.h`, while `src/Json.h` remains a compatibility include for existing
Nift source. After any Jsonic++ parser change, run standalone Jsonic++ tests and
`make check-nift-sync NIFT_DIR=/path/to/nift`, then Nift `test-json`, schema, JSON
binding, contracts and affected full integration gates. Minify++ also vendors the
same parser privately and must be reconciled separately before the ecosystem
checkpoint is complete.

## Maintaining this handover

This handover and every linked handover document are living project
infrastructure. Review them whenever architecture, behavior, development or test
workflow, release procedure, project relationships, or durable decisions change.
Add lessons from significant investigations and bug families. Correct, reorganize,
split, consolidate, or remove obsolete material. Do not turn the files into
append-only diaries: individual defects normally belong in tests and Git history;
the handover should retain the durable lesson.

A substantial checkpoint is not complete until handover impact and the living
production roadmap have been considered. When the checkpoint changes protected
behavior, regression coverage, test families, or validation evidence, also review
the website's Battle Tested page and related public reliability claims before
calling the Nift work complete.
## 2026-08-18 — vendored Jsonic++ memory-safety gate

- Standalone Jsonic++ Checkpoints 1A and 1B are complete for the maintained lifetime corpus. Sanitizer/RSS evidence is supplemented by an independent Valgrind 3.26.0 Linux pass at canonical Jsonic++ commit `b9d0ff3`: 40 corpus iterations, 0 errors, 0 bytes in use at exit, all 6,579,515 allocations freed.
- Nift's vendored Jsonic++ documentation is synchronized with that canonical record. This is component-level evidence only; Nift's own lifecycle, incremental-state, watch/endurance, large-project and cross-project integration memory gates remain open.
- Keep standalone Jsonic++ as the source of truth for parser lifetime evidence and rerun Nift integration checks whenever the vendored parser implementation changes.
## 2026-08-18 — standalone Minify++ memory-safety checkpoint

- Nift's vendored Minify++ test infrastructure now mirrors the standalone Checkpoint 2A lifetime corpus and CLI stress harness. Standalone Minify++ at `db2a6ff` passed its sanitizer/RSS/CLI campaign without requiring a production source fix.
- This is component-level evidence only. Do not claim Nift-owned Minify++ lifecycle/resource behavior until the later cross-project integration checkpoint exercises that ownership boundary directly.
- Independent standalone Minify++ Valgrind confirmation remains open because the current environment has no Valgrind executable.

## 2026-08-18 — standalone Minify++ memory-safety gate complete

- Standalone Minify++ Checkpoints 2A and 2B are complete. The independent gate used Valgrind 3.26.0 on Linux x86_64 at canonical Minify++ commit `2a51a38`: 30 lifetime-corpus iterations, 0 errors, 0 bytes in use at exit, all 2,448 allocations freed and 184,908 KiB peak Valgrind process RSS.
- The exact evidence is mirrored under `minifypp/docs/evidence/` so Nift's vendored component record remains synchronized with the canonical standalone checkpoint. This is still component evidence, not a Nift lifecycle/resource verdict.
- The memory campaign now advances to Nift core command/state/failure-recovery lifetimes. Long-duration watch/10k endurance remains later, and sustained component-integration ownership stress remains reserved for the cross-project checkpoint.

## Memory-safety Checkpoints 3 and 4A (2026-08-18)

- Checkpoint 3 core lifecycle evidence is retained under `docs/evidence/memory-safety/checkpoint-3-core.json`; 57 sanitizer-backed lifecycle/test phases passed without ASan/LSan/UBSan findings.
- Checkpoint 4A retains native and sanitizer watch evidence plus the 10k worker/minification matrix under `docs/evidence/memory-safety/`.
- `build-auto` exits after a failed watched rebuild; failure/repair cleanup therefore remains part of the repeated command-lifecycle corpus rather than the one-process watch soak.
- Checkpoint 4B is still open pending `make valgrind-memory-safety-checkpoint-4` on Linux with Valgrind.

## Checkpoint 4B Valgrind shutdown-harness correction (2026-08-18)

- The first Linux/Valgrind Checkpoint 4B attempt did not produce leak evidence because `checkpoint4_watch_endurance.py` signalled only the Valgrind supervisor PID and waited four seconds; the monitored `build-auto` process could remain alive underneath it.
- The harness now launches the monitored command in a dedicated session/process group, sends SIGINT to the whole group, allows 30 seconds for Valgrind to finalize its leak report, then escalates to SIGTERM/SIGKILL only if shutdown genuinely wedges.
- A direct Nift probe and a synthetic supervisor+child probe both complete through the new SIGINT path. Checkpoint 4B remains pending until `make valgrind-memory-safety-checkpoint-4` is rerun on Linux with Valgrind and its JSON evidence passes.

## Checkpoint 4B acknowledgement-driven watch harness (2026-08-18)

- The second external Valgrind attempt shut down cleanly and itself reported 0 bytes in use at exit, 39,102 allocations / 39,102 frees and 0 errors, but `build-auto` exited before the 30-cycle workload completed.
- Root cause was test pacing: the harness issued edits every 220 ms regardless of whether the much slower Valgrind-supervised rebuild had finished. That could mutate a dependency while the previous rebuild was still reading it.
- The watch harness is now acknowledgement-driven. After each edit it waits for `public/index.html` to receive a new mtime before issuing the next edit, with a configurable rebuild timeout. The interval is now only an optional post-acknowledgement pause.
- A synthetic slow-supervisor probe completed 30/30 cycles through the new path. The partial clean Valgrind result is useful diagnostic evidence but does not close Checkpoint 4B; rerun the maintained target and retain the full JSON evidence.

## Checkpoint 4B terminal-safe supervisor correction (2026-08-18)

- The third external attempt completed all 30 watch cycles and showed Valgrind-process RSS settling at 197,864 KiB from the midpoint through completion, but shutdown escalated to SIGKILL (`process_exit_status=-9`) before Valgrind could emit a final heap report.
- The harness now launches watch mode with stdin connected to `/dev/null`, so a failed endurance run cannot alter the caller's interactive terminal settings.
- `valgrind_nift.sh` now remains as an explicit supervisor, traps SIGINT/SIGTERM from the harness, forwards the signal to Valgrind and waits for Valgrind to finish. This preserves Valgrind's opportunity to observe Nift termination and write its final leak/error summary.
- A synthetic forwarding-supervisor probe completed 30/30 acknowledgement-driven cycles and shut down through SIGINT with exit 130. Checkpoint 4B remains open until the corrected external Valgrind target completes and its final report is retained.

## Memory-safety Checkpoint 4 complete (2026-08-18)

- Checkpoint 4B now passes on the external Linux/Valgrind host at Nift commit `92e6c05`.
- The corrected acknowledgement-driven watch corpus completed all 30 cycles in 14.044 seconds and shut down through the intended SIGINT path (`process_exit_status=130`, `shutdown=sigint`).
- Machine-readable evidence is retained at `docs/evidence/memory-safety/checkpoint-4-watch-valgrind.json`.
- Wrapper/supervisor RSS was 3,824 KiB at cycle 20 and cycle 29. Treat this as operational telemetry, not the leak oracle.
- Combined with Checkpoint 4A's native 180-cycle settling run, sanitizer watch run and 10k worker/minification matrix, Checkpoint 4 is complete. The memory-safety campaign advances to Checkpoint 5 (tscc).

## Memory-safety Checkpoint 6 complete (2026-08-18)

- At commit `9b64e94`, standalone/embedded synchronization passed for Jsonic++ (20 mirrored files) and Minify++ (24 mirrored files).
- Native cross-component integration completed 60 rounds over 90 pages with schema-validated JSON, Project Contracts, HTML/CSS minification and ordinary content/template rebuilds in the same project. Twenty malformed-JSON/minifier-configuration failures were injected and each recovered successfully.
- The sanitizer build completed 12 rounds over 30 pages with four injected failure/recovery transitions and no ASan/LSan/UBSan finding.
- Focused JSON/schema, Contracts, Minify++, cross-feature, incrementality and persistence/concurrency contracts remain green. Exact evidence is retained under `docs/evidence/memory-safety/checkpoint-6-*.json`.
- Corrected Checkpoint 6B passed externally at commit `03e18b4`: 19 separate Nift invocations ran directly under Valgrind across a 40-page / 12-round mixed workload with four expected component failures; every invocation reported zero Valgrind errors and no non-zero definite/indirect/possible leak bytes. All 12 recovery phases and the final clean build passed. Exact evidence is retained at `docs/evidence/memory-safety/checkpoint-6-valgrind.json`.
- The generic memory campaign completed here. At that point the roadmap moved to incremental-vs-clean equivalence, filesystem/transaction integrity, parser fuzz/resource boundaries and cross-platform behavioural equivalence; all of those later checkpoints are now complete and the hardening plateau has been reached.

## Checkpoint 6B instrumentation correction (2026-08-18)

- The first external 6B `FAIL` is not Nift evidence: the target accidentally ran Valgrind around `python3 scripts/checkpoint6_integration.py`, so Memcheck observed the Python harness rather than the Nift subprocesses.
- `checkpoint6_integration.py --valgrind` now runs every Nift invocation directly under Memcheck. Exit 99, non-zero `ERROR SUMMARY`, and non-zero definite/indirect/possible leak byte counts fail the harness even during deliberately expected Nift build failures.
- The final evidence records each monitored Nift invocation. A synthetic Valgrind shim completed the mixed workload and verified the corrected process boundary.
- Rerun `make valgrind-memory-safety-checkpoint-6`; only that corrected result can close 6B.

## Checkpoint 6B completion note (2026-08-18)

- The corrected external run supersedes the rejected Python-wrapped attempt.
- Commit under test: `03e18b4`; platform recorded by the evidence: Linux 7.0.0-29 x86-64.
- Workload: 40 pages, 12 rounds, four injected Jsonic++/Minify++ failure paths, 12 successful repair phases, 19 directly Valgrind-monitored Nift invocations.
- Every monitored invocation recorded `ERROR SUMMARY: 0 errors`; no non-zero definite/indirect/possible leak bytes were reported.
- Checkpoint 6 is fully closed. Do not keep adding generic memory torture unless future changes invalidate this evidence; advance to Checkpoint 7.
## Battle Tested pre-Checkpoint-7 editorial baseline (2026-08-18)

- The website now defines Battle Tested as scoped executable evidence, not test-count marketing or a claim of perfection. It distinguishes test hardening from field hardening and explicitly acknowledges Nift's shorter production exposure.
- Checkpoint 6 memory/resource evidence is public and complete. Checkpoint 7 incremental-vs-clean equivalence is deliberately described as the next planned/current frontier, not a proven property.
- When Checkpoints 7–10 complete, reconcile implementation ↔ regression evidence ↔ HANDOVER/checkpoint ↔ public Battle Tested claims and promote only the properties actually established.

## Checkpoint 7 complete — incremental-vs-clean equivalence (2026-08-18)

- Maintained gate: `make checkpoint-7-incremental-equivalence`.
- Commit under test: `93b7c2c`.
- All three incremental modes were exercised across 8 deterministic seeds × 30 mutations each: 720 complete public-output-tree comparisons.
- Mutation coverage included content, templates, JSON, schemas, Project Contracts, build-thread config, shared input dependencies, metadata and tracked-page add/move/remove operations.
- After every mutation, the incremental tree was compared byte-for-byte (SHA-256 per path) with a clean `build-all` from the same logical project state. All 720 comparisons passed.
- Discovery corrected the oracle rather than Nift: untracked files in `public/` are user-owned and should not be erased to simulate a clean build. The maintained harness removes only generated tracked outputs and page build metadata.
- Evidence is scoped deliberately: the generated/mutated states tested establish equivalence for that corpus; this is not an absolute proof over every possible Nift project.
- Checkpoint 8 still makes sense unchanged: it attacks safe failure and persistent-state integrity under hostile filesystem/interruption conditions, a property not exercised by successful incremental equivalence.

## Checkpoint 8 complete — filesystem/transaction integrity (2026-08-18)

- Maintained gate: `make checkpoint-8-filesystem-transaction`.
- Commit under test: `e261074`; exact evidence is retained at `docs/evidence/checkpoint-8/filesystem-transaction.json`.
- Thirteen Linux adversarial cases pass: unreadable content/template/JSON, dangling symlink, symlink loop, file↔directory obstruction classes, read-only output/state directories, metadata-write obstruction/recovery, long Unicode path, `RLIMIT_FSIZE` partial-write pressure, and forced `SIGKILL` during a 48 MiB generated-output write.
- Three bug families were fixed: unreadable inputs being conflated with empty files; directory-as-file reads reaching a `std::length_error` abort; and truncate-in-place writes that could destroy the last-good artifact on interruption.
- File/state writes now stage to same-directory Nift temporary files and replace only after a complete write. A killed write leaves the prior output+metadata intact; the next successful write removes stale Nift temporaries.
- Failed output writes do not refresh page metadata. Failed metadata writes leave the page stale so `status`/`build-updated` can repair it.
- Claim scope is intentionally limited to the tested Linux failure classes. Direct ENOSPC and platform-specific Windows/macOS permission/locking semantics remain outside this checkpoint and belong in later platform evidence.
- Checkpoint 9 remains appropriate and distinct: fuzz/sanitizer the n++ parser and explicit resource/depth boundaries, without duplicating standalone Jsonic++ or Minify++ parser campaigns.

## Checkpoint 9 complete — parser fuzz/resource boundaries (2026-08-18)

- Maintained gate: `make checkpoint-9-parser-fuzz`.
- Commit under test: `45d96ba`; exact evidence is retained at `docs/evidence/checkpoint-9/parser-fuzz.json`.
- Sanitizer-backed corpus: 1,200 grammar-aware generated/mutated n++ cases across seeds 9001/17713/424242 plus 17 explicit resource/depth boundaries.
- Final outcomes: 1,217 total cases; 234 successful builds; 983 controlled Nift errors; 0 timeouts; 0 crashes/signals; 0 ASan/LSan/UBSan findings.
- Boundary coverage included recursive control flow around the 64-level parser guard, 8 MiB literal templates, multi-megabyte comments/content, 1 MiB parameters/interpolations, 100k balanced parentheses and high-volume Unicode.
- The 64-level parser-depth boundary was verified directly: 16/32/63/64 succeeded for the nested fixture; 65/80/128 failed cleanly. The diagnostic was clarified from `maximum @input depth` to `maximum template parse depth` because the limit covers recursive parser entry generally.
- No n++ crash/memory bug was found in this campaign. Existing parser/content, comments, control-flow, template-optional, Contracts, Checkpoint 7 and Checkpoint 8 gates remain green.
- Checkpoint 10 still makes sense and is now the final deliberate-hardening campaign: cross-platform behavioural equivalence via normalized Linux/macOS/Windows CI evidence, not merely compilation.

## Codex handover for Checkpoint 10 (2026-08-18)

- The repository is intentionally paused after Checkpoint 9 at a clean handoff boundary.
- The full Checkpoint 10 execution/design brief is now committed at `docs/handover/CODEX-CHECKPOINT-10.md`.
- Preferred execution is a GitHub Actions matrix over `ubuntu-latest`, `macos-latest` and `windows-latest`, with normalized per-platform JSON artifacts and a final semantic comparison job.
- Compare user-visible output semantics separately from selected internal behavioural state; normalize only documented platform noise and keep genuinely platform-specific filesystem expectations separate.
- Checkpoints 7–9 are regression constraints during cross-platform fixes: `make checkpoint-7-incremental-equivalence`, `make checkpoint-8-filesystem-transaction`, and `make checkpoint-9-parser-fuzz`.
- Do not promote Battle Tested claims until the required matrix and final comparer are green. After Checkpoint 10, deliberately stop adding arbitrary hardening checkpoints and move toward distribution, real-world dogfooding and the planned AI-DX example-site experiment.

## Checkpoint 10 complete — cross-platform behavioural equivalence (2026-08-18)

- Maintained gate: `make checkpoint-10-cross-platform`; decisive CI workflow: `.github/workflows/checkpoint-10-cross-platform.yml`.
- Final passing run `32118334090` at commit `f1512bf` executed the same 18 portable contracts on `ubuntu-latest`, `macos-latest` and `windows-latest`; each runner uploaded normalized JSON and the final artifact-consuming comparer reported zero portable mismatches.
- Evidence compares normalized complete output-tree hashes, selected tracked/build metadata, status classes, lifecycle/failure/recovery observations and diagnostic classes. Normalization is deliberately limited to path separators and non-contractual CRLF/LF spelling.
- Two separately classified platform contracts passed: the Windows `.exe` suffix and Windows' need to restore write permission before deleting Nift's deliberately read-only generated files.
- The campaign found and fixed a genuine Windows defect: tracked `mv`/`rm` could leave stale read-only generated output and metadata while tracking state moved on. `filesystem::remove_owned_file` now removes Nift-owned read-only artifacts portably; the cross-platform add/move/remove lifecycle is the maintained regression.
- Checkpoints 7–9 were rerun after the change and remain green: 720 incremental-equivalence comparisons, 13 filesystem/transaction cases and 1,217 sanitizer-backed parser/resource cases.
- Exact run/artifact identities, SHA-256 archive digests, scope, findings and limitations are retained in `docs/evidence/checkpoint-10/cross-platform-equivalence.json`.
- The deliberate hardening sequence is now at its planned plateau. Future emphasis moves to distribution, real-world dogfooding, field exposure and evidence-driven work; do not invent Checkpoint 11 by default.

## v4.0.3 shorthand ternary follow-up (2026-08-19)

- Lazy ternary rendering now also accepts `$[condition ? true-branch]` as shorthand for an empty false branch. The condition is still evaluated by the same condition evaluator as `@if`, and the true branch is parsed only when selected; a false shorthand branch produces no output and no dependency/requirement side effects.
- The source-tree and independent black-box control-flow contracts cover selected/unselected shorthand branches, nested shorthand ternaries and lazy dependency behavior.

## v4.0.3 pure-expression follow-up (2026-08-19)

- `$[...]` now supports pure numeric expressions with `+`, `-`, `*`, `/`, integer-valued `%`, unary signs and parentheses. The same arithmetic is available inside `@if`/ternary conditions; division/modulo by zero, non-integer modulo and arithmetic on non-numeric values fail cleanly.
- `@pathtopage(n)` retains absolute-page semantics, while an explicit leading sign selects a relative offset: `@pathtopage(+1)`, `@pathtopage(-1)`, `@pathtopage(+$[offset])`, etc. Existing `$[paginate.previous]` / `$[paginate.next]` remain supported.
- The feature was defined by source-tree and independent regression tests before implementation.

## v4.0.3 pure collection operations follow-up (2026-08-19)

- Added immutable, composable typed collection operations: `@filter`, `@map`, `@sort`, `@slice`, `@find`, `@some`, `@every`, `@distinct`, `@reverse`, `@sum`, `@prod`, `@min`, `@max`, and `@reduce`.
- Simple forms such as `@sort(array)`, `@sum(array)`, `@prod(array)`, `@min(array)` and `@max(array)` cover direct scalar collections. Advanced per-item forms use the canonical arrow grammar `binding : collection => expression`; the earlier comma-shaped prototype is intentionally rejected before release.
- Tuple bindings such as `@sum((a,b,c) : triples => a + b + c)` positionally unpack array elements and require exact arity. Binding names are ordinary local names, not inferred object-member names.
- `@reduce(item : items & acc = initial => expression)` is the general pure fold. The initial accumulator is evaluated once; each iteration exposes immutable item/accumulator bindings and carries the pure expression result forward. Empty reduction returns the initial value.
- Predicate/mapping/key/aggregation forms reuse the same pure expression evaluator as `$[expression]` and `@if(expression)`.
- Collection operations can consume nested collection operations and remain typed until rendered; `@for` and `@join` consume these values directly. Direct rendering serializes arrays/objects as JSON, making the operations useful in generated JavaScript/JSON as well as HTML.
- `@slice(array, pos, length)` is zero-based and requires non-negative integer position/length. `@find` returns the first match or `null`; `@some`/`@every` short-circuit, with `@every` true for an empty collection. `@distinct` preserves first-occurrence order and `@reverse` returns a reversed copy. `@sum`/`@prod` are numeric; `@min`/`@max` accept homogeneous numbers or strings and reject empty collections.
- The collection surface intentionally has a functional-programming flavour—immutable values, pure transforms, composition and folds—without adding assignment statements, collection mutation, side-effecting callbacks, user-defined functions or a general query/runtime language.
- Contract tests were written before implementation in `tests/collection_ops_smoke.sh`; the independent regression suite mirrors this black-box contract.


## v4.0.4 diagnostic rendering follow-up (2026-08-20)

- Real Aeye/Nift website dogfooding exposed a source-diagnostic presentation bug: parser byte columns were correct, but a template line containing leading tabs could make the single `^` marker appear visually under unrelated HTML text because terminals expand tabs to display columns.
- `ProjectInfo::print_build_error()` now expands tabs in source excerpts before rendering and computes the marker position from display width rather than raw bytes. The diagnostic location (`file:line:column`) retains the source column while the visual marker tracks terminal columns.
- `Parser::fail()` now retains a bounded source span for errors that begin at Nift directives/calls or `$[...]` expressions. Diagnostics underline that span with `^~~~...`, making the offending construct visually unambiguous rather than pointing at one character only.
- TTY diagnostics now use a small Nift-aware lexer rather than `@pathto`-shaped special handling. The complete current `@function` surface is colourized in both diagnostic messages and source excerpts: `@content`, `@pathtopage`, collection helpers (`@filter`, `@map`, `@sort`, `@slice`, `@find`, `@some`, `@every`, `@distinct`, `@reverse`, `@sum`, `@prod`, `@min`, `@max`, `@reduce`), presentation/helpers (`@substr`, `@join`), filesystem/data functions (`@input`, `@pathto`, `@pathtofile`, `@getenv`, `@ent`, `@json`, `@dep`) and control/pagination tokens (`@if`, `@for`, `@item`, `@paginate`). `$[...]` expressions and Nift quoted values are also coloured; the offending span remains visually dominant. Redirected stderr remains plain and ANSI-free.
- The lexer uses an explicit current-function registry so ordinary at-sign syntax such as CSS `@media` and email-like text are not falsely coloured as Nift. HTML attribute quotes containing a Nift call no longer swallow the embedded directive: `href="@pathto('docs')"` highlights `@pathto` and its Nift argument independently.
- Maintained source-tree gates: `make test-console` covers display-width/tab expansion, every current `@function`, `$[...]`, quoted values, false-positive rejection for CSS/ordinary at-sign text, and explicit colour rendering; `make test-diagnostics` creates a real initialized project with a two-tab-indented invalid `@pathto` and verifies caret/underline alignment. Existing path-security, parser-content, requirements and control-flow smoke tests remain green.
- The independent `nift-regression-suite` now carries `contract/diagnostics_smoke.sh` for the black-box alignment/diagnostic contract; ANSI colouring stays implementation-level because redirected contract logs must not contain escape sequences.
