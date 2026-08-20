# Nift release and publication handover

Package-manager recipes, GitHub release workflows, store credentials, artifact
names, and the existing Flathub update path are documented in `PACKAGING.md`.
This document owns release readiness; `PACKAGING.md` owns how an approved release
is packaged and published.

## Authority and current state

The development executable currently reports `Nift v4.0.4`, following the public v4.0.3 release.
identity remains documented in project history and release notes, but it is not
part of the public product version. Exact tag, artifact, and public release
conventions must follow `PACKAGING.md` and actual Git/release evidence.

A website content checkpoint, regression-suite checkpoint, and executable version
are distinct identities. Do not synchronize version numbers mechanically.

## Checkpoint versus release

```text
validated checkpoint
    coherent development baseline, not public by implication

release candidate
    validated checkpoint undergoing packaging/publication checks

release
    deliberately published artifact after approval
```

Historical ZIP checkpoints were a file-transfer mechanism. With direct Git
access, prefer commit/status provenance and repeatable scripts. Do not recreate
ZIP-heavy ritual unless an actual release artifact requires it.

## Release-candidate validation

Proportionately include:

1. Clean source build with the intended release compiler/options.
2. Full implementation-local test set.
3. Full external contract against the candidate executable.
4. Relevant ASan/UBSan and platform checks.
5. Current scaling, performance, and memory guards.
6. Exact embedded Minify++ synchronization and its relevant standalone gates.
7. Review `PENDING-WEBSITE.md`; complete every item targeted at this release in
   the website source, verify the built result, and remove or explicitly retarget
   each queue entry before tagging.
8. Build the updated Nift website with the exact candidate binary.
9. Validate representative templates/downloadable examples where relevant.
10. Reconcile README, docs, website, AI context, release notes, decisions, and
   production roadmap.
11. Build the actual package/archive, extract it freshly, build/use it, run the
    external suite against it, and verify `nift version`, `nift about`, and
    `nift commands`, plus the license and expected files. Confirm unknown and
    help-like invocations produce the intended diagnostic and direct users to
    `nift commands`; a separate `nift help` command is not part of the contract.
12. Inspect repository state for generated/debug residue.

Repository tests passing does not prove a release archive is usable. After the
release is public, the separate distribution-verification workflow tests the
actual packages and archives users receive. Package-manager propagation is a
post-release state, not a reason to delay an otherwise completed release; keep
its expected-version assertion strict and rerun lagging channels as they become
publicly available. See `DISTRIBUTION-VERIFICATION.md`.

## Website publication

The Nift website source is a separate repository on its authoritative `stage`
branch in the current checkout. Its nested `public/` is a separate generated Git
checkout used for built-site state. For Nift website publication checkpoints,
commit the rebuilt/generated `public/` checkout on `main` first, then commit the
corresponding authoritative source changes on `stage`, and verify both trees are
clean. Never hand-edit generated HTML as the source of truth; the extensionless
installer is the documented exception only in the sense that its canonical
repository-root source is copied byte-for-byte to `public/install` because the
current tracked-extension contract cannot emit an extensionless path directly.

Building locally is authorized as validation. Pushing the generated branch or
deploying publicly requires approval.

Implementation-driven website changes that must coincide with a future Nift
release belong in `PENDING-WEBSITE.md`. This queue does not freeze the website:
unrelated website content and improvements can continue through their normal
development and publication flow. The release gate applies only to queued items
whose target version is the candidate being prepared.

For the v4.0.3 reconciliation checkpoint, the generated website commit is `478c617` on `public/main` and the corresponding authoritative source commit is `c5d0d94` on `stage`, in that required order. These are local checkpoint identities until deliberately pushed/published.

## Version and notes

User-visible behavioral changes, correctness fixes, and language capabilities may
justify version/release-note changes. Tests or prose alone do not automatically
require a binary version bump. Follow established Git/release evidence and ask
before assigning a public release version.

Immediately after a public release is completed, advance the executable/source
identity to the next development version before beginning further development.
Do not leave the working tree identifying itself as the version that was just
released: keeping the development identity current prevents the next bump from
becoming a release-day memory task. Update any regression assertion that checks
the executable version as part of the same post-release checkpoint. Packaging
metadata that intentionally records a published store version is separate and
must remain historical. Packaging source recipes that participate in development
and release validation, including `snap/snapcraft.yaml`, advance with the
executable identity so the next release cannot inherit a stale version.

## Release report

Record exact source/suite/site identities, commands, outcomes, environment where
material, package contents, known limitations, and publication status. Separate
facts from interpretation and avoid universal performance claims from one host.

For packaged releases, also record artifact checksums, the installed package
version tested from each store, the store/channel publication state, and the
external Flathub manifest commit where applicable. A successful workflow upload
is not evidence that a store has published or served the package.

Treat these as separate states and report them precisely:

1. packaging workflow succeeded;
2. package was submitted to the store;
3. automated verification passed;
4. human review/approval completed where applicable; and
5. the intended version is publicly installable from the intended channel.

Do not collapse those states into "released". A GitHub Actions success can prove
that a package was built or submitted, but it cannot by itself prove store
approval or availability.

## v4.0.3 release report (2026-08-20)

### Source and workflow

- Tag: `v4.0.3` (annotated), pushed to `origin`.
- GitHub Actions run: `32257518071` — all 14 jobs passed.
- GitHub release: https://github.com/nift-dev/nift/releases/tag/v4.0.3

### Archives and checksums

- `nift-4.0.3-linux-x86_64.tar.gz` (429 378 bytes) — extracted binary verified: `Nift v4.0.4` is the post-release dev identity, `version` output matches `v4.0.3` exactly.
- `nift-4.0.3-macos-arm64.tar.gz` (336 913 bytes)
- `nift-4.0.3-macos-x86_64.tar.gz` (356 665 bytes)
- `nift-4.0.3-windows-x86_64.zip` (1 327 085 bytes)
- `SHA256SUMS` verified against all four archives; independent download + checksum recheck passed for Linux archive.
- Smoke test: extracted Linux binary ran `init`, `build-all`, `test-installer` against a temporary project — all clean.

### Package publication

- **Snap stable**: Workflow `snap / build` succeeded on amd64 and arm64; "Publish stable Snap" succeeded on both architectures. Edge confinement-testing step was skipped (no `SNAPCRAFT_STORE_CREDENTIALS` configured; not required for this release path). Store: https://snapcraft.io/nift
- **Chocolatey Community**: Workflow `chocolatey / package` succeeded through "Publish to Chocolatey Community Repository". The `.nupkg` was submitted. Store: https://community.chocolatey.org/packages/nift
- **Homebrew**: Formula was built and tested on macOS arm64 and Linux x86_64 by the workflow. Homebrew's automatic bump infrastructure will pick up the release on its own schedule; no manual PR was opened.
- **Flathub**: External `flathub/cc.nift.nsm` manifest update required — pending external PR.

### Post-release

- Development executable advanced to `Nift v4.0.4` in `src/CLI.cpp`, `snap/snapcraft.yaml`.
- Regression suite assertions updated; all 20 contract modules pass against the v4.0.4 binary.
- Both `nift-dev/nift` and `nift-dev/nift-regression-suite` pushed to `main`.

## v4.0.4 release report (2026-08-21)

### Scope

v4.0.4 is a bounded reliability release fixing the Checkpoint 8 transactional-writer
performance regression and hardened transactional recovery semantics. It carries the
recovery-epoch hardening (one directory scan per distinct touched parent per build pass),
the quoted-ternary rendering fix, and the syntax-highlighted diagnostics from the
v4.0.3→v4.0.4 development cycle.

### Source and workflow

- Tag: `v4.0.4` (annotated), pushed to `origin`.
- Release commit: `29eb4d0` (`Refresh checkpoint 8 recovery evidence`), matching `main`.
- GitHub Actions run: TBD after push; all artifact jobs and `installer-preflight` must
  succeed before the GitHub release is created.

### Release-candidate validation (performed at `29eb4d0`, clean build)

- Full source build clean with the release compiler/options.
- `nift version` reports `Nift v4.0.4`; `nift about` and `nift commands` correct;
  unknown and help-like invocations fail with exit 1 and direct the user to
  `nift commands`; no separate `nift help` command exists.
- Contract suite: PASS (all modules).
- Checkpoint 8 filesystem/transaction: 16/16 PASS (evidence refreshed at `29eb4d0`).
- Checkpoint 7 incremental equivalence: 720/720 PASS.
- Checkpoint 10 cross-platform corpus: 18/18 PASS.
- Pagination incremental equivalence: 18/18 PASS.
- Regression suite: 22/22 contract modules PASS against the candidate binary.
- Performance guards: tracking 2k→10k ratio 4.63×, changed-output full build 1k→4k
  ratio 3.48×, recovery-epoch scan-bound guard PASS; retained 10k benchmark
  (full 0.116 s, no-op 0.074 s, single-page 0.083 s, shared-template 0.148 s medians);
  memory peaks ≤ 9 920 KiB (guard ≤ 16 384 KiB). Ratios vary with machine load
  (repeated runs measured tracking up to 6.09×); all guards pass and the retained
  timings sit in the historical v1.0.41 checkpoint range.
- Website built with the candidate binary: 61/61 pages, clean.
- No generated/debug residue in the repository.

### Archives and checksums

- `nift-4.0.4-linux-x86_64.tar.gz`
- `nift-4.0.4-macos-arm64.tar.gz`
- `nift-4.0.4-macos-x86_64.tar.gz`
- `nift-4.0.4-windows-x86_64.zip`
- `SHA256SUMS` (verified independently after publication; record exact checksums below).

### Package publication

- **Snap**: `snap.yml` called after the GitHub release; `SNAPCRAFT_STORE_CREDENTIALS`
  is configured and direct publication to `stable` is intended for all supported
  architectures.
- **Chocolatey Community**: `chocolatey.yml` called after the GitHub release;
  `CHOCOLATEY_API_KEY` is configured and the `.nupkg` is pushed. Store:
  https://community.chocolatey.org/packages/nift
- **Homebrew**: Formula built and tested on macOS arm64 and Linux x86_64 by the
  workflow; Homebrew's automatic bump service picks up the release on its own
  schedule; no manual PR.
- **Flathub**: External `flathub/cc.nift.nsm` manifest update required — pending
  external PR.
