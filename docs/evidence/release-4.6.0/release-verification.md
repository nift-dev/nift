# Nift v4.6.0 release verification

Date: 2026-10-05

## Immutable identities

- Certified product/evidence commit and tag target:
  `bb6e9f2a218b153919fa7186e6e20c38f9f6ce75`.
- Annotated tag object: `31fd56cdf34ca4f6dbc13a036f1250523dde8be6`.
- Tag: `v4.6.0` (annotated, message `Nift v4.6.0`).
- Public release: <https://github.com/nift-dev/nift/releases/tag/v4.6.0>;
  published 2026-10-05T12:41:10Z, non-draft and non-prerelease.

The candidate is documentation/evidence-consistent with the last runtime/build
source change `85960d1`: `git diff 85960d1..bb6e9f2 -- src include Makefile snap
packaging` is empty. The only changes between the previous candidate `6ee0499`
and `bb6e9f2` are the reviewed release notes and this release-notes evidence.

## Phase 1 rehearsal

Release artifacts rehearsal 4.6.0: PASS — run #37301607974 — candidate SHA bb6e9f2.

Non-publishing dispatch rehearsal:
<https://github.com/nift-dev/nift/actions/runs/37301607974> — version
consistency, Linux/macOS/Windows archives, installer and public-installer
preflights, and the aggregate rehearsal passed; publication and public smoke
jobs skipped by design.

## Published workflow

Release artifacts run #37310682044
(<https://github.com/nift-dev/nift/actions/runs/37310682044>) passed at
`bb6e9f2`:

- version consistency: PASS;
- Linux x86-64 archive and extracted native smoke: PASS;
- macOS arm64 archive and extracted native smoke: PASS;
- macOS x86-64 archive and extracted native smoke: PASS;
- Windows x86-64 archive and extracted native smoke: PASS;
- installer and public-installer preflights: PASS;
- GitHub release publication: PASS;
- public installation: PASS on Linux x86-64, macOS arm64, and macOS x86-64.

## Independent post-publication verification

All five assets were downloaded from the public release and verified:

- `sha256sum -c SHA256SUMS` passed for all four archives;
- each archive was extracted and its embedded executable reports `Nift v4.6.0`
  (Linux x86-64, macOS arm64, macOS x86-64, Windows x86-64);
- the extracted Linux binary ran `init`, `build --all` and `status` cleanly on a
  fresh project;
- the live `https://nift.dev/install` is byte-identical to the tagged
  `packaging/install.sh`, which is unchanged since v4.5.0.

## Definitive public assets and checksums

- `nift-4.6.0-linux-x86_64.tar.gz` — `46a6418c979e95bbecdb79ac40a846128f48cc30db310e2137c83afc70a9097e`
- `nift-4.6.0-macos-arm64.tar.gz` — `78485d033ebbec18f1432d206b0dc54ffb1fddf1a0caf70fa57a244cbc3f4946`
- `nift-4.6.0-macos-x86_64.tar.gz` — `402287d107fd734fed2159c7cff9c6437e29688e6fcb6a973f7fa592aa82cbb0`
- `nift-4.6.0-windows-x86_64.zip` — `be02b44946fb1f96f2c78804f89c2b258f640bd3e0989ab5a978336f0a388106`
- `SHA256SUMS` — 386 bytes.

The release contains exactly those five assets. Reviewed release body:
`docs/evidence/release-4.6.0/release-notes-4.6.0.md`.

## Independent regression contract

`nift-regression-suite` `cb51794`, 92/92 modules PASS, run #37301554461 against
Nift `bb6e9f2`.

## Website

- Source (`nift-dev.github.io`, branch `stage`): `da64cdd`; the five v4.6.0
  `PENDING-WEBSITE` items are complete. Committed locally, not pushed.
- Generated `public/` rebuilt from `da64cdd` with the released v4.6.0 binary
  (104 files built); the nested `public` checkout HEAD is `95e57e0` with the
  rebuild prepared but not committed.
- Published: **no.** `nift-dev.github.io/HANDOVER.md` requires explicit approval
  to commit/push/publish either branch, with the documented ordering (generated
  `public` on `main` first, then source on `stage`). No publication approval was
  given for v4.6.0, so none was performed.

## Immutability

The GitHub release is immutable. Its tag target is
`bb6e9f2a218b153919fa7186e6e20c38f9f6ce75`; release workflow #37310682044 and
all archive checksums above remain unchanged. No asset was modified or replaced.
The development identity remains `Nift v4.6.0`; no development bump occurred.

Snap (maintainer-operated), Homebrew (asynchronous downstream) and Chocolatey
(Phase 3, agent-owned but not yet started) are recorded as separate follow-ups.
Phase 3 requires explicit authorization.
