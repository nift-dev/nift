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
- Current executable identity: `Nift v4.0.2`.
- Language/toolchain: C++17 and Make.
- Output convention for modern projects: `public/`.
- Current branch: `main` in this checkout.
- Public documentation: the separate `nifty-site-manager.github.io` repository.
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

## Five things not to misunderstand

1. The modern simplification was deliberate. Do not restore LuaJIT, ExprTk,
   mutable template scripting, arbitrary shell execution, or build hooks merely
   because older Nift had them.
2. Dependencies and requirements are different. A dependency can change output
   bytes; a requirement means generated output assumes a path continues to exist.
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
