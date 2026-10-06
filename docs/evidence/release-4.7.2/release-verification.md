# Nift v4.7.2 release verification

Date: 2026-10-06

## Immutable identities

- Certified candidate and tag target:
  `495012966756c9d2910a714c453359c8ff60bab1`.
- Annotated tag object: `becae31dba39e5250478ae8d76acc8fe71573ce6`.
- Tag: `v4.7.2` (annotated, message `Nift v4.7.2`).
- Public release: <https://github.com/nift-dev/nift/releases/tag/v4.7.2>;
  published 2026-10-06T16:56:41Z, non-draft and non-prerelease.

The candidate fixes a prepared-execution regression present in v4.7.1 and adds
the version transition plus the reviewed v4.7.2 release notes.

Note: an earlier Phase 1 report carried an incorrect full-SHA string
(`4950129e56…`). That string is not a real Git object and never received any
certification; it was used only in two `workflow_dispatch` contract attempts that
failed at `actions/checkout` fetching `refs/heads/<sha>`. The authoritative
identity (this commit at Nift `origin/main`) is the object every Phase 1 wall,
the 94/94 contract run, and the rehearsal actually certified.

## Phase 1

- Release artifacts rehearsal: PASS — run #37487025928 — candidate
  `4950129` (all jobs including the `First-party warnings as errors
  (GCC + Clang)` job and the `rehearse` aggregate).
- Exact-SHA hosted walls (success on `4950129`):
  Deep guards #37487020716; Test integrity #37486916970; Gate 6A-R
  #37486916891; Gate 6B bytes #37486916857; packaging matrix #37486916757
  (a duplicate dispatch #37486978321 hit a transient macOS-arm64 runner
  checksum failure; the exact-SHA rerun #37490894259 passed); v4.4/v4.5
  cross-platform #37486916900; Checkpoint 10 #37486916826; Init targets
  #37486917052; Performance regression #37486917125; hosted certification
  diagnostic #37486916767; Release artifacts #37487025928.
- Independent external contract: `nift-regression-suite` `cf34587` (94 modules,
  93→94), run #37486924459, **94/94 PASS** against Nift `4950129` (clean
  checkouts). Dispatch attempts #37487031901 / #37490887751 failed at
  `actions/checkout` (infra quirk: `refs/heads/<full-sha>`); the push-triggered
  exact-SHA run above is authoritative.
- Local: `make test` PASS incl. `test-prepared-method-parity`; `make
  test-warnings` PASS (GCC + Clang); deep sanitizer (v47 deep expression, v44
  AST differential, prepared-parity test) PASS; lifetime sanitizer PASS; runtime
  matrix has no material regression; `nift-packages/diff` and `nift-packages/mdx`
  pass unchanged.

## Prepared method-call argument parity (the v4.7.2 fix)

- Root cause: the prepared AST `postfix()` built member/method `Call` nodes
  (`obj.method(...)`) without parsing their argument expressions, so prepared
  native-method dispatch received zero arguments for argument-taking native
  methods (`string.encode`, `bytes.decode`, `bytes.slice`, atomic/timer
  setters) inside prepared loop/while/function bodies.
- Fix: `postfix()` now parses and stores method-call arguments in the prepared
  AST exactly like named calls; unsupported argument expressions retain the
  legacy-evaluator fallback.
- Prepared execution was NOT disabled; no encode/decode special-casing; no
  package workarounds.
- Core regression coverage: `tests/v472_prepared_method_parity.sh`
  (`make test-prepared-method-parity`).
- External contract: `contract/v47_prepared_method_parity_smoke.sh` (module 94).

## First-party warnings-as-errors gate (permanent invariant)

`make test-warnings` compiles every Nift-owned `src/` translation unit with
`-Wall -Wextra -pedantic -Werror` (GCC and Clang) and is a dependency of both
`publish` and `rehearse` in the release workflow and of the Deep guards
workflow:

```text
A RELEASE CANDIDATE IS NOT GREEN IF FIRST-PARTY NIFT CODE PRODUCES
COMPILER WARNINGS ON A SUPPORTED RELEASE TOOLCHAIN.
```

## Phase 2 published workflow

Release artifacts run #37499165416 passed at tag `v4.7.2`:

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

- `nift-4.7.2-linux-x86_64.tar.gz` — `333f5d7fd3a464617ae31afd1ae96f27d6676631e8cc9517c3e2fae19c939730`
- `nift-4.7.2-macos-arm64.tar.gz` — `b1fe2bd104546f84b44ecde85454c46e1bae21861a65b526382e392aac427d19`
- `nift-4.7.2-macos-x86_64.tar.gz` — `6927b978b3eaa68b261d791d6f0aef874ea55c1bf4548f27173bbffd2a090967`
- `nift-4.7.2-windows-x86_64.zip` — `e71e518b2aa8c3cf5eb0db8e29966d8a9d28b0407dcfe6ff436e13afe81237e0`
- `SHA256SUMS` — 386 bytes.

The release contains exactly those five assets. Each archive was extracted and
its embedded executable reports `Nift v4.7.2` (Linux x86-64, macOS arm64, macOS
x86-64, Windows x86-64). The extracted Linux binary ran `init`, `build --all`
and `status` cleanly on a fresh project. The public installer installed
`NIFT_VERSION=4.7.2` into a fresh location and the installed executable reports
`Nift v4.7.2` and passes the same smoke; `https://nift.dev/install` remains
byte-identical to `packaging/install.sh`.

## Compatibility

- Patch fixes prepared method-call argument parity; language/API semantics as
  documented in the reviewed release notes
  (`docs/evidence/release-4.7.2/release-notes-4.7.2.md`); no language or public
  API change.
- C ABI: **1.3** (unchanged; no embedding ABI bump in v4.7.2).

## Immutability

The GitHub release is immutable. Its tag target is
`495012966756c9d2910a714c453359c8ff60bab1`; release workflow #37499165416 and
all published archive checksums above remain unchanged. No asset was modified or
replaced. This post-publication evidence does not move the tag.

Snap (maintainer-operated), Homebrew (automatic downstream) and Chocolatey
(Phase 3, not started) are separate, non-blocking follow-ups.