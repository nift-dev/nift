# NIFT v4.8 — FINAL PRE-FREEZE TRANSFORMATION + WEBSITE PASS

## Scope and prerequisite

CP-F is CLOSED at `b11e48b44607bc96898a45c42e66b7a737af2630` before any
new-tranche push: all ten applicable hosted Nift workflows, Deep's seven jobs,
Test Integrity's five jobs, pinned NRS 93 and PRS 12 passed. See the CP-F report.
The already approved migration-guidance tranche at `1871425` was published
separately; its Pages deployment and live canonical migration check passed.

This final tranche adds experimental rewrite/redesign workspaces and completes
transformation/website guidance. No runtime evaluator, optimizer, RuntimeValue,
public ABI header, callback/FFI ownership, package or benchmark implementation
changes belong to it. C ABI remains 1.3. No Release workflow, tag, version bump
or release artifact is authorized or run.

## Transformation contracts

- Migration: same product/design/routes/content/behaviour, faithful port preserving
  implementation intent where practical. Existing `--migration-existing` API
  and canonical handover/plain-init behavior remain protected.
- Rewrite: same product/design/behaviour, freely replaced implementation.
  Experimental `nift init --rewrite` / `--rewrite-existing=POLICY`.
- Redesign: deliberately replaced implementation and design, protected explicit
  requirements/content/capabilities and approved route/redirect decisions.
  Experimental `nift init --redesign` / `--redesign-existing=POLICY`.

Shared mode selection, ownership checks, policy writer, AGENTS augmentation,
HANDOVER and guidance generation avoid cloning init. Workbooks have distinct
14-phase/18-phase methods; investigation records expose reference/behaviour/design
contracts for rewrite and requirements/design-brief/route-map for redesign.
Authored/rendered/hybrid source models remain independent of intent.

All modes permit React/Vue/Svelte/Solid/Web Components/vanilla JS islands,
independently prepared browser bundles and explicit source/accessibility/state/
maintenance/cost tradeoffs. Complete validation precedes a real performance
campaign, then full revalidation, final production-pipeline benchmarks,
clean-checkout proof and handover. Redesign comparisons qualify changed workloads.

Preflight rejects mutually exclusive modes, mismatched policies, foreign workbook
or AGENTS contracts, and malformed owned boundaries before scaffold writes.
Default guidance preserves unrelated content; canonical conflicts fail closed;
keep/append/replace are explicit, deterministic and tested. AGENTS replacement
consumes its owned trailing newline, fixing repeated-block newline accumulation
without changing unrelated instructions. Existing Nift projects cannot reinit.

## Local certification

- Full GCC and isolated Clang `make -j2 test`: PASS. Initial restricted runs
  stopped because ptrace was unavailable; the ordering proof and full reruns
  passed with tracing permitted. These stopped attempts are not green evidence.
  A final concurrent GCC/Clang rerun hit the identifier timing ratio guard at
  4.43 (limit 4); its failed log is retained. Clang passed; the full serial GCC rerun passed without other suite load,
  without changing guard thresholds.
- Final CLI: GCC/Clang `-Wall -Wextra -pedantic -Werror -fsyntax-only`: PASS.
- Existing migration/handover tests: PASS. New rewrite/redesign black-box gates:
  96 initialization cases each, including all policies, deterministic bytes,
  whitespace, reruns, malformed markers, both cross-mode directions, conflicting
  options, ordinary init, handover, targets and extensions.
- Canonical generator `--check`: all three workbook literals byte-exact.
- NRS: 93/93 PASS; extend the existing init module, no module inflation.
- PRS: 12/12 PASS; suite and package repositories unchanged.
- Static test integrity: 284 files, zero findings. Version consistency PASS.
- Guarantee registry cross-repository audit: PASS, four claim surfaces. Historical
  30-target discrepancy reconciled without upgrading enforcement states.
- Website: 106/106 outputs built, then 106/106 up to date. Four canonical displays
  match core bytes and each negative drift mutation is detected. Both sidebar
  copies have Migrations/Rewrites/Redesigns order and direct routes. Distinct
  metadata/canonical URLs and sitemap discovery checked; 18,638 internal href/src
  targets resolve. Agent-readiness check: 97 sitemap URLs coherent.

No browser surface is available in this session (CUA browser inventory empty),
so visual/interactive viewport screenshots are not claimed. Structural desktop/
mobile menus, shared active-link/Escape logic, existing responsive template/CSS
and route/link checks are covered; no CSS or interaction implementation changed.

## Website factual and drift audit

Audit inventory: 121 source files (107 content, 14 templates), including 91 docs
pages; 106 tracked outputs. Search covered CLI/version/install spellings, removed
surfaces, current/pending/completed claims, concurrency/callables, complexity,
embedding, source models, migration/islands, benchmarks and agent discovery.
The initial broad search produced 213 contextual hits; hits are not all defects.
Current facts were checked against CLI/ReleaseNotes, worker-transfer code, public
ABI, Makefile/registry, NRS/PRS results, retained benchmark evidence and migration
reports. GitHub latest release was verified as v4.7.2 on 8 October 2026.

Concrete corrections: label the 30-target list and v4.0.7 rehearsal historical;
stop presenting the v4.3 performance snapshot as current; correct its impossible
“identical sources” wording; qualify earlier competitor context as its August
snapshot; remove speculative future channel-validation wording; distinguish
v4.7.2 release from v4.8 development; reconcile outdated no-runtime philosophy;
clarify worker transfer/resource boundaries; update operational ABI 1.1 to 1.3;
update deep aggregate condition equality; add three missing runtime sitemap
entries; add mode chooser/discovery links, two distinct pages and canonical
mirrors. Removed nift run/sh, LuaJIT/ExprTk and @system mentions retained only
where they explain removed/historical boundaries; no mechanical replacement of
faithful-migration references was performed.

Production-scale hardening is supported by completed Docker (3,901 HTML routes),
Deno (834 HTML documents/2,573 publication files, exact clean reproduction) and
Capgo (1,347 routes) corpus work. Representative browser and private-service
limits remain explicit. AI SDK full-family/browser evidence is described as an
unfinished campaign, not final certification. This supports real-project
migration hardening, not years of widespread independent production, sustained
live traffic, thousands of users or universal superiority. Rewrite/redesign are
not presented as equally mature.

## Independent October opinion

The attributed September record is preserved. October classifications:
retained composition/interoperability/checked relationships/integrated-framework
tradeoffs/community cautions; stronger migration evidence and resumable agent
rails; qualified simplicity, schema/type and iteration claims; weaker universal
speed inference; reversed the restriction against a supported native runtime;
new strength original-source diagnostics; new concern provenance/runtime cost
(CP-F measured 1.028–1.075 whole-process median ratios, scoped workloads).
Internal production-scale confidence grows; independent network/field maturity
does not rise merely because internal certification passed. No new numeric
competitor ranking or fresh benchmark result is invented.

## Hosted/publication closeout

Hosted outcomes are reported by exact certification head and run links in the
final closeout, with machine-readable captures under `.build/`. This prepublication
record makes local claims only; it does not infer hosted success from local passes.
Feature queue closes after this tranche; next work is stabilization and release
preparation. No additional runtime-feature campaign.
