# Nift production-readiness roadmap

## Living-roadmap rule

This is a maintained risk assessment, not a fixed launch checklist. Review it at
every substantial checkpoint. New failures, architecture findings, sanitizer
results, real projects, performance evidence, documentation gaps, or release
work may add, remove, reorder, or redefine priorities. Once production status is
reached, continue maintaining the roadmap for preserving production quality.

## Current inherited assessment

Nift is the most mature of the related projects and appears release-near, but that
is a hypothesis to be re-earned against the live repository. Production readiness
means cumulative behavioral correctness, safe filesystem/state handling, precise
incrementality, real-world dogfooding, performance stability, accurate docs, and
a reproducible release process—not merely a version number.

## Immediate checkpoint sequence

1. Reconcile current repository, local tests, external suite, website, and
   retained benchmark evidence.
2. Preserve the now-green `$[...]` parameter-level contract and its strict
   value/operation boundary.
3. Extend source-guided adversarial testing around parser/value/dependency edges.
4. Continue proving lexical scope, escaping, type behavior, source-bound argument boundaries,
   non-recursion, path-safety parity, and failure transactionality.
5. Prove A→B lifecycle replacement for dynamic inputs, dependencies, JSON sources,
   and requirements where supported, across relevant incremental modes/watch.
6. Run the complete existing contract and targeted adversarial/source-guided
   audit around parser/value/dependency interactions.
7. Run appropriate native safety validation.
8. Recheck literal-heavy and interpolated performance, 10k scaling, and memory.
9. Build the real Nift website and representative templates with the candidate.
10. Reconcile docs, AI guidance, decisions, testing lessons, and handovers.
11. Resolve release-blocking findings and formalize the release candidate process.
12. Decide with Nick whether evidence justifies production publication.

Repository reconciliation and a clean 14-module pre-feature contract run were
completed on 2026-08-16. The parameter feature was then implemented and the
expanded suite passed all 15 modules, including its 73-check focused contract.
Local integration tests, an ASan/UBSan candidate run, a disposable 40-page Nift
website build, and the retained 10k performance fixture also passed. LeakSanitizer
could not run under the desktop environment's ptrace supervision; retain that as
a future native-environment check rather than claiming leak coverage here.

If the feature exposes deeper dependency-state or parser architecture defects,
expand the roadmap rather than promoting the candidate prematurely.

## Broader evidence-building work

- realistic JSON/Schema-driven catalogue using nested control flow and sorting;
- large non-flat dependency topology with shared and page-specific inputs/data;
- migration of an existing ordinary HTML/CSS/JS site;
- larger multi-page islands project maintained after initial generation;
- backend/API application frontend;
- deliberate failure exercise for malformed data, missing requirements,
  minification, collisions, and recovery;
- fresh-install/package and broader Linux/macOS/Windows validation.

These are evidence opportunities, not automatic release blockers. Promote them
based on risk and intended production claims.

## Post-production maintenance

Every production bug should leave a regression where appropriate. New language
features expand the contract. New platforms expand release evidence. Performance
and memory remain monitored. Documentation and the website remain synchronized
with current truth. “Production ready” is a maintained quality state, not a
permanent medal.
