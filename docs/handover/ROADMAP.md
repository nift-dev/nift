# Nift maintained roadmap

## Current status — v4.9 orientation

Public v4.8.0 is released at immutable `1bfc4a54b373da3477b497106fa4db59111e2770`.
Development is v4.9.0 after `9de5c3e3e62291c929702271b34f2c870ec2a442`.
The v4.8 feature/performance campaign and release are closed. Historical campaign
records remain evidence; they do not authorize more implementation.

## Near-term checkpoints

1. **CP49-0:** reconcile living roadmap, decisions and handovers with release reality.
2. **CP49-1:** reproduce and repair maintained binding warnings, validate builds/tests
   and aggregate gate, clarify support. No public API/ABI or packaging redesign.
3. **CP49-2:** investigate current performance: scaling, profiles, allocations,
   isolated reproducers and shared root-cause ranking. Stop for review before optimization.
4. **CP49-3+:** choose bounded implementation checkpoints from accepted profiler evidence,
   with semantic, aliasing, ownership and cross-platform safety gates.
5. **Later:** a new formal benchmark series, dedicated-node lab.nift.dev update and
   post-benchmark prioritization. Series `20261008-v480` is complete and frozen;
   never mutate or rerun it, alter workloads/presentation or provision nodes in this checkpoint.

Do not commit to bytecode/JIT, RuntimeValue/object redesign or a broad runtime rewrite.
Rank shared causes by demonstrated cost, breadth, confidence and semantic/lifetime risk.
Use BFS as a cross-check; separate raw Jsonic++ parse from Nift conversion/value work.
Standalone Jsonic++ remains canonical for parser changes.

Deferred architecture: explicit FFI resource-release API, further rewrite/redesign
dogfooding, binding publication/support reassessment, and closure/capture/storage
work only if profiles justify it. No package publication is authorized here.

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

## CP49 review checkpoint

CP49-0 reconciliation and CP49-1 binding maintenance are complete locally.
CP49-2 profiles/ranking are ready for review in `../evidence/cp49/report.md`.
No runtime optimization is implemented. Review the proposed CP49-3 boundary
before proceeding; frozen official measurements remain unchanged.
