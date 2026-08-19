# Nift maintained roadmap

## Current status

Nift has completed the planned Checkpoints 0–10 deliberate hardening campaign and reached the intended **hardening plateau**. The current development executable identifies as `Nift v4.0.3`, following the public v4.0.2 release. Production readiness is now a maintained state rather than a milestone still waiting to be earned through another synthetic checkpoint.

The completed campaign established and retained evidence around component memory/resource safety, Nift lifecycle/endurance behavior, cross-component ownership, incremental-vs-clean equivalence, filesystem/transaction integrity, parser fuzz/resource boundaries, and scoped Linux/macOS/Windows behavioral equivalence. Checkpoint 10 also found and fixed a real Windows read-only artifact deletion defect before the final portable corpus converged with zero mismatches.

Do **not** invent Checkpoint 11 merely to continue the sequence. New hardening campaigns need a concrete trigger: a field defect, an unsupported platform claim worth establishing, a newly introduced semantic contract, or another clearly justified guarantee.

## Current phase: v4.0.3 release reconciliation and distribution verification

The bounded v4.0.3 contract-first programme is now implemented: exactly-one rendered `@content`, logical condition composition, lazy ternary rendering, `@join`, UTF-8-safe `@substr`, modern multi-output pagination, pure `$[expression]` arithmetic shared with conditions, relative `@pathtopage` offsets, and a composable immutable collection layer (`@filter`, `@map`, `@sort`, `@slice`, `@find`, `@some`, `@every`, `@distinct`, `@reverse`, `@sum`, `@prod`, `@min`, `@max`, `@reduce`). The collection surface deliberately has a small functional-programming flavour—pure transforms, projections, predicates, aggregation and folds—without general assignment or mutation. The release programme also includes a verified installer and staged strict-Snap edge experiment. Pagination retains one tracked dependency/invalidation unit while rendering large page sets concurrently and preserving prior successful sets on failed replacement.

The current sequence is now:

1. Keep the v4.0.3 public language/configuration documentation synchronized with the candidate and retain the focused/independent regressions established for the new contracts.
2. Treat the 18 pagination incremental-vs-clean comparisons plus compact sanitizer/TSan pagination gates as scoped evidence for the exercised states, not universal proof.
3. Publish/test the strict Snap candidate only through a non-stable channel until an installed Store artifact proves ordinary project-local `.nift/` access. Revert to classic rather than redesign Nift around Snap if strict confinement cannot support the project model cleanly.
4. Publish and validate the extensionless `nift.dev/install` endpoint against the actual public release artifacts/checksums before presenting it as live installation evidence.
5. Continue v4.0.2 downstream distribution verification while preparing the v4.0.3 release candidate; store/channel propagation remains evidence separate from repository CI.
6. Continue dogfooding real sites and platform targets; turn concrete field failures into focused regressions instead of opening synthetic checkpoint sequences without a trigger.

## Distribution validation direction

Once the latest code is released through the intended channels, prefer a CI matrix that validates installation and a small post-install contract through each channel on the environment that actually consumes it. Examples may include Homebrew on macOS, Chocolatey/winget-style Windows channels where supported, Snap/Flatpak or other Linux channels, and direct GitHub release artifacts.

The important distinction is:

```text
source CI green
    ≠
package is installable and correct

actual package install
    + version/provenance check
    + representative Nift build
    + upgrade/reinstall/uninstall checks where appropriate
    = distribution evidence
```

Do not claim a channel is validated merely because its recipe exists or an upstream submission was accepted. Prefer testing the public artifact users actually receive after propagation. Keep channel-specific constraints explicit rather than forcing false uniformity across package managers.

## Maintained engineering obligations

The hardening plateau does not retire the existing gates. Significant changes should continue to protect the relevant established contracts, including:

- focused source-tree tests and the independent black-box regression suite;
- Checkpoint 7 incremental-vs-clean equivalence when incremental semantics are affected;
- Checkpoint 8 filesystem/transaction integrity when state/output handling is affected;
- Checkpoint 9 parser fuzz/resource boundaries when parser/template semantics are affected;
- Checkpoint 10 cross-platform behavioral equivalence when portable behavior is affected;
- component and Nift memory/resource gates when ownership/lifetime behavior is affected;
- real-site self-builds and documentation reconciliation for public behavior changes.

Run risk-specific gates deliberately; not every edit needs every expensive historical campaign.

## Product and ecosystem work

New functionality should still satisfy Nift's architectural rules: extend the small dependency-aware build layer only where Nift can provide a clear, testable guarantee without swallowing a specialist tool's domain. Lower comparison-table scores in integrated runtimes, framework islands or ecosystem size are not automatic feature requests.

Useful post-plateau exploration may include AI/developer-experience experiments, additional real application patterns, documentation improvements and packaging ergonomics, but these should be judged by user value rather than used as excuses to reopen a completed hardening campaign.

## Living-roadmap rule

This remains a maintained risk assessment. Field findings, release incidents, new platform support, significant language features or architectural changes may add or reorder work. Production bugs should leave regressions where appropriate; new platforms expand evidence; performance and memory remain monitored; documentation and the website remain synchronized with current truth.

“Production ready” and “battle tested” are maintained scoped claims, not permanent medals.
