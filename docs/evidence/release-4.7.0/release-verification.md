# Nift v4.7.0 release verification

Date: 2026-10-06

## Immutable identities

- Certified candidate and tag target:
  `e31cd00e5f741393dc27ebecd5f1cc2f4bbd89f3`.
- Annotated tag object: `715cbaaa790ae7c16e835217633a5d40a12fff9c`.
- Tag: `v4.7.0` (annotated, message `Nift v4.7.0`).
- Public release: <https://github.com/nift-dev/nift/releases/tag/v4.7.0>;
  published 2026-10-06T00:57:13Z, non-draft and non-prerelease.

The candidate is the reviewed release-preparation commit: it adds the release
notes below and the matching `ReleaseNotes.md` section on top of the runtime /
parser work at `b0f6d8a`. Runtime/build source is unchanged from the
locally-tested stack; the documentation/evidence change was covered by the
complete exact-SHA hosted Phase 1 walls and the release-artifacts rehearsal.

## Phase 1

- Release artifacts rehearsal: PASS — run #37389427143 — candidate SHA
  `e31cd00`.
- Exact-SHA hosted walls (all success on `e31cd00`):
  Deep guards #37389430559; Test integrity #37389354508; Gate 6A-R
  #37389353824; Gate 6B bytes #37389354316; packaging matrix #37389354269;
  v4.4/v4.5 cross-platform #37389355220; Checkpoint 10 #37389353948; Init
  targets #37389353965; Performance regression #37389354428; hosted
  certification diagnostic #37389354174.
- Independent external regression contract: `nift-regression-suite` `ba99df5`,
  run #37389398163, **93/93 modules PASS** against Nift `e31cd00`.
- Local: `make test` PASS; deep-capable sanitizer (v47 deep-expression incl.
  nesting-64, recursion guard, AST differential) PASS; lifetime sanitizer
  (`make test-sanitize-lifetime`, use-after-scope confirmed by canary) PASS.
- Public installer: `https://nift.dev/install` byte-identical to
  `packaging/install.sh` (unchanged since v4.6.0).

## Phase 2 published workflow

Release artifacts run #37396227898 passed at `e31cd00`:

- version consistency: PASS;
- Linux x86-64, macOS arm64, macOS x86-64, Windows x86-64 archive + extracted
  native smoke: PASS;
- installer and public-installer preflights: PASS;
- GitHub release publication: PASS;
- public installation: PASS on Linux x86-64, macOS arm64 and macOS x86-64.

## Definitive public assets and checksums

The tag-triggered workflow rebuilds the archives; the values below are the
actual published checksums (independently downloaded and `sha256sum -c`
verified), not the rehearsal checksums.

- `nift-4.7.0-linux-x86_64.tar.gz` — `29d169b81273ba83b22c5f6d2dde060579db88d5f1111e3dde3cbdef58c35557`
- `nift-4.7.0-macos-arm64.tar.gz` — `298115198cd6c58b05ad36660d8e61d2598e4ba1f0c04a358582f6fd69238b17`
- `nift-4.7.0-macos-x86_64.tar.gz` — `499c09b8d7c2e5cc0756cfa07889110480a6c9a64a34e66420f7c0f726982a62`
- `nift-4.7.0-windows-x86_64.zip` — `03d4197d904a0fe989c680534d6ef323e6de338b0e92d73fc43e92f7caaee90e`
- `SHA256SUMS` — 386 bytes.

The release contains exactly those five assets. Each archive was extracted and
its embedded executable reports `Nift v4.7.0` (Linux x86-64, macOS arm64, macOS
x86-64, Windows x86-64). The extracted Linux binary ran `init`, `build --all`
and `status` cleanly on a fresh project. The public installer installed
`NIFT_VERSION=4.7.0` into a fresh location and the installed executable reports
`Nift v4.7.0` and passes the same smoke.

## Compatibility

- Language/API semantics: as documented in the reviewed release notes
  (`docs/evidence/release-4.7.0/release-notes-4.7.0.md`); no language or public
  API change.
- C ABI: **1.3** (unchanged; no embedding ABI bump in v4.7.0).

## Immutability

The GitHub release is immutable. Its tag target is
`e31cd00e5f741393dc27ebecd5f1cc2f4bbd89f3`; release workflow #37396227898 and
all published archive checksums above remain unchanged. No asset was modified or
replaced. This post-publication evidence does not move the tag.

Snap (maintainer-operated), Homebrew (automatic downstream) and Chocolatey
(Phase 3, not started) are separate, non-blocking follow-ups.
