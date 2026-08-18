# Nift maintained roadmap

## Current status

Nift has completed the planned Checkpoints 0–10 deliberate hardening campaign and reached the intended **hardening plateau**. The current development executable identifies as `Nift v4.0.2`. Production readiness is now a maintained state rather than a milestone still waiting to be earned through another synthetic checkpoint.

The completed campaign established and retained evidence around component memory/resource safety, Nift lifecycle/endurance behavior, cross-component ownership, incremental-vs-clean equivalence, filesystem/transaction integrity, parser fuzz/resource boundaries, and scoped Linux/macOS/Windows behavioral equivalence. Checkpoint 10 also found and fixed a real Windows read-only artifact deletion defect before the final portable corpus converged with zero mismatches.

Do **not** invent Checkpoint 11 merely to continue the sequence. New hardening campaigns need a concrete trigger: a field defect, an unsupported platform claim worth establishing, a newly introduced semantic contract, or another clearly justified guarantee.

## Current phase: bounded 4.0.2 platform initialization, then distribution and field evidence

Before distributing 4.0.2, the candidate is taking one bounded post-hardening usability feature: explicit project initialization through `nift init`, `--ext=.ext`, and `--target=<platform>`. This is not a reopened hardening campaign. It prepares small provider-correct static project shapes while leaving authentication, provisioning, runtime services, and provider-specific deployment tooling outside Nift.

Priorities:

1. Finish and validate the 4.0.2 initialization contract, including the generic HTML/PHP/neutral starter families and the supported platform targets.
2. Keep provider behavior factual and minimal; run local/native cross-platform target-generation checks before release and obtain end-to-end provider evidence where credentials/workflows make that practical.
3. Reconcile the Nift repository, independent regression suite, website platform guides, command reference and release notes with the final 4.0.2 behavior. The homepage remains intentionally unchanged for this feature.
4. Release/distribute 4.0.2 through the supported channels and reconcile recipes, checksums and version-sensitive install material.
5. After the public packages propagate, use GitHub Actions or similarly reproducible clean environments to install from the **actual distribution channels** and run post-install smoke/behavioral checks. Testing an older package may rehearse the harness but is not 4.0.2 distribution evidence.
6. Continue dogfooding Nift across its own sites and representative real projects; treat unfamiliar user workflows as a new source of evidence.
7. Convert reproducible production/distribution defects into focused regressions when practical, and keep Battle Tested, Production readiness, handovers and package documentation synchronized with what current evidence actually proves.

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
