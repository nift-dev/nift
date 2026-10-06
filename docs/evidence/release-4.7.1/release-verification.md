# Nift v4.7.1 release verification

Date: 2026-10-06

## Immutable identities

- Certified candidate and tag target:
  `8eeeb67a21ccd5a4e6d6523346738a66ed7a3477`.
- Annotated tag object: `434d2a2f20e40acdfa09cef1de66a50ada3c8b14`.
- Tag: `v4.7.1` (annotated, message `Nift v4.7.1`).
- Public release: <https://github.com/nift-dev/nift/releases/tag/v4.7.1>;
  published 2026-10-06T06:21:34Z, non-draft and non-prerelease.

The candidate applies a warning-clean patch and two release-infrastructure
changes over the released v4.7.0 (tag `v4.7.0` at `e31cd00`): the compiler
warning cleanup, the strict `make test-warnings` first-party gate wired into the
release workflow and Deep guards, the 4.7.1 version transition, and the
reviewed v4.7.1 release notes (corrected once, before tagging, to describe the
complete four-warning cleanup; this forced a re-certification of the notes-only
candidate `8eeeb67`).

## Phase 1

- Release artifacts rehearsal: PASS — run #37416322154 — candidate SHA
  `8eeeb67` (all jobs including the new `First-party warnings as errors
  (GCC + Clang)` job and the `rehearse` aggregate).
- Exact-SHA hosted walls (all success on `8eeeb67`):
  Deep guards #37416319341; Test integrity #37416292801; Gate 6A-R
  #37416310060; Gate 6B bytes #37416313203; packaging matrix #37416298492;
  v4.4/v4.5 cross-platform #37416307190; Checkpoint 10 #37416295554; Init
  targets #37416301204; Performance regression #37416304189; hosted
  certification diagnostic #37416316222.
- Independent external regression contract: `nift-regression-suite` `ba99df5`,
  run #37416325223, **93/93 modules PASS** against Nift `8eeeb67`.
- Local: `make test` PASS; `make test-warnings` PASS under GCC and Clang;
  v47 deep-expression parser hardening PASS; deep-capable sanitizer (v47 +
  AST differential) PASS; lifetime sanitizer (`make test-sanitize-lifetime`)
  PASS; runtime matrix unchanged; clean GCC and Clang builds emit zero
  first-party warnings.
- First-party warning cleanup: v4.7.0 carried two `-Wmisleading-indentation`
  sites and a dead `find_binary` in `ParserExpression.cpp`, plus (found by the
  new strict gate under the hosted Clang toolchain) a
  `-Wtautological-compare` in `ParserHelpers.cpp`. v4.7.1 removes all four; no
  parser, language or runtime behavior changed.

## First-party warnings-as-errors release gate (new invariant)

`make test-warnings` compiles every Nift-owned `src/` translation unit with
`-Wall -Wextra -pedantic -Werror` (GCC and Clang) and is a dependency of both
the `publish` and `rehearse` jobs in the release workflow and of the Deep
guards workflow. A first-party warning-bearing candidate cannot pass the
rehearsal or be published:

```text
A RELEASE CANDIDATE IS NOT GREEN IF FIRST-PARTY NIFT CODE PRODUCES
COMPILER WARNINGS ON A SUPPORTED RELEASE TOOLCHAIN.
```

## Phase 2 published workflow

Release artifacts run #37422842656 passed at tag `v4.7.1`:

- version consistency: PASS;
- First-party warnings as errors (GCC + Clang): PASS;
- Linux x86-64, macOS arm64, macOS x86-64, Windows x86-64 archive + extracted
  native smoke: PASS;
- installer and public-installer preflights: PASS;
- GitHub release publication: PASS;
- public installation: PASS on Linux x86-64, macOS arm64 and macOS x86-64.

## Definitive public assets and checksums

The tag-triggered workflow rebuilds the archives; the values below are the
actual published checksums (independently downloaded and `sha256sum -c`
verified), not the rehearsal checksums.

- `nift-4.7.1-linux-x86_64.tar.gz` — `3137159d9b5e0c203496a65135127c77a54eb8529b86d751e846fca7706a952f`
- `nift-4.7.1-macos-arm64.tar.gz` — `b672ba1834c18eda875c0669a69c2f179ee619af7995b4ee41b308da6d558c4e`
- `nift-4.7.1-macos-x86_64.tar.gz` — `ed5dd9ba471a1c52b28eca4290f01390f090886db9b4202fa3fa38054ff91886`
- `nift-4.7.1-windows-x86_64.zip` — `01aad88a7866324a488e5a6c200fd3114cfe9a5d79dec70abe8ef1eae76e9a4c`
- `SHA256SUMS` — 386 bytes.

The release contains exactly those five assets. Each archive was extracted and
its embedded executable reports `Nift v4.7.1` (Linux x86-64, macOS arm64, macOS
x86-64, Windows x86-64). The extracted Linux binary ran `init`, `build --all`
and `status` cleanly on a fresh project. The public installer installed
`NIFT_VERSION=4.7.1` into a fresh location and the installed executable reports
`Nift v4.7.1` and passes the same smoke; `https://nift.dev/install` remains
byte-identical to `packaging/install.sh`.

## Compatibility

- Warning-only patch; language/API semantics as documented in the reviewed
  release notes (`docs/evidence/release-4.7.1/release-notes-4.7.1.md`); no
  language or public API change.
- C ABI: **1.3** (unchanged; no embedding ABI bump in v4.7.1).

## Immutability

The GitHub release is immutable. Its tag target is
`8eeeb67a21ccd5a4e6d6523346738a66ed7a3477`; release workflow #37422842656 and
all published archive checksums above remain unchanged. No asset was modified or
replaced. This post-publication evidence does not move the tag.

Snap (maintainer-operated), Homebrew (automatic downstream) and Chocolatey
(Phase 3, not started) are separate, non-blocking follow-ups.