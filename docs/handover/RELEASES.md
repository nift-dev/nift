# Nift release and publication handover

Package-manager recipes, GitHub release workflows, store credentials, artifact
names, and the existing Flathub update path are documented in `PACKAGING.md`.
This document owns release readiness; `PACKAGING.md` owns how an approved release
is packaged and published.

## Authority and current state

The development executable currently reports `Nift v4.0.9`, following the
public v4.0.8 release. The exact executable identity remains documented in
project history and release notes, but it is not part of the public product
version. Exact tag, artifact, and public release conventions must follow
`PACKAGING.md` and actual Git/release evidence.

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
13. **Pre-tag public-installer deployment gate.** If `packaging/install.sh` (or
    any public update script Nift may later gain) has changed since the previous
    public release, the exact reviewed script must already be deployed at
    `https://nift.dev/install` and byte-verified *before* the release tag is
    created. The required sequence is:

    ```text
    installer change detected
    → website source updated
    → generated website rebuilt
    → public website deployed
    → public installer downloaded
    → bytes/checksum verified
    → public installer candidate smoke passes
    → release go/no-go
    → tag created
    → release published
    → public installer tested against released artifacts
    ```

    Three gates protect the installer path when it has changed. They are
    distinct and all required:

    - **Pre-tag process gate.** The mandatory non-publishing rehearsal must
      pass before tag authorization: the rehearsal aggregate depends on
      `release.yml`'s `public-installer-preflight` job (which byte-compares
      `https://nift.dev/install` with `packaging/install.sh`, records both
      SHA-256 values, and runs `sh -n`), and the go/no-go review refuses to
      authorize tagging unless that rehearsal passes. The tag-triggered
      workflow runs only after the tag is pushed, so it cannot by itself
      prevent tag creation.
    - **Publication gate.** The tag-triggered `release.yml` rechecks equality:
      `public-installer-preflight` is a dependency of the `publish` job, so a
      stale public installer fails the workflow fail-closed and publication
      does not proceed, even if the invariant was violated between rehearsal
      and tagging.
    - **Post-publication gate.** The retained `installer-public-smoke` jobs
      prove the same live public installer actually installs the newly
      published artifacts (equality alone does not prove the script can install
      them).

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
- Release commit: `29eb4d0` (`Refresh checkpoint 8 recovery evidence`).
- GitHub Actions run: `32392530246` — all 13 jobs passed (unix linux/macos-arm64/macos-x86_64, windows, installer-preflight, publish, installer-public-smoke ×3, snap build ×2, chocolatey, homebrew ×2).
- GitHub release: https://github.com/nift-dev/nift/releases/tag/v4.0.4

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

- `nift-4.0.4-linux-x86_64.tar.gz` (436 896 bytes) — extracted binary verified: `Nift v4.0.4`; fresh-project `init`/`build`/`status` smoke passed.
- `nift-4.0.4-macos-arm64.tar.gz` (343 783 bytes)
- `nift-4.0.4-macos-x86_64.tar.gz` (363 737 bytes)
- `nift-4.0.4-windows-x86_64.zip` (1 331 823 bytes)
- `SHA256SUMS` (verified independently against all four public archives):
  - `64b1a205cb4607702dc2bf1a2c886893c6377bfe1667862ecbbd390ea7e17acd` linux-x86_64
  - `e94e29541fa9838d98c108035a75e210e9634e9e509700b9e9c680cc5ddbeefd` macos-arm64
  - `81d7c95b80c528502e4b84093b74e72caf89d633e3def71fdac720a614fcfb5b` macos-x86_64
  - `b4ddb5d8f57b05e9ebdd8ea65b82143baa7cc3798b96abad70f98f2b89c0d0b8` windows-x86_64

### Package publication

- **Snap**: `snap / build` succeeded on amd64 and arm64; "Publish stable Snap" succeeded on both architectures. `snap info nift` reports `latest/stable: 4.0.4 (2026-08-20, rev 543)`. Other supported architectures (armhf, ppc64el, riscv64, s390x) remain on the connected Snap Store/Launchpad path; verify `snap info nift` on those before declaring full availability.
- **Chocolatey Community**: `chocolatey / package` succeeded through "Publish to Chocolatey Community Repository". The public package page returns 200 and the `.nupkg` downloads for `nift/4.0.4`; its embedded `VERIFICATION.txt` and `chocolateyInstall.ps1` reference the v4.0.4 release URL with SHA-256 `b4ddb5d8…` matching the published Windows archive. Workflow success and downloadable package mean submitted, not yet approved by community moderation or publicly installable.
- **Homebrew**: Formula was built and tested on macOS arm64 and Linux x86_64 by the workflow. The canonical `Homebrew/homebrew-core` formula still publishes v4.0.3; Homebrew's automatic bump service picks up releases on its own schedule. No manual PR was opened.
- **Flathub**: External `flathub/cc.nift.nsm` manifest update required — pending external PR.

### Post-release

- Development executable advanced to `Nift v4.0.5` in `src/CLI.cpp`, `snap/snapcraft.yaml`, and release notes.
- Regression suite assertions updated to the v4.0.5 development identity.
- Remaining downstream states to track: Homebrew auto-bump merge + fresh install, Chocolatey moderation/approval, Flathub external PR, Snap non-x86 architectures.

## v4.0.5 release report

### Scope

v4.0.5 is a public reliability/release-hardening release. There are no
intentional user-facing semantic changes; it incorporates the completed
battle-hardening campaign (test-integrity/enforcement architecture, curated
guard mutation and test-of-test, recovery/filesystem validation,
cross-platform evidence, and public-truth reconciliation). The only `src/`
change since v4.0.4 is the development-version bump.

### Source and workflow

- Release-candidate validation performed at `fb30c1b` (release-notes framing
  commit). The tagged commit `8bb10f2` adds this release report (documentation
  only) on top of the validated product state.
- Tag: `v4.0.5` (annotated) at `8bb10f2`; pushed to `origin`.
- GitHub Actions run: `32462270055` — all 15 jobs passed (linux, macos-arm64,
  macos-x86_64, windows, installer-preflight, publish, installer-public-smoke ×3,
  homebrew ×2, chocolatey, snap ×2).
- GitHub release: https://github.com/nift-dev/nift/releases/tag/v4.0.5

### Archives and checksums

- `nift-4.0.5-linux-x86_64.tar.gz` (436 896 bytes) — extracted binary verified:
  `Nift v4.0.5`; fresh-project `init`/`build`/`status` smoke passed.
- `nift-4.0.5-macos-arm64.tar.gz` (343 785 bytes)
- `nift-4.0.5-macos-x86_64.tar.gz` (363 735 bytes)
- `nift-4.0.5-windows-x86_64.zip` (1 331 823 bytes)
- `SHA256SUMS` (386 bytes), independently downloaded and verified against all
  four public archives:
  - `9d18ace1f939cc3bf62f57a1e75f89b0d5f7909ba6199f04e1b24c5615d745c8` linux-x86_64
  - `14dff81a8269a43ee8672a1074804c97e79106d85367ee52f07f36dcd2dd4f80` macos-arm64
  - `adabeaf9b2722ca947812339988267031db4c18a7425a47fa9d4949e14978233` macos-x86_64
  - `f7898f0ee7f6c4887bcf84c4ed6a6bad6318dddcd590355830eaf1cdb3ee5439` windows-x86_64
- The GitHub release body mirrors the user-facing v4.0.5 framing (reliability
  and release hardening; no intentional user-facing semantic changes).

### Release-candidate validation (performed at `fb30c1b`, clean build)

- Clean source build clean with release compiler/options (g++ C++17 -O2).
- `nift version` reports `Nift v4.0.5`; `about`/`commands` correct; unknown and
  help-like invocations fail with exit 1 and direct users to `nift commands`.
- Implementation-local correctness: 19/19 targets PASS (jsonic-sync not run
  locally — requires external `JSONIC_DIR` checkout).
- BH2 CI-equivalent chain PASS: integrity scanner 50 files / 0 findings;
  registry CI (26 guarantees / 27 claims / 3 discrepancies / 20 CI refs);
  contracts; pagination 18/18; init targets; BH4 incremental transitions;
  BH5 parser/value 15/15; BH6 init functional truth; BH7 crash recovery;
  BH8 complexity invariants; BH9 filesystem boundary.
- BH3 repair battery: `all_red_correct: True` (real Nift GREEN, all injected
  faulty implementations RED).
- Full 3-repository registry audit PASS; BH1 liveness PASS.
- Minify++ standalone gates PASS (15,459-program generated JS corpus, 180 JSX,
  CSS, formats, CLI, 70,000-case fuzz).
- Performance guards: tracking 4.01x, full-build 3.52x ratios; recovery-epoch
  scan-bound PASS. External regression harness: tracking 4.00x, full-build
  3.42x; 10k full build 0.108 s, no-op 0.073 s medians.
- Memory guard: all 10k peaks ≤ 9,948 KiB (guard ≤ 16,384 KiB).
- ASan/UBSan build clean; sanitized binary passed the BH5 parser battery and
  BH6 init scaffold with no findings.
- Website built with the exact candidate binary: 61/61 pages; both website
  source `stage` and generated `public/main` trees clean; corrected wording
  (15,459-program corpus, Checkpoint 8 16 cases, `ai-context.txt` wording)
  present in the generated site.
- No generated/debug residue in any of the three repositories.

### Local Linux archive layout validation

- `nift-4.0.5-linux-x86_64/` (nift, README.md, LICENSE) built and tarred;
  SHA-256 `9925c4a556d33147c769e8627e95afe26117b9148900802bb026f6035fd3d7eb`.
- Fresh extract: binary reports `Nift v4.0.5`; unknown/help diagnostics exit 1;
  fresh-project `init`/`build`/`status` smoke PASS.
- Other platform archives, the full `SHA256SUMS`, and the GitHub release are
  produced by `release.yml` on tag push (pending approval).

### Package publication

- **Snap**: `snap.yml` built and `Publish stable Snap` succeeded on amd64 and
  arm64. Public `snap info nift` reports `latest/stable: 4.0.5 (2026-08-21,
  rev 553)`. Other connected-store architectures and a fresh public stable
  install remain external verification tasks.
- **Chocolatey**: `chocolatey.yml` packaged and `Publish to Chocolatey
  Community Repository` succeeded. The public page
  `community.chocolatey.org/packages/nift/4.0.5` returns 200. Submitted, not yet
  approved or publicly fresh-install tested.
- Chocolatey automated verification initially FAILED for a spurious reason: the
  verifier's `choco install nift --version 4.0.5` query to
  `community.chocolatey.org/api/v2/Packages(Id='nift',Version='4.0.5')` returned
  503 (service unavailable). Validation passed and the package content was
  independently confirmed correct (release ZIP URL + SHA-256
  `f7898f0e…ee5439` match). Per PACKAGING.md a verifier-only resubmit of the
  exact same version is the documented remedy; resubmit run `32467758750`
  completed success on 2026-08-21, replacing the version under moderation and
  triggering a fresh automated verification.
- **Homebrew**: `homebrew.yml` tested the formula on macOS arm64 and Linux
  x86-64. Ordinary propagation is left to Homebrew's automatic bump service.
- **Flathub**: external `flathub/cc.nift.nsm` manifest tag/checksum update
  required — pending external PR.

### Post-release (completed)

- Development identity advanced to `Nift v4.0.6` in `src/CLI.cpp`,
  `snap/snapcraft.yaml`, release notes, and the guarantee registry baseline
  (released_version 4.0.5, release_commit a318732, development_version 4.0.6).
- The init-target Amplify framework version is now derived from `version_text`
  so it can never drift from the executable version again (it previously held a
  hard-coded 4.0.5).
- Regression-suite version assertions advanced to v4.0.6; 22/22 contract modules
  pass against the v4.0.6 development binary.
- Both `nift-dev/nift` and `nift-dev/nift-regression-suite` pushed to `main`.

### Remaining downstream tracking

- Homebrew auto-bump merge + fresh install; Chocolatey moderation/approval and
  fresh install; Flathub external PR; Snap non-x86 architectures.
- Run `distribution-verification.yml` against the exact public version once
  stores propagate.

## v4.0.6 release report

### Scope

v4.0.6 is the first release with intentional user-facing changes since v4.0.4.
It ships four product changes: `nift init --handover` (a canonical project-root
`HANDOVER.md`, byte-identical to the live canonical version), generated outputs
that preserve the source content file's permissions (executable script outputs
stay executable), `@pathto` root-absolute web paths when rendering the tracked
page named `404`, and loud rejection of unknown `.nift/config.json` keys.

### Source and workflow

- Release-candidate validation performed at `d0d10c9` (release-notes framing
  commit); the working tree and the release commit are exactly that state.
- Tag: `v4.0.6` (annotated) at `d0d10c9`; pushed to `origin`.
- GitHub Actions run: `32564541873` — all 14 jobs passed (unix linux-x86_64,
  macos-arm64, macos-x86_64, windows, installer-preflight, publish,
  installer-public-smoke ×3, chocolatey, snap ×2, homebrew ×2).
- GitHub release: https://github.com/nift-dev/nift/releases/tag/v4.0.6

### Archives and checksums

- `nift-4.0.6-linux-x86_64.tar.gz` — extracted binary verified: `Nift v4.0.6`;
  fresh-project `init`/`build`/`status` and `init --handover` (byte-identical
  HANDOVER.md) smoke passed.
- `nift-4.0.6-macos-arm64.tar.gz`
- `nift-4.0.6-macos-x86_64.tar.gz`
- `nift-4.0.6-windows-x86_64.zip`
- `SHA256SUMS` (386 bytes), independently downloaded and verified against all
  four public archives:
  - `c342695001f987c515bcdb7314784bc1ff0e2d5290e7d846fc9e6752de170782` linux-x86_64
  - `496f1e746219af1009e55fccfeed994050fdb35fb2477a4fc99a88ecd72f8398` macos-arm64
  - `96abb1f7d12577b3a25b416b3478e85b698e1943e272a96d0914b04e4fb28ae0` macos-x86_64
  - `6d6852060ba919c962c20c9b45502670446720846310902df8e5d23345f04b99` windows-x86_64
- The immutable tagged source archive used by source-based package managers has
  SHA-256 `8e43c1579001d8f97695bb50f2486a3ce48442797fb56d80519335312af34fbd`.

### Release-candidate validation (performed at `d0d10c9`, clean build)

- Clean source build clean with release compiler/options (g++ C++17 -O2);
  `nift version` reports `Nift v4.0.6`; `about`/`commands` correct (init row
  lists `[--handover]`); unknown and `help`-like invocations behave exactly as
  in the released v4.0.5 (a separate `nift help` command does not exist).
- Implementation-local suite: all standard correctness targets pass; bh2
  CI-equivalent chain PASS (test-integrity 55 files / 0 findings; registry
  26 guarantees / 27 claims / 3 known discrepancies / 20 CI refs; contracts;
  pagination 18/18; init targets; BH4 transitions; BH5 15/15; BH6 init truth;
  BH7 crash recovery; BH8 complexity; BH9 filesystem boundary; config
  validation; pathto-404; output permissions; init-handover).
- Full 3-repository registry audit PASS; BH1 liveness PASS.
- External regression suite: 22/22 contract modules PASS against the candidate
  (after the legacy mode-444 assertion was updated to the source-permission
  contract); historical+ruthless 578 assertions PASS. Performance harness: 10k
  full 0.113 s / no-op 0.074 s / single-page 0.091 s / shared-template 0.143 s
  medians; memory guard PASS (all peaks ≤ 9 972 KiB).
- ASan/UBSan build clean; sanitized binary + pagination sanitizer smoke PASS.
- Minify++ embedded gates PASS: 15,459-program generated JS semantic corpus,
  115-document non-JS idempotence corpus, standalone CLI, and Node semantic
  differential.
- Scaling guards PASS: full-build near-linear, recovery scans bounded to one
  per distinct parent per epoch, tracked loading near-linear.
- Website built with the exact candidate binary: 70/70 pages; the generated
  `public/main` and source `stage` trees clean; `scripts/check_handover_display.py`
  reports the rendered HANDOVER.md byte-identical to `public/HANDOVER.md`.
- No generated/debug residue in any of the three repositories.

### Local Linux archive layout validation

- `nift-4.0.6-linux-x86_64/` (nift, README.md, LICENSE) built and tarred
  locally; fresh extract reported `Nift v4.0.6` and passed a fresh-project
  `init`/`build`/`status` smoke plus `init --handover` producing the canonical
  HANDOVER.md (SHA-256 `fe8c0459…`, matching the vendored fixture).
- Other platform archives, the full `SHA256SUMS`, and the GitHub release are
  produced by `release.yml` on tag push.

### Package publication

- **Snap**: `snap.yml` built and `Publish stable Snap` succeeded on amd64 and
  arm64. Public `snap info nift` reports `latest/stable: 4.0.6 (2026-08-22,
  rev 579)`. Other connected-store architectures and a fresh public stable
  install remain external verification tasks.
- **Chocolatey**: `chocolatey.yml` packaged and `Publish to Chocolatey
  Community Repository` succeeded. The public page
  `community.chocolatey.org/packages/nift/4.0.6` returns 200. Submitted, not yet
  approved or publicly fresh-install tested.
- **Homebrew**: `homebrew.yml` tested the formula on macOS arm64 and Linux
  x86-64. Ordinary propagation is left to Homebrew's automatic bump service.
- **Flathub**: external `flathub/cc.nift.nsm` manifest tag/checksum update
  required — pending external PR.

### Cross-platform CI

- The `checkpoint-10-cross-platform.yml` corpus passed on Linux, macOS and
  Windows at `247ca84`, including the corrected `readonly-output-deletion`
  platform-specific contract (source-preserving outputs keep the portable
  read-only deletion distinction via a read-only fixture source) and the
  deterministic cross-platform `init --handover` pre-build-ordering proof.

### Post-release

- Development identity advanced to `Nift v4.0.7` in `src/CLI.cpp`,
  `snap/snapcraft.yaml`, release notes, and the guarantee registry baseline
  (released_version 4.0.6, release_commit d0d10c9, development_version 4.0.7).
- Regression-suite version assertions advanced to v4.0.7; contract modules
  re-verified against the v4.0.7 development binary.
- Both `nift-dev/nift` and `nift-dev/nift-regression-suite` pushed to `main`.

### Remaining downstream tracking

- Homebrew auto-bump merge + fresh install; Chocolatey moderation/approval and
  fresh install; Flathub external PR; Snap non-x86 architectures.
- Run `distribution-verification.yml` against the exact public version once
  stores propagate.

## v4.0.7 release report (2026-08-28)

### Scope

v4.0.7 is a CLI-only release. It unifies the build and inspection command
grammar and adds an explicit repair path for interrupted builds. The embedded
engine, its language bindings, the shared corpus and the experimental Rust
implementation remain in-tree but are not released, documented or promoted.

### Source and workflow

- Tag: `v4.0.7` (annotated) at `756aa61`; pushed to `origin`.
- GitHub Actions run: `33146385355` — publication succeeded; the run initially
  reported failure due to a release-ordering defect (below) and completed
  success after the public installer was deployed and the failed/skipped
  verification jobs were re-run.
- GitHub release: https://github.com/nift-dev/nift/releases/tag/v4.0.7

### Release-ordering defect and recovery (public installer deployment)

`packaging/install.sh` changed since v4.0.6 (a `custom_install_dir` refactor and
an automatic macOS PATH-profile update), but the website was not redeployed
first, so `https://nift.dev/install` still served the previous installer. The
`installer-public-smoke` gate correctly caught this post-publication
(`packaging/install.sh /tmp/nift-install.sh differ: byte 68, line 5`). Detecting
a stale public installer only in the post-publication smoke is too late, so this
release established a permanent invariant (item 13 in the release-candidate
validation checklist): the exact reviewed installer must be deployed and
byte-verified before the release tag is created, protected by the three gates
described there — the pre-tag process gate (mandatory non-publishing rehearsal
whose aggregate depends on `public-installer-preflight`, with the go/no-go
review refusing tag authorization unless it passes), the publication gate (the
tag-triggered workflow rechecks equality via `public-installer-preflight` as a
dependency of `publish` and refuses to publish if it differs), and the
post-publication gate (the retained `installer-public-smoke` jobs).

Recovery (no release assets, tag or published checksums were changed):
- Website deployment (`main`) commit `a0573ec` and website source (`stage`)
  commit `2369090` deployed the reviewed installer to `https://nift.dev/install`.
- Public installer SHA-256 after deployment: `a98bdf72…` == canonical
  `packaging/install.sh` SHA-256 `a98bdf72…`; independent `cmp` PASS; `sh -n`
  PASS.
- The public script was exercised against the published 4.0.7 release in a
  temporary install directory: installed binary reports `Nift v4.0.7`; clean
  `init`/`build --all`/`status`/`info` smoke PASS.
- Failed/skipped verification jobs re-run: `installer-public-smoke` PASS on
  linux-x86_64, macos-x86_64 and macos-arm64; the run then completed success.
- User-visible broken-installation window: none. The stale installer resolved
  `releases/latest` (= v4.0.7) and honoured `NIFT_VERSION`, so it still installed
  the correct 4.0.7 binary; it only lacked the newer macOS PATH-profile
  convenience, not correctness.

### Release-candidate validation (performed at `756aa61`, clean build)

- Full implementation-local suite, external contract suite (22/22 modules +
  historical/ruthless), ASan/UBSan, performance/scaling and memory guards, and
  the reduced-CLI isolation gate all PASS (the external suite was reconciled to
  the unified grammar and the `.unfinished`/`--repair` semantics first).
- Four-platform non-publishing native-runner rehearsal (run `33144427317`)
  PASS: linux-x86_64, macos-arm64, macos-x86_64, windows-x86_64, installer
  preflight, aggregate rehearsal; publish and public smokes skipped.
- Website built with the candidate binary (75/75 pages); homepage unchanged; no
  Embedded Nift promotion anywhere in the public site.

### Archives and checksums (definitive, from the published release)

- `nift-4.0.7-linux-x86_64.tar.gz` (489 353 bytes) — `b76b23c81d4dd8b3b1b972487b1b3228fd5a584dd187b88e871cc6c2d2458716`
- `nift-4.0.7-macos-arm64.tar.gz` (372 544 bytes) — `6960314754394c3684a1e563a092b27ec23e63cf932b0da7946ede44506a207f`
- `nift-4.0.7-macos-x86_64.tar.gz` (396 520 bytes) — `c915e9849d1794e77edc99a595d8c807fb0f38020a27e75a94ad32859253ad9f`
- `nift-4.0.7-windows-x86_64.zip` (1 388 264 bytes) — `9db779cb264d3936b7be71aad8df1abc2bd70bba6194cc77f7bcb8f22bf80aad`
- `SHA256SUMS` (386 bytes) — independently downloaded; all four entries verified
  (`sha256sum -c` OK).
- Release body equals the reviewed CLI-only notes; no Embedded Nift mention.

### Package publication

- **Snap stable**: PUBLISHED on amd64 (revision 678) and arm64 (revision 677)
  via `snapcraft upload … --release stable`.
- **Chocolatey Community**: `nift.4.0.7.nupkg` pushed successfully to
  `push.chocolatey.org` — submitted, awaiting moderation; not yet independently
  verified from the public channel.
- **Homebrew**: formula built and tested on Linux x86-64 and macOS arm64;
  propagation is left to Homebrew's automatic bump service (no manual PR);
  fresh install is external verification.
- **Flathub**: external `flathub/cc.nift.nsm` manifest update required —
  pending external PR.
- **Distribution verification**: run `distribution-verification.yml` against the
  exact public version once channels have had the required propagation time.

### Post-release

- Development identity advanced to `Nift v4.0.8` in `src/CLI.cpp`,
  `snap/snapcraft.yaml`, release notes, and the guarantee registry baseline
  (released_version 4.0.7, release_commit 756aa61, development_version 4.0.8).
- Regression-suite version assertions advanced to v4.0.8.
- Both `nift-dev/nift` and `nift-dev/nift-regression-suite` pushed to `main`.

### Remaining downstream tracking

- Homebrew auto-bump merge + fresh install; Chocolatey moderation/approval and
  fresh install; Flathub external PR; Snap non-x86 architectures; public
  installer verified against 4.0.7 in CI (completed).
- Run `distribution-verification.yml` against the exact public version once
  stores propagate.

## v4.0.8 release report (2026-08-31)

### Scope

v4.0.8 fixes the build-progress output ordering so final summaries are never
interleaved with an active progress line, replaces the progress presentation
with a restrained green Braille spinner, hardens the `.nift/.lock`
serialization file (parent-sync fail-closed, write failure seams, no-follow
open, init-established lock), and adds automated all-architecture Snap release
coordination. The embedded engine, its language bindings, the shared corpus and
the experimental Rust implementation remain in-tree but are not released,
documented or promoted.

### Source and workflow

- Tag: `v4.0.8` (annotated) at `89eb46e`; pushed to `origin`.
- GitHub release: https://github.com/nift-dev/nift/releases/tag/v4.0.8 —
  published 2026-08-31T10:00:29Z, non-draft, non-prerelease.
- Pre-tag rehearsals: Snap rehearsal `33375601302` (toolchain-preflight ran,
  amd64/arm64 builds passed, release-coordination skipped with no Store
  mutation) and release-artifacts rehearsal `33375627058` (all four archives +
  installer + public-installer gates + rehearsal aggregate, publish skipped)
  PASSED.
- Push-triggered runs at the tagged commit passed: Init targets, Checkpoint 10,
  Test integrity guards, packaging matrix, Performance regression guards.

### Snap coordination and i386 promotion defect

The connected Snap Store/Launchpad build service published the six declared
platforms to `latest/edge`; the release-coordination job staged exactly those
edge revisions to `latest/candidate`, and the amd64 candidate confinement smoke
passed (revision 698). The subsequent whole-channel `snapcraft promote` failed
(run `33380252640`): its completeness policy requires the entire set of
ever-released store architectures, which includes the historical i386 entry
(409, 3.0.3) that Nift no longer declares or builds.

The promotion attempt itself was fail-closed with respect to stable: it made no
stable mutation. By the time it failed, candidate staging had already completed
(all six selected revisions were in `latest/candidate`), the amd64 candidate
smoke had passed, and `latest/candidate` remained populated with the screened
set. Only `latest/stable` was untouched by the failure.

Fix `53a3e66` replaced whole-channel promote with guarded per-revision
releases: after candidate revalidation, the coordinator runs
`snapcraft release nift <revision> latest/stable` for exactly the six selected
revisions, then verifies stable. Per-revision releases are idempotent, so a
rerun preserves already-correct stable assignments. Snapcraft offers no
supported atomic multi-architecture release API that can exclude a legacy
architecture.

Recovery: the six already-screened candidate revisions were released to
`latest/stable` manually; amd64/arm64 were initially released from newer edge
rebuilds (704/703) instead of the screened revisions, then corrected to the
exact screened candidate revisions. Final verified `latest/stable` state:

- amd64 revision `698` · arm64 revision `702` · armhf revision `699` ·
  ppc64el revision `700` · riscv64 revision `687` · s390x revision `701` — all
  at version `4.0.8`.
- Legacy `i386` (`409`, `3.0.3`) untouched in stable/edge; never released,
  promoted or closed.
- amd64 revision `698` is the exact revision that passed the candidate
  confinement smoke.

### Release-candidate validation (performed at `89eb46e`, clean build)

- Full implementation-local suite, external contract suite, ASan/UBSan,
  performance/scaling and memory guards PASS; the repaired full-build scaling
  benchmark (2000/8000 pages, 7 interleaved rounds, median paired ratio vs a
  7.00x threshold plus confirmation phase) with invocation-invariant guards.
- `.nift/.lock` lifecycle coverage: init establishes the persistent lock file,
  `.gitignore` records `.nift/.lock`, interrupted builds leave no
  `.unfinished`, and write/parent-sync/legacy-removal failure seams fail
  closed.
- Four-platform non-publishing rehearsal PASS (linux-x86_64, macos-arm64,
  macos-x86_64, windows-x86_64) with installer preflight and aggregate
  rehearsal; publish and public smokes skipped.
- Fresh Linux archive smoke: `Nift v4.0.8`; `init` creates `.nift/.lock` with
  the canonical sentence; `build`/`status` PASS; no `.unfinished`.

### Archives and checksums (definitive, from the published release)

- `nift-4.0.8-linux-x86_64.tar.gz` (499 258 bytes) — `d63cc323c7f9c4a624ddcab131c1f52db440bf0c07c3c791ce5658fa5cabb2c8`
- `nift-4.0.8-macos-arm64.tar.gz` (380 589 bytes) — `3b0bb6db6ba7dc514cf504d6fd8608b75ebe54fc7aeaf9d8c8cdf09bd1adb6d7`
- `nift-4.0.8-macos-x86_64.tar.gz` (404 052 bytes) — `76f253369a41a25e6f246f7ddd88a53f16ae475b87386d73af75b2b9c0b114c4`
- `nift-4.0.8-windows-x86_64.zip` (1 400 751 bytes) — `3c04f48c37d88ec44a8b0c74a667f78d8731c2b0744dd5226df033fbe9fc580d`
- `SHA256SUMS` (386 bytes) — independently downloaded; all four entries verified
  (`sha256sum -c` OK).

### Package publication

- **Snap stable**: PUBLISHED on all six declared architectures at v4.0.8 with
  the exact screened revisions (amd64 698, arm64 702, armhf 699, ppc64el 700,
  riscv64 687, s390x 701); legacy i386 untouched. Recovery required the
  coordinator fix `53a3e66` (see above) plus a manual release of the screened
  candidate revisions.
- **Chocolatey Community**: `nift.4.0.8.nupkg` pushed — submitted, awaiting
  moderation; not yet independently verified from the public channel.
- **Homebrew**: formula built and tested on Linux x86-64 and macOS arm64;
  propagation is left to Homebrew's automatic bump service (no manual PR);
  fresh install is external verification.
- **Flathub**: external `flathub/cc.nift.nsm` manifest update required —
  pending external PR.
- **Distribution verification**: run `distribution-verification.yml` against the
  exact public version once channels have had the required propagation time.

### Post-release

- Development identity advanced to `Nift v4.0.9` in `src/CLI.cpp`,
  `snap/snapcraft.yaml`, release notes, and the guarantee registry baseline
  (released_version 4.0.8, release_commit 89eb46e, development_version 4.0.9).
- Regression-suite version assertions advanced to v4.0.9; the full-build
  scaling benchmark invocation invariants synced byte-identical.
- Both `nift-dev/nift` and `nift-dev/nift-regression-suite` pushed to `main`.

### Remaining downstream tracking

- Homebrew auto-bump merge + fresh install; Chocolatey moderation/approval and
  fresh install; Flathub external PR; Snap non-x86 rebuilds on edge (amd64 704,
  arm64 703, armhf 705) do not affect stable and are selected only by a future
  coordinator run.
- Run `distribution-verification.yml` against the exact public version once
  stores propagate.

## v4.0.9 release report (2026-09-04)

### Scope

v4.0.9 gives ordinary CSS and JavaScript direct ownership in the output tree,
makes build and status output consistently file-oriented, fixes a CLI
read-integrity defect that could silently render unreadable sources as empty,
and hardens Snap publication with honest rollback reporting and guarded
per-revision stable releases. The embedded engine, its language bindings, the
shared corpus and the experimental Rust implementation remain in-tree but are
not released, documented or promoted.

### Release blocker and correction

- **Unreadable-source CLI regression (found during preparation).**
  `ProjectInfo::read_shared_source` returned an empty string instead of
  `nullptr` for unreadable files, so the CLI silently rendered unreadable
  content/`@input`/template sources as empty and reported success. It was a
  latent defect affecting v4.0.7 and v4.0.8, introduced by the CP1 RenderHost
  seam. Fixed in `9aa3c4b` to mirror `ProjectState` (`read_file_checked`,
  `nullptr` on failure, failed reads never cached), with CLI smoke coverage,
  a `ProjectState`/`ProjectInfo` read-parity test, an unreadable-`@input`
  checkpoint-8 case, and a black-box regression-suite contract.
- **Checkpoint-8 reconciliation to the CP3 direct-write contract.** The gate's
  write-interruption cases predated the approved CP3 direct-write design (in-place
  output/`.info.json` writes backed by the durable `.unfinished` marker and
  `build --repair`) and had been masked by the unreadable-content failure. They
  were renamed and re-asserted against that contract:
  `partial-direct-write-marks-unfinished-and-repairs` and
  `sigkill-during-direct-write-marks-unfinished-and-repairs`. Case count 16 → 17;
  `HANDOVER.md` and the checkpoint-8 evidence were refreshed.

### Source and workflow

- Annotated tag object `c0677b37fd3ad2c89016067328dcda05dd93b132`; tag `v4.0.9`
  peels exactly to `9c91298d9f7fba226fb1ea0b2e1cf78cf1f9bb43`; only the tag was
  pushed (no branch changes).
- GitHub Actions run: `33818807348` — every job succeeded (unix linux-x86_64 /
  macos-arm64 / macos-x86_64, windows, installer-preflight,
  public-installer-preflight, publish, installer-public-smoke ×3, homebrew ×2,
  chocolatey, snap toolchain-preflight + build amd64/arm64 +
  release-coordination; rehearse skipped as dispatch-only).
- Pre-tag rehearsals: release-artifacts rehearsal `33817292964` and Snap
  rehearsal `33817296723` (both non-publishing; all publish/coordination jobs
  skipped). Push-triggered CI at `9c91298`: test-integrity `33816347286`,
  packaging `33816347291`, checkpoint-10 `33816347309`, performance-regression
  `33816347382`, init-targets `33816347187` — all success.
- GitHub release: https://github.com/nift-dev/nift/releases/tag/v4.0.9 — public,
  non-draft, non-prerelease, latest; body is the reviewed committed notes
  (`docs/evidence/release-4.0.9/release-notes-4.0.9.md`, verbatim modulo a
  trailing newline). The release display name is empty by design (published via
  the tag); it is cosmetic and was not mutated.

### Archives and checksums (definitive, from the published release)

- `nift-4.0.9-linux-x86_64.tar.gz` (499 381 bytes) — `38658a223dabd3360dbbe2cc48df5e8252009bf79f0e488453f73243d3f8f26a`; extracted binary reports `Nift v4.0.9`; archive contains LICENSE, README.md, nift.
- `nift-4.0.9-macos-arm64.tar.gz` (380 985 bytes) — `c35fb58a8d56773127f3779f7dbec2cb6600d6226b2dc500f13d653f6992a90d`
- `nift-4.0.9-macos-x86_64.tar.gz` (404 605 bytes) — `510b6ca8bb20ebf9659abbc11630cb772f560b68656c6eb03a83cc1fb0c7d025`
- `nift-4.0.9-windows-x86_64.zip` (1 288 287 bytes) — `da9bfb6dc579d6f6218bdce0815c6d2d4e92c0e4c5d57cf816cda0c1025e62f5`
- `SHA256SUMS` (386 bytes) — independently downloaded; all four entries verified (`sha256sum -c` OK).
- Release body equals the reviewed CLI-only notes; no Embedded Nift mention.

### Installer verification

`installer-public-smoke` PASS on linux-x86_64, macos-x86_64 and macos-arm64: the
live `https://nift.dev/install` is byte-identical to `packaging/install.sh`
(SHA-256 `a98bdf72…`), `sh -n` passes, the tagged 4.0.9 is installed through the
public script with an exact version assertion, and fresh `init`/`build`/`status`
succeed.

### Snap publication (coordinated, per-revision)

The coordinator staged all six declared architectures at 4.0.9, verified
candidate, ran the amd64 candidate confinement smoke (revision 722, PASS), and
released each selected revision individually to `latest/stable`. Legacy i386
(409, 3.0.3) was ignored and left untouched in edge/stable and absent from
candidate. Final verified state:

- amd64 revision `722` · arm64 revision `724` · armhf revision `723` ·
  ppc64el revision `726` · riscv64 revision `727` · s390x revision `725` — all
  at version `4.0.9` on both `latest/candidate` and `latest/stable`.
- Rollback commands recorded against the pre-release stable revisions
  (698/702/699/700/687/701).

### Package publication

- **Chocolatey Community**: `nift.4.0.9.nupkg` pushed successfully to
  `push.chocolatey.org`; the package page
  `community.chocolatey.org/packages/nift/4.0.9` returns 200 and shows
  **Pending automated review** (under moderation). Submitted, not yet approved
  or independently fresh-install verified. Do not resubmit.
- **Homebrew**: `homebrew.yml` built and tested the formula against the
  immutable `v4.0.9` tag on macOS arm64 and Linux x86-64 (validation only;
  nothing published to homebrew-core). Canonical `Homebrew/homebrew-core`
  formula still lists 4.0.8; propagation is Homebrew's external automatic bump
  service (no manual PR).
- **Flathub**: external `flathub/cc.nift.nsm` manifest tag/checksum update
  required — explicitly separate downstream work, pending.
- **Distribution verification**: run `distribution-verification.yml` against
  the exact public version once channels have had the required propagation time.

### Post-release

- Development identity advanced to `Nift v4.0.10` in `src/CLI.cpp`,
  `snap/snapcraft.yaml`, release notes, and the guarantee registry baseline
  (released_version 4.0.9, release_commit 9c91298, development_version 4.0.10).
- Regression-suite version assertions advanced to v4.0.10.
- Both `nift-dev/nift` and `nift-dev/nift-regression-suite` pushed to `main`.

### Remaining downstream tracking

- Homebrew auto-bump merge + fresh install; Chocolatey moderation/approval and
  fresh install; Flathub external PR; Snap fresh install verification on
  non-x86 architectures.
- Run `distribution-verification.yml` against the exact public version once
  stores propagate.
