# REWRITE.md

EXPERIMENTAL v4.8 agent-oriented workflow. Migration has repeated parity-first
production-scale dogfooding; rewrite does not yet have that evidence base.
Parity-constrained work gives agents a clearer acceptance target than open-ended
reimplementation. Use representative proofs and human review before scaling up.

Read this method, `investigation/STATUS.md`, and `HANDOVER.md` before working.
A generated scaffold is placeholder material, not a completed rewrite.

## Rewrite contract

Build the same product and visual design with a new implementation. Preserve
routes, content, responsive/browser behaviour, accessibility, keyboard behaviour,
SEO/meta behaviour, redirects, search, downloads, exports and client interactions.
Differences are regressions unless explicitly approved and recorded.

The old implementation is reference evidence, a design/behaviour specification
and content source, not architecture that must be copied. Replace framework
architecture, components, build/dependency structure, abstractions, adapters and
internal implementation freely where the user experience remains equivalent.
Do not preserve implementation baggage merely because upstream has it.

Migration is a faithful port preserving implementation intent where practical.
Rewrite preserves product/design/behaviour while replacing implementation.
Redesign deliberately changes design/UX under an explicit requirements contract.
Do not silently turn a rewrite into a redesign.

## Source model and interactive islands

Transformation intent and source model are separate choices. Choose authored
(Markdown/MDX/frontmatter retained), rendered (maintained HTML/CSS/JS with explicit
metadata/navigation), or hybrid. A rewrite can use any of them; record the choice
and maintenance model in `investigation/REFERENCE.md`.

Nift generation may compose with independently prepared React, Vue, Svelte or
Solid islands, Web Components, vanilla JavaScript controllers and other browser
bundles. Nift does not itself compile these frameworks. Record bundle preparation
and mounting commands as part of the complete production pipeline.
Framework islands do not make the project "not Nift". Retain islands where they
provide the safest parity path; use vanilla HTML/CSS/JS where it is practical.
Choose by parity, accessibility, maintenance, bundle/runtime cost, shared client
state, source model and compatibility risk. Record significant decisions in
architecture evidence and STATUS before broad implementation.

## Workflow

### Phase 1 - Understand the old product

Read the application and identify its users, essential capabilities and published
interfaces. Preserve the old checkout as evidence rather than editing it in place.
Record source revision and the complete production-equivalent pipeline.

### Phase 2 - Freeze design and behaviour reference

Reproduce and freeze the old output, representative browser captures, viewports,
interactive states and keyboard/accessibility behaviour in REFERENCE.
Classify nondeterminism and external inputs; a Git SHA alone may not freeze them.
Keep REFERENCE OUTPUT separate from REWRITE OUTPUT. Never compare the new output
against itself. Do not proceed on an incomplete or irreproducible reference.

### Phase 3 - Inventory routes, content and features

Reconcile expected, rebuilt and remaining authored inputs, route/file sets,
assets, data, indexes, search, redirects, exports and downloads. Placeholder
scaffold pages do not count. Track accounting in CONTENT-INVENTORY.

### Phase 4 - Extract product requirements

Define explicit BEHAVIOR-CONTRACT and DESIGN-CONTRACT acceptance rules, including
browser/runtime, keyboard/accessibility, responsive and SEO/meta behaviour.
Specify byte equality versus semantic/visual tolerances and approved differences.

### Phase 5 - Choose a new Nift-native architecture

Design templates, shared shells, data schemas, dependency discovery, content
transforms and client islands around the required product, not upstream internals.
Record source model and tradeoffs. Compatibility stages are valid when useful;
do not rewrite semantics simply to make the architecture look cleaner.

### Phase 6 - Prove a representative vertical slice

Build one route spanning real content, layout, transformation, navigation and
interaction. Exercise the hardest required feature and production pipeline.
Prove the architecture and acceptance tests before rebuilding the full corpus.

### Phase 7 - Rebuild shared shells and components

Implement shared heads, navigation, responsive layouts and interaction structures.
Compare them against the frozen design/behaviour reference. Keep required browser
bundles and preparation costs visible.

### Phase 8 - Reconstruct the full corpus

Reconcile every route, content item and capability against the inventory. Account
for generated/listing/search outputs, assets, redirects and downloadable material.
Do not declare completion from representative samples alone.

### Phase 9 - Validate design and behaviour parity

Run the complete design/behaviour contracts, route/file accounting, content checks,
browser/keyboard/accessibility matrix and incremental-versus-clean equivalence.
Classify inherited upstream defects versus rewrite regressions or approved changes
in KNOWN-DIVERGENCES. Unresolved required differences block acceptance.

### Phase 10 - Performance campaign

Profile before optimizing. Measure the complete production-equivalent pipeline
and actual time/RSS hotspots: Nift evaluation/build paths, compatibility adapters,
Markdown/MDX/content transforms, indexes/search, dependencies, repeated parsing or
conversion, filesystem/process work, collection/data hotspots and client bundles.
Make general, maintainable, semantics-preserving, profiling-supported improvements.
Rerun relevant acceptance tests after every meaningful optimization.

Do not special-case benchmark inputs, change corpus/workload to improve numbers,
weaken parity, hide required production/compatibility stages or benchmark a cheaper
workflow than users run. Record profiles, changes, useful before/after measurements,
tradeoffs and deliberately deferred bottlenecks in STATUS/evidence. Defer a
disproportionate architectural rewrite rather than destabilizing the product.

### Phase 11 - Final parity revalidation

Rerun the complete design/behaviour acceptance contracts after the campaign,
including browser and incremental correctness. Record exact revision and commands.
No unresolved required difference may be hidden by a performance improvement.

### Phase 12 - Final benchmark campaign

Benchmark the optimized, parity-certified final rewrite. Measure the complete
production pipeline with serialized repeated samples, median/range, environment,
tool versions, warm/full, fresh/application-cold and changed-input cases as useful.
Report phase/process peak RSS accurately; it is not aggregate simultaneous RSS.
Disclose prepared dependencies, uncontrolled OS caches and compatibility costs.
Exploratory profiling results are not final benchmark results.

### Phase 13 - Clean-checkout proof

Verify fresh checkout, dependency acquisition, complete build and acceptance tests.
Reject hidden untracked state and developer-machine absolute paths. Record exact
commands and source/external-input identities another maintainer can reproduce.

### Phase 14 - Handover

Keep HANDOVER and STATUS current: revisions, source model, architecture/islands,
corpus accounting, contract results, divergences, profiles/deferrals, benchmark
methodology, clean-checkout proof, blockers and the exact next action.

## Operating boundaries and reruns

Do not modify Nift core for a source-project compatibility gap without stopping
and reporting a narrow reproduction first. This workflow authorizes site
implementation changes, not new Nift runtime features or publication by itself.
Never silently weaken the rewrite contract.

`--rewrite-existing=error|keep|append|replace` controls canonical workbook/HANDOVER
conflicts; default error fails before scaffolding. Existing README/investigation
state is preserved by default. AGENTS updates only its owned rewrite block and
preserves unrelated text. Malformed markers and another transformation mode are
rejected without overwriting their contract. An existing Nift project is not
reinitialized; continue using its STATUS/HANDOVER instead.
