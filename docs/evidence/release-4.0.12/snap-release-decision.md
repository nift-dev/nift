# v4.0.12 Snap release decision (2026-09-11)

## Decision

- **v4.0.12 remains the current release** on the channels where it successfully
  shipped (GitHub release archives, Homebrew, and the Chocolatey submission).
  It is not treated as globally withdrawn or broken.
- **The v4.0.12 Snap rollout is abandoned.** It must not be promoted to
  candidate or stable, and historical Snap revisions 802–822 must not be used
  without authoritative provenance.
- **Snap stable remains on its prior valid version** (4.0.11) until v4.0.13 is
  released through the repaired Snap pipeline.
- **Chocolatey v4.0.12 remains under moderation** and must not be resubmitted
  without a separate decision.
- **Flathub is outside the maintained release process.**

## Reason

The defect is specific to the Snap build and promotion process, not to v4.0.12
itself. The post-release development bump (`326f21a`) advanced the executable
identity to 4.0.13 but left `snap/snapcraft.yaml` at 4.0.12. Every subsequent
default-branch push caused the connected Snap Store/Launchpad build service to
publish a new `latest/edge` revision whose Snap metadata reported `4.0.12`
while the bundled executable reported `Nift v4.0.13`. The promotion coordinator
selects revisions by Store-declared version metadata alone, so a metadata-only
"4.0.12" selection could have promoted a binary that is actually 4.0.13.

## Consequences

- v4.0.12 will be released through the repaired Snap pipeline only as the
  successor release (v4.0.13).
- Historical Snap revisions 802–822 (the genuine v4.0.12 builds) are not
  eligible for promotion without an authoritative manifest proving their
  source commit, per-architecture digest, metadata version, and
  executable-reported version.

## Authority

Recorded after review by Nick and Codex. See `RELEASES.md` and `PACKAGING.md`
for the operational version of this policy.