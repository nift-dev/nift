# Nift maintained roadmap

## Current status

Nift has completed the planned Checkpoints 0–10 deliberate hardening campaign and reached the intended **hardening plateau**. The current development executable identifies as `Nift v4.0.3`, following the public v4.0.2 release. Production readiness is now a maintained state rather than a milestone still waiting to be earned through another synthetic checkpoint.

The completed campaign established and retained evidence around component memory/resource safety, Nift lifecycle/endurance behavior, cross-component ownership, incremental-vs-clean equivalence, filesystem/transaction integrity, parser fuzz/resource boundaries, and scoped Linux/macOS/Windows behavioral equivalence. Checkpoint 10 also found and fixed a real Windows read-only artifact deletion defect before the final portable corpus converged with zero mismatches.

Do **not** invent Checkpoint 11 merely to continue the sequence. New hardening campaigns need a concrete trigger: a field defect, an unsupported platform claim worth establishing, a newly introduced semantic contract, or another clearly justified guarantee.

## Current phase: v4.0.2 distribution verification and v4.0.3 maintenance

The bounded post-hardening initialization feature is complete in the development tree: `nift init`, `--ext=.ext`, and nine `--target=<platform>` presets are implemented, covered by focused/local and independent black-box contracts, and the native `ubuntu-latest` / `macos-latest` / `windows-latest` init-target Actions matrix is green. This did not reopen the hardening campaign.

The current sequence is now:

1. Keep the released 4.0.2 initializer/platform contract and documentation reconciled; broaden it only in response to a concrete requirement or defect.
2. Treat the generated target files/layouts as implementations of the documented provider conventions, not as claims of end-to-end provider certification. Real provider deployments are optional dogfooding/field evidence and are **not** a 4.0.2 release gate.
3. Track v4.0.2 independently through each downstream channel until the public package is installable and verified; workflow submission alone is not availability evidence.
4. Use GitHub Actions or similarly reproducible clean environments to install from the **actual 4.0.2 distribution channels** and run post-install smoke/behavioral checks. Testing an older package may rehearse the harness but is not 4.0.2 distribution evidence.
5. Continue dogfooding platform targets and real projects. Turn concrete provider, packaging or field failures into focused regressions where practical rather than inventing a new synthetic hardening checkpoint.

The homepage remains intentionally unchanged for the platform-target feature.

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
