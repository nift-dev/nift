# Nift maintained roadmap

## Current status — v4.9 release preparation

Public v4.8.0 is released at immutable `1bfc4a54b373da3477b497106fa4db59111e2770`.
Version is v4.9.0; FEATURE FREEZE is ACTIVE and the PERFORMANCE CAMPAIGN is CLOSED.
Runtime `3c0a5b2`, hosted-green identity `02e3793` and accepted documentation-only
investigation `cc60913` are the release-preparation starting identities.
The v4.8 feature/performance campaign and release are closed. Historical campaign
records remain evidence; they do not authorize more implementation.

## Release-preparation order

1. Accepted investigation evidence pushed; no production changes after wave 2.
2. Review canonical v4.9 notes covering both waves and maintained bindings.
3. Repeat local native/binding/sanitizer/NRS/PRS/performance and zero-warning gates.
4. Run appropriate exact-SHA hosted non-release readiness walls.
5. Run canonical non-publishing Release artifacts rehearsal for 4.9.0; inspect
   actual four public archives and checksum manifest.
6. STOP for explicit final tag/release authorization. No Chocolatey submission.

The optional third performance tranche is declined for v4.9. Capture insertion,
owned keys, callable plans/instances, captures/frames/arenas, persistent slots or
indexes, RuntimeValue/root-path/storage and VM/JIT remain post-release work.
Only correctness/regression/release blockers and release documentation/process
fixes are permitted during freeze. Official benchmarks remain post-release;
frozen `20261008-v480`, Labs and official workloads stay unchanged. No nodes.

## Distribution validation direction

Once the latest code is released through the intended channels, prefer a CI matrix that validates installation and a small post-install contract through each channel on the environment that actually consumes it. Examples may include Homebrew on macOS, Chocolatey/winget-style Windows channels where supported, Snap or other Linux channels, and direct GitHub release artifacts.

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

## Semantic archaeology / implementation-caveat audit

Before any multi-implementation Nift specification is frozen, run a dedicated
audit that aggressively discovers accidental behaviour, undefined edges,
platform dependencies, implementation artifacts, inconsistent semantics and
"probably fine" caveats, and classifies each finding as specified behaviour,
bug, implementation detail, permitted platform variation, or unresolved. See
`docs/handover/EMBED.md` for the classification scheme. The Embedded Nift
programme (`nift-embed`) is the seed for this: every project-sensitive
directive funnels through a small host seam, which is exactly where contracts
versus implementation details become visible.

## Living-roadmap rule

This remains a maintained risk assessment. Field findings, release incidents, new platform support, significant language features or architectural changes may add or reorder work. Production bugs should leave regressions where appropriate; new platforms expand evidence; performance and memory remain monitored; documentation and the website remain synchronized with current truth.

“Production ready” and “battle tested” are maintained scoped claims, not permanent medals.

## CP49 accepted campaign checkpoint

The user accepted CP49-0/1/2 and authorized the bounded implementation campaign.
Six measured changes are retained: live numeric identity/index selectors, cached
exact glob ordering keys, owned map/filter moves, one JSON timer preflight, and
ordered group construction indexing. Parameter-scope reserve was rejected and
reverted. Broader typed dispatch, callable hoisting, root/path and loop redesign
remain deferred. See [campaign evidence](../evidence/cp49-campaign/report.md).
Version remains 4.9.0 and C ABI 1.3. Frozen `20261008-v480`, official workloads,
release identities and vendored Jsonic++ remain unchanged. No official rerun,
release, provisioning or architecture redesign is authorized by this campaign.
The bounded campaign is complete: all required local and hosted certification
is green. That historical benchmark recommendation was superseded: official measurements
are deferred until after v4.9 release. Both bounded waves and the identity-sort
investigation are accepted; performance implementation is now frozen. Full certificates and tradeoffs are
recorded in the campaign evidence.
