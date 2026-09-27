# Nift v4.5.0 release verification

Date: 2026-09-27

## Immutable identities

- Certified product commit: `dc61e9a47f0988b69ad070355aa64192ce0cbf3d`.
- Release-policy and reviewed-notes commit: `560863b664c482802b8df1206c63723bb1078e51`.
- Corrected annotated tag object: `9f78142e566a88e4679bed6c8e7ce1e71d6b658a`.
- Corrected tag target: `560863b664c482802b8df1206c63723bb1078e51`.
- Public release: <https://github.com/nift-dev/nift/releases/tag/v4.5.0>;
  published 2026-09-27T05:24:22Z, non-draft and non-prerelease.

No product implementation changed between the certified product commit and tag
target. The intervening changes add closeout documentation, reviewed release
notes, the release-process guard, and its regression test.

## Pre-publication recovery

Initial tag-triggered run #44 (`36296014337`) built and rehearsed all native
archives but failed closed at publication because the reviewed release-notes
file was not committed. It created no GitHub release or release assets. Under
explicit one-time authorization, the unpublished tag object
`88bd1e2bbb85ff2685c562c1cb983aa75ba7a56e` (target `467782e`) was deleted and
replaced only after the missing notes and durable rehearsal guard were committed.

Release artifacts rehearsal 4.5.0: PASS — run #45 — SHA `560863b664c482802b8df1206c63723bb1078e51`.

Run #45 (`36296730412`) was a complete non-publishing rehearsal: version,
Linux, both macOS architectures, Windows, installer preflights, tracked notes,
exact asset set, checksums, and aggregate rehearsal passed; publication and
public smoke jobs skipped by design.

## Published workflow

Release artifacts run #46 (`36296984201`) passed at SHA `560863b`:

- version consistency: PASS;
- Linux x86-64 archive and extracted native smoke: PASS;
- macOS arm64 archive and extracted native smoke: PASS;
- macOS x86-64 archive and extracted native smoke: PASS;
- Windows x86-64 archive and extracted native smoke: PASS;
- installer and public-installer preflights: PASS;
- GitHub release publication: PASS;
- public installation: PASS on Linux x86-64, macOS arm64, and macOS x86-64.

The native archive jobs checked `Nift v4.5.0` on their respective runners. An
independent post-publication download extracted all four archives, confirmed the
expected executable formats/layouts, and executed the Linux binary, which
reported `Nift v4.5.0`.

## Definitive public assets and checksums

- `nift-4.5.0-linux-x86_64.tar.gz` — `856fcc401333aced5c492b92050caf6252cfec3fb9d15a51b2676af363599694`
- `nift-4.5.0-macos-arm64.tar.gz` — `edde7d16a183b589c90fca6da7abd294d6981852a0fe4dbc89a4340009ee8b53`
- `nift-4.5.0-macos-x86_64.tar.gz` — `a50eb5d04b3fdd8e915a66325198a6a677f58f05084ce8a62100cc97616e15c4`
- `nift-4.5.0-windows-x86_64.zip` — `10d4a543c5c4d356a5a2f566f507e16ba0859749dcc0301ae06029ae81f844b3`
- `SHA256SUMS` — 386 bytes.

The release contains exactly those five assets. All four independently
downloaded archives passed `sha256sum -c SHA256SUMS`.

## Phase boundary

Phase 2 is complete. No manual Snap, Chocolatey, or Homebrew publication was
performed, and the development version was not advanced. Those remain separate
Phase 3 work requiring explicit authorization.
