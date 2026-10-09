# Nift maintained roadmap

## Current status — v4.10 scripting competitiveness review

Public v4.9.0 targets immutable `aaadeb31219251b7cc02a62fbf75360ac3e11aaf`.
Release/public installer verification and Chocolatey submission are recorded in
[release verification](../evidence/release-4.9.0/release-verification.md).
Development is 4.10.0 with C ABI 1.3. The v4.9 performance campaign is closed;
the separately authorized v4.10 investigation starts at local `629b1f2`.

The [v4.10 candidate report](../evidence/cp410-competitiveness/report.md) explains
fresh callback/frame/capture, native dispatch, value lifecycle and wide-object
costs. Both bounded changes are accepted: canonical capture insertion and
allocation-free native name probes. Normal commits/push and exact-SHA hosted
certification are authorized. The peer competitiveness target remains unmet;
the frozen `20261009-v490` series stays immutable. No campaign Labs edits,
benchmark rerun, node provisioning or new release are authorized.

## Next review boundary

1. DONE: accepted source was committed and normally pushed at `4d80ca47`;
   all ten exact-SHA hosted certifications passed.
2. Prioritize sort/selector factory, capture, frame, instance and retention costs.
   Extend exact semantic contracts and quantify the post-acceptance decomposition.
   Design immutable plans plus fresh instances; return ownership, diagnostics,
   captures, module and async proof for review before architecture implementation.
3. As a secondary bounded experiment, implement one-fetch member access after
   baseline alias/root-path/mutation oracles; retain only a measured safe win.
4. Investigate canonical standalone Jsonic++ wide duplicate-key checking and
   propose a fix. No standalone implementation or vendored-only patch is authorized.
5. Persistent indexing and RuntimeValue layout remain review boundaries. A new
   official series requires meaningful accepted changes and separate authorization.

No speculative reclamation, reusable frames, arenas, compiler-wide slots,
bytecode/JIT or capture/identity semantic changes are part of this checkpoint.

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

The [sort-first review](../evidence/cp410-sort-first/report.md) records the
post-acceptance decomposition, ownership proposal and separate local object
experiment. Callable architecture and canonical Jsonic++ implementation remain
review boundaries. No release or official benchmark run is authorized.

## Accepted sort-first follow-up and next boundary

The one-fetch object win is accepted and committed separately. Publish and
certify it first; then investigate capture/frame/instance ownership for sort.
Metadata plan sharing is subordinate to the selected environment ownership
model and must not be implemented as an isolated architecture. Compare current,
shared-environment/overlay, persistent-parent/overlay and COW prototypes outside
production, with exact lookup/escape semantics and measured payoff. Canonical
Jsonic++ threshold work is secondary. Stop before production representation or
ownership changes. Current [checkpoint](../evidence/cp410-capture-frame/README.md).
