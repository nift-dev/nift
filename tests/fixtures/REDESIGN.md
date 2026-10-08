# REDESIGN.md

EXPERIMENTAL v4.8 agent-oriented workflow. Redesign has not established migration's
production-scale parity-first evidence base. Open-ended design work needs explicit
requirements, representative prototypes and human direction review before scaling.

Read this method, `investigation/STATUS.md`, and `HANDOVER.md` before working.
A generated scaffold is placeholder material, not an accepted redesign.

## Redesign contract

Replace implementation and deliberately replace design/UX. The old project is
requirements evidence, content corpus, brand/source material and product history,
not a visual or architecture specification that must be copied.

Inventory and protect important functionality, content, capabilities, legal and
compliance material, accessibility requirements, SEO value, inbound routes,
redirects, search, downloads/exports and useful brand assets. Define what must
remain, may change, or may retire, with approval and acceptance evidence.

Information architecture, visuals, typography, layout, navigation, components,
interaction model and client architecture may change deliberately. Route changes
require an old-to-new route map, redirect or approved retirement decision, and
verification evidence. Do not silently discard content or inbound-route value.

Migration faithfully ports the same product/design; rewrite rebuilds the same
product/design with new implementation. Redesign uses old requirements/content
as evidence for new implementation and design. Intentional differences follow
an explicit contract; unspecified required losses remain regressions.

## Source model and interactive islands

Redesign intent does not choose the maintained source model. Authored
(Markdown/MDX/frontmatter), rendered (HTML/CSS/JS with explicit metadata/navigation)
and hybrid models are all available. Record the choice independently in DESIGN-BRIEF.

Nift generation can compose with independently prepared React, Vue, Svelte or
Solid islands, Web Components, vanilla JavaScript controllers and other browser
bundles. Nift does not itself compile these frameworks. Include bundle preparation
and mounting in the complete production pipeline. Framework islands do not make
the project "not Nift". Choose by protected functionality, accessibility,
maintenance, bundle/runtime cost, shared client state, source model and risk.
Use vanilla code where practical; use framework islands where complex/shared state
or interactions make them the cleaner solution. Record significant decisions.

## Workflow

### Phase 1 - Audit the existing project

Understand users, capabilities, content, implementation and published surfaces.
Record revision, external inputs and production pipeline. Preserve an old checkout
and reference captures as evidence; do not destroy source material while exploring.

### Phase 2 - Extract immutable requirements

In REQUIREMENTS, identify protected functionality, important content, legal/
compliance material, accessibility, SEO, exports/downloads and product obligations.
Define acceptance tests and decision owners. Do not infer permission to remove a
capability merely because the old design is changing.

### Phase 3 - Classify what works, fails or is obsolete

Distinguish observed shortcomings, strengths worth retaining and approved
retirements. Separate source defects from proposed changes. Record reasons and
approval evidence in the design brief and divergence ledger.

### Phase 4 - Inventory content, capabilities and routes

Reconcile content, assets, data, search, exports and route/file inventories.
Classify preserve, transform, merge, replace or retire decisions. Track inbound
route value and redirect requirements in ROUTE-MAP; scaffold pages do not count.

### Phase 5 - Define redesign goals

Specify audiences, problems, success criteria and constraints. Convert subjective
preferences into reviewable goals without claiming a prototype proves full delivery.
Define intentional changes and protected requirements before implementation.

### Phase 6 - Define information architecture

Design content hierarchy, taxonomy, navigation and routes around goals. Map old
routes to new destinations or approved retirements. Verify that discovery, search
and important inbound links retain a usable path.

### Phase 7 - Define the design system

Specify typography, spacing, colour, contrast, responsive layouts, components and
brand assets. Define accessible states, empty/loading/error states and reusable
patterns. Record decisions in DESIGN-BRIEF and prototype evidence.

### Phase 8 - Define the interaction model

Design browser/client states, navigation, keyboard/focus behaviour, forms and
shared state against requirements. Choose islands/controllers deliberately and
include their preparation pipeline, accessibility and maintenance costs.

### Phase 9 - Choose the maintained source model

Choose authored, rendered or hybrid independently of redesign intent. Record
content editing, data/schema ownership, templates and build/transformation stages.
Do not force a rendered model merely because the old design is being replaced.

### Phase 10 - Build a representative prototype

Build a real vertical slice with representative content, responsive layouts and
the hardest required interaction. Exercise complete production preparation.
A prototype is not full corpus accounting or final acceptance.

### Phase 11 - Validate direction

Review the prototype against redesign goals and protected requirements. Record
human approval or required corrections before broad implementation. Freeze an
accepted direction and acceptance rules; do not silently move goals mid-build.

### Phase 12 - Build the complete project

Implement shared structure and the full content/capability inventory. Reconcile
all items and route decisions, including indexes, search, downloads, assets,
redirects and externally generated inputs. No unexplained required item may vanish.

### Phase 13 - Accessibility and browser acceptance

Validate REQUIREMENTS, DESIGN-BRIEF, content/route accounting, browser/viewports,
keyboard/focus/accessibility, interactions, SEO/meta and incremental/full
equivalence. Compare protected requirements rather than demanding old visual parity.
Classify intentional changes and regressions in KNOWN-DIVERGENCES.

### Phase 14 - Performance campaign

Profile before optimizing the complete production-equivalent workflow. Locate
actual time/RSS hotspots in Nift build/evaluation, compatibility stages,
Markdown/MDX/content transforms, indexes/search, dependencies, repeated parsing/
conversion, filesystem/process work, collection/data operations and client bundles.
Prefer general, maintainable, semantics-preserving, profiling-supported changes.
Rerun relevant acceptance checks after every meaningful optimization.

Do not special-case benchmark inputs, alter corpus/workload to improve numbers,
weaken acceptance, hide required stages or time a cheaper workflow than users run.
Record profiles, changes, useful before/after evidence, tradeoffs and deliberate
deferrals in STATUS. Defer disproportionate architectural rewrites rather than
destabilizing accepted direction or required functionality.

### Phase 15 - Final full revalidation

Rerun the complete requirements/design acceptance contract after the campaign,
including protected content, route map, browser/accessibility and incremental
correctness. Resolve required regressions and record exact revision and commands.

### Phase 16 - Final benchmark or comparison

Measure the optimized, validated complete production pipeline. Use repeated
serialized samples and disclose environment, tool/source identities, median/range,
warm/fresh/changed-input cases, prepared dependencies, OS-cache limits and phase/
process peak RSS. Keep required compatibility and bundle stages in scope.

A redesign may change the workload legitimately under its approved goals.
Explain changed routes/content/capabilities and avoid speedup claims from unequal
workloads. Separate comparisons that are meaningful from those that are not;
do not alter the agreed workload merely to improve benchmark numbers.

### Phase 17 - Clean-checkout proof

Verify fresh checkout, dependency acquisition, complete build and acceptance tests.
Reject hidden untracked state and developer-machine paths. Record reproducible
commands and source/external-input identities.

### Phase 18 - Handover

Keep HANDOVER/STATUS current: accepted goals, requirements, source model,
architecture/islands, content accounting, route decisions, acceptance results,
intentional divergences, campaign evidence/deferrals, valid benchmark limitations,
clean-checkout proof, blockers and the exact next action.

## Operating boundaries and reruns

Do not modify Nift core for a compatibility gap without stopping and reporting a
narrow reproduction first. Redesign does not itself authorize publishing, deleting
protected material or waiving requirements. Get explicit direction approval before
broad implementation; classify changes instead of pretending visual history changed.

`--redesign-existing=error|keep|append|replace` controls canonical workbook/HANDOVER
conflicts. Default error fails before scaffolding; existing README/investigation
state is preserved. AGENTS manages only its redesign block and preserves unrelated
text. Malformed markers and another transformation mode fail without overwriting
contracts. An existing Nift project is not reinitialized; resume from its records.
