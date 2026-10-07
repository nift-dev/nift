# MIGRATION.md

A migration workbook for converting an existing site to Nift.

This project was created with `nift init --migration`: a normal Nift project
with an agent-ready migration playbook. It is **not** an automatic converter.
Nift provides the rails; the human or coding agent understands the source
framework. Parity comes before cleanup.

Read this file first. Then read `investigation/STATUS.md` for where the
migration actually is, and `HANDOVER.md` for the current operational state.
`AGENTS.md` (created or augmented by init) points agents here.

## Operating contract

These rules are the migration contract. Do not violate them without explicit
authorization.

- Preserve route, content and behavioural parity unless divergence is
  explicitly approved.
- Do not modify Nift itself to solve source-project compatibility problems
  without stopping and reporting the requirement first.
- Commit at meaningful migration checkpoints.
- Maintain the known-divergence ledger (`investigation/KNOWN-DIVERGENCES.md`).
- Do not classify the migration as complete while unexplained differences
  remain.
- Prefer faithful compatibility stages over speculative rewrites.
- Do not optimize or redesign before the faithful baseline is established.
- Do not destroy the upstream/reference implementation while it is still
  needed for comparison.
- Record exact source, toolchain and external-input revisions.
- Keep `HANDOVER.md` and `investigation/STATUS.md` current after meaningful
  checkpoints.

## Scaffold is not migration

Initialization has already created a normal Nift project (starter `content/`,
`templates/`, output directory, `AGENTS.md` managed block, `MIGRATION.md`,
`HANDOVER.md`, `README.md` and `investigation/`). That is **scaffold creation**,
not migration implementation.

The initial Nift scaffold is **placeholder material**. Do not include it in
parity counts, route inventories, file counts or benchmark claims. Replace or
retire it during architecture proof once the migrated publication structure is
established.

Do not begin translating or modifying upstream site content until the upstream
source, the complete production build and the parity baseline have been
established and frozen. An initialized project is allowed to exist before that
baseline work; that does not relax the rule against migrating before the
baseline is reproducible.

## Workflow

### Phase 1 - Understand the source project and freeze the baseline

Establish and record in `investigation/BASELINE.md`:

- source framework/generator and version;
- upstream repository and exact source revision/commit;
- **source model** (see below);
- source build command (and development command where relevant);
- the **complete production-equivalent pipeline** (see below);
- deployment architecture;
- authored content inventory;
- templates/layouts inventory;
- components/shortcodes/render hooks inventory;
- static asset inventory;
- generated/index/listing pages;
- redirects;
- data files and schemas/taxonomies/frontmatter conventions;
- special rendering behaviour;
- the authoritative route set;
- upstream build time and peak memory.

Complete the external/generated-input inventory
(`investigation/EXTERNAL-INPUTS.md`): a pinned Git SHA does **not**
necessarily define the complete production input set.

Invariant: **DO NOT START MIGRATION UNTIL THE COMPLETE SOURCE BASELINE CAN BE
REPRODUCED AND FROZEN.**

#### Source model

Record which maintained-source model this migration uses, in
`investigation/BASELINE.md`:

- `authored` - human + agent oriented; preserve Markdown/MDX/frontmatter
  structured authored sources where appropriate;
- `rendered` - agent-primary; maintain rendered HTML/CSS/JS with explicit
  metadata/navigation where that is the chosen architecture;
- `hybrid` - describe explicitly.

Publication/behaviour parity does **not** require different maintained-source
architectures to preserve identical source semantics. An authored-source
migration and an agent-primary rendered-source migration can both be valid.

#### Complete production pipeline

A successful upstream build does **not** necessarily mean the complete
production publication was reproduced. Identify and record the full set of
production steps before freezing the reference, for example: build, search
indexing, post-processing, API/reference generation, download/export
generation, asset processing, deployment transforms, and registry-generated
data. Some pipelines require multi-stage execution (for example a build, then
a search-index generation, then a build that consumes the search-derived
output). Freeze the **reference publication** under an immutable path, kept
visually and semantically distinct from the Nift migration output so the
migration can never silently compare its output against itself.

#### Upstream nondeterminism classification

Classify the frozen reference:

- `byte deterministic`;
- `semantic deterministic`;
- `nondeterministic but bounded/understood`;
- `unresolved`.

Do not demand byte identity where upstream itself cannot reproduce
byte-identical output. Instead: reproduce upstream, characterize its
nondeterminism, freeze the accepted reference, document the classification,
and compare Nift against that accepted reference using the appropriate parity
rule.

### Phase 2 - Define the parity contract and fixtures

Record the migration-specific parity contract in
`investigation/PARITY-CONTRACT.md` (what "parity" means here: route parity,
generated-file parity, content parity, browser/visual parity where relevant,
runtime/interactive behaviour, keyboard/accessibility behaviour where
relevant, redirects, search/indexing, downloads/exports, allowed divergences,
byte-versus-semantic equality).

Build appropriate reference evidence before broad migration. Use proportional
fixtures; do not mandate browser testing where it is irrelevant. Candidate
fixtures:

- route inventory;
- generated-file inventory;
- representative browser captures;
- content checks;
- navigation checks;
- keyboard/runtime behaviour where relevant;
- mobile/desktop viewport coverage;
- the known-divergence ledger (`investigation/KNOWN-DIVERGENCES.md`).

Maintain progress in `investigation/STATUS.md`, which is the resumable
checkpoint ledger for the migration.

### Phase 3 - Establish the initial Nift structure

Set up:

- project initialization;
- tracking;
- source/output layout;
- templates;
- dependencies;
- schemas/taxonomies where appropriate;
- migration checkpoint structure.

Do not prematurely redesign the source architecture. Treat the starter
scaffold as placeholder and replace it here.

### Phase 4 - Migrate shared shells/templates first

Identify and recover the largest common structure first:

- common page shells;
- shared heads;
- headers/footers/navigation;
- layouts;
- repeated structural components.

### Phase 5 - Migrate authored content

Reconcile explicit corpus accounting (excluding placeholder scaffold):

- expected authored inputs;
- converted inputs;
- remaining inputs;
- generated/listing layouts;
- route counts.

Counts must be reconciled rather than guessed.

### Phase 6 - Source-specific compatibility

**MIGRATION SUPPORT DOES NOT MEAN NIFT MUST NATIVELY REIMPLEMENT EVERY SOURCE
GENERATOR'S SEMANTICS.** Faithful compatibility stages are legitimate. For
example, a Hugo source may require Goldmark or Chroma, MDX may use an
external/package rendering stage, and special shortcodes may need adapters.

Rule: **Prefer faithful compatibility over semantic rewrites unless the
migration goal explicitly permits behavioural changes.** And: **DO NOT MODIFY
NIFT CORE TO SOLVE A SOURCE-PROJECT COMPATIBILITY GAP WITHOUT STOPPING AND
REPORTING THE REQUIREMENT FIRST.**

### Phase 7 - Route/content/rendered parity

Require:

- an authoritative route-set comparison;
- missing and extra route detection;
- generated-file-set comparison where meaningful;
- content/render parity checks;
- explicit divergence classification in
  `investigation/KNOWN-DIVERGENCES.md`.

No migration is complete while unexplained differences remain. Record whether
each difference is inherited upstream behaviour or a migration regression.

### Phase 8 - Incremental correctness

Verify, as appropriate for the project:

- single-page edits;
- shared-template edits;
- asset changes;
- data changes;
- dependencies;
- targeted builds;
- incremental-vs-full equivalence.

A migration that only passes a full clean build is insufficient evidence.

### Phase 9 - Benchmark

Measure the **complete production-equivalent pipeline**, not only `nift
build`. Where practical, take five serialized samples and report median with
range, and distinguish:

- warm full;
- fresh / application-cold;
- changed-input.

Record proportionally:

- source baseline build time;
- Nift clean/full build time;
- warm/full and targeted/single-page builds where useful;
- peak RSS (per-process/phase, which is not aggregate simultaneous pipeline
  RSS);
- environment/hardware;
- measurement methodology.

Disclose explicitly: whether prepared dependencies/binaries are inside or
outside the timed interval, that OS caches are uncontrolled unless
deliberately controlled, that search/indexing/post-processing costs stay
visible, and that a production build is not a dev-server hot reload. Do not
cherry-pick. Do not hide compatibility-stage cost. If an external renderer
exists, measure and report its cost separately where useful.

### Phase 10 - Clean-checkout verification

Before completion verify on a fresh checkout:

- fresh clone;
- dependency acquisition;
- build from scratch;
- route/content checks;
- no reliance on untracked local state;
- no absolute developer-machine paths.

### Phase 11 - Handover / final report

Produce a final report containing at least:

- source revision and source model;
- migration revision;
- external-input inventory summary;
- route counts;
- content counts;
- known divergences;
- compatibility stages;
- build measurements;
- memory measurements;
- clean-checkout result;
- remaining work;
- maintenance recommendation.

## Rerun behaviour

Running `nift init --migration` inside an already-initialized Nift project
refuses rather than destructively recreating the project. Existing generated
guidance (README, `investigation/`) is preserved by default; canonical
`MIGRATION.md`/`HANDOVER.md` follow the `--migration-existing` policy
(`error` default, `keep`, `append`, `replace`). A future `--refresh-guidance`
mode is a possible roadmap item, not current behaviour.

## Document roles

- `MIGRATION.md` - methodology, baseline, migration plan, parity
  requirements, checkpoints, migration-specific evidence/state.
- `HANDOVER.md` - current operational state, completed work, current blocker,
  exact commands, important decisions, exact next action.
- `README.md` - concise operational entry point (status, upstream reference,
  source model, build command, validation command, document locations).
- `AGENTS.md` - project instructions (Nift's migration block is managed and
  safe to augment).
- `investigation/STATUS.md` - resumable checkpoint ledger (where the migration
  is, gates, next checkpoint).
- `investigation/BASELINE.md` - upstream reference, source model, complete
  production pipeline, frozen reference.
- `investigation/EXTERNAL-INPUTS.md` - external/generated build inputs that a
  Git SHA alone does not capture.
- `investigation/KNOWN-DIVERGENCES.md` - divergence ledger (inherited upstream
  versus migration regression).
- `investigation/PARITY-CONTRACT.md` - migration-specific parity definition.
- `investigation/` - baseline evidence, reproductions, audit notes, route
  inventories, browser-matrix metadata, benchmark data, compatibility findings.

## What comes after migration

A faithful migration achieves source parity first. Once the constraints are
understood, a subsequent native-Nift evolution may simplify the architecture -
but do not strip away a compatibility stage until parity is proven and the
simplification is explicitly authorized.
