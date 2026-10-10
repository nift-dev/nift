# Nift maintained roadmap

## Current status — v4.11 released and verified

Public v4.11.0 is released and verified at immutable
`c8d9c266518d115e4f289c5c6a2b94ddd7ee9968`. The current executable is v4.11.0;
C ABI remains 1.3. The accepted v4.11 incremental correctness and process
hardening campaigns are CLOSED, with native Linux/macOS/Windows certification.
Hash/hybrid consumers retain their own historical dependency snapshots, native
reads snapshot consumed bytes, and targeted/generated dependency transitions are
covered. POSIX launch safety/cleanup and Windows environment/handle/redirect/
Unicode behavior have strengthened regression and fault guards.

No urgent unresolved correctness issue is known from the post-hardening review.
Trusted project-code and host-coordination boundaries remain explicit; there is
no hostile-repository sandbox claim. See the accepted state review in
`../evidence/v411-state-review/review.json` and process closeout evidence.

## Next review boundary

Release preparation, exact-candidate local/hosted certification, Deep guards,
canonical rehearsal, publication and public-artifact/site verification are complete.
See `../evidence/release-4.11.0/release-verification.md`. Chocolatey moderation and
manual Snap promotion are separate downstream states. STOP for next direction;
return to a bounded product/Labs workload only when separately requested. Further
hardening follows demonstrated defects or missing user contracts.

Persistent RuntimeValue indexing, canonical Jsonic++ optimization, broader
sort/frame architecture, VM/JIT, process redesign and posix_spawn migration remain
deferred unless new evidence changes priority. Known ordinary sort/tiny-save
performance residuals are not release correctness blockers. Frozen official
benchmark series remain immutable; no benchmark campaign is authorized here.

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

## Historical CP49 accepted campaign checkpoint

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

## Historical sort-first follow-up (superseded implementation boundary)

The one-fetch object win is accepted and committed separately. Publish and
certify it first; then investigate capture/frame/instance ownership for sort.
Metadata plan sharing is subordinate to the selected environment ownership
model and must not be implemented as an isolated architecture. Compare current,
shared-environment/overlay, persistent-parent/overlay and COW prototypes outside
production, with exact lookup/escape semantics and measured payoff. Canonical
Jsonic++ threshold work is secondary. Stop before production representation or
ownership changes. Current [checkpoint](../evidence/cp410-capture-frame/README.md).

The historical sort-first authorization above was superseded by the accepted
v4.10 implementation campaign (`../evidence/cp410-implementation-final/report.md`).
It is not current authorization to implement architecture changes.
