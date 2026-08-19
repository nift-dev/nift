# Nift packaging and publication handover

## Purpose and authority

This document owns the operational relationship between Nift releases, package
definitions, package stores, and the release workflows in this repository. Read
`RELEASES.md` first for the release-candidate boundary and validation standard.
That document decides whether a release is ready; this one explains how a ready
release becomes downloadable artifacts and store packages.

The tagged Nift source commit is authoritative for implementation. A GitHub
release is authoritative for portable release archives and checksums. The
in-repository Snap and Chocolatey definitions are authoritative for those
packages. The Flathub-owned `flathub/cc.nift.nsm` repository remains
authoritative for the published Flatpak manifest.

Publishing, pushing, tagging, changing a public version, or promoting a package
store channel is a public action and requires Nick's explicit approval.

## GitHub Actions secrets

Repository secrets are configured on GitHub under **Settings → Secrets and
variables → Actions → Repository secrets**. The workflows expect exact names
`SNAPCRAFT_STORE_CREDENTIALS` and `CHOCOLATEY_API_KEY`. Never commit their values.

Create the Snap credential on a trusted Linux machine with current Snapcraft:

```bash
snapcraft export-login --snaps=nift \
  --acls package_access,package_push,package_update,package_release \
  --channels=stable exported-snap-login.txt
```

Optionally add `--expires=<ISO-8601-date>` and rotate it before expiry. Paste the
complete contents of `exported-snap-login.txt` into the GitHub secret, verify it
with `SNAPCRAFT_STORE_CREDENTIALS="$(cat exported-snap-login.txt)" snapcraft
whoami`, then securely delete the exported local file when no longer needed.

## Repository layout

```text
snap/snapcraft.yaml                         Snap recipe
packaging/chocolatey/                       Chocolatey source templates
packaging/flatpak/                          upstream Flathub migration aid
packaging/homebrew/                         upstream homebrew-core formula template
.github/workflows/release.yml               portable GitHub release archives
.github/workflows/snap.yml                  Snap build and optional publication
.github/workflows/chocolatey.yml            Chocolatey pack and optional push
.github/workflows/homebrew.yml               Homebrew formula generation/testing
```

These definitions were consolidated here to keep packaging changes reviewable
with source/build changes and to prevent version drift. The historical
`nifty-site-manager/nsm-snap`, `nsm-snap-nift`, `nsm-flatpak`, and
`nsm-chocolatey-nift` URLs are institutional history, not current upstream
authority. They were initially unavailable anonymously, then inspected from the
retained clones in `package-specific-repos`. Current recipes were reconciled
against those sources, the live Flathub manifest, and the current C++
Make/install contract rather than copied mechanically.

## Version and artifact contract

The public release tag has the form `vX.Y.Z`. Before creating it, reconcile all
of the following deliberately:

- `src/CLI.cpp` must report public version `X.Y.Z`.
- `snap/snapcraft.yaml` must contain version `X.Y.Z`.
- release notes and public documentation must describe that release.
- the intended commit must have passed the release-candidate validation in
  `RELEASES.md`.

`release.yml` rejects a tag whose public executable version differs. It builds:

- `nift-X.Y.Z-linux-x86_64.tar.gz`
- `nift-X.Y.Z-macos-arm64.tar.gz`
- `nift-X.Y.Z-macos-x86_64.tar.gz`
- `nift-X.Y.Z-windows-x86_64.zip`
- `SHA256SUMS`

The workflow creates the GitHub release only after all platform jobs succeed and
the installer preflight passes (`sh -n packaging/install.sh` plus
`make test-installer`). After publication, `installer-public-smoke` fetches the
actual `https://nift.dev/install`, requires it to be byte-identical to the tagged
`packaging/install.sh`, installs the exact tagged release through that public
script on Linux x86-64, macOS ARM64 and macOS x86-64, and exercises `nift init`,
`nift build` and `nift status` in a fresh project. This is build/distribution
automation, not evidence that every release-candidate gate has been performed;
record that evidence before tagging.

Published release assets are immutable inputs to downstream package managers.
Once a GitHub release exists, `release.yml` deliberately leaves its assets
unchanged on a rerun. Never restore `gh release upload --clobber`, replace an
archive, or otherwise mutate a published asset after Homebrew, Chocolatey,
Flathub or another consumer has recorded its checksum. If a release is partial
or defective, investigate it explicitly; do not repair it by silently replacing
files at the same URLs.

## Snap

The Snap is named `nift` and supports every architecture currently listed by
Snapcraft's Launchpad remote-build service: amd64, arm64, armhf, ppc64el,
riscv64, and s390x. It runs `usr/bin/nift`. For v4.0.3 the recipe is staged
with strict confinement and the `home` interface; this must be proven with the
Store `edge` artifact against ordinary projects and their project-local `.nift/`
state before promotion. It builds the same portable C++ source through its
Makefile. An architecture-specific build
failure is a packaging defect to fix, not a reason to narrow the intended support
claim pre-emptively.
`snap.yml` builds on relevant pull requests and is called by `release.yml` after
the GitHub release succeeds; on that release path it requires the recipe version
to match the tag. GitHub-hosted runners directly build and publish amd64 and
arm64. Produce the remaining supported architectures through `snapcraft
remote-build` or a connected Snap Store/Launchpad build. Legacy i386 is not
declared because the current Launchpad supported-architecture list no longer
includes it.

Configure the GitHub Actions secret `SNAPCRAFT_STORE_CREDENTIALS` with a scoped
Snap Store login/export credential to enable stable publication after the GitHub
release job succeeds. Without the secret the workflow builds and uploads the
`.snap` artifact but does not publish it. Treat credential rotation, store
ownership and channel promotion as external state that must be checked in the
Snap Store.

The Snap Store can also build a connected GitHub repository through its own
Launchpad integration. That is a separate publication path from `snap.yml` and
does not use the GitHub Actions secret. Decide which path owns publication for a
release so that both do not promote independently. In particular, `snap.yml`
publishes directly to `stable` when the credential is present; leave the secret
absent if builds should remain under manual edge/candidate review and promote
them deliberately in the Snap Store instead. Connected builds commonly land in
`edge`, and completion may vary substantially by architecture; `riscv64` can
take much longer than the other builders.

Before a stable release, install the produced `.snap` on a clean representative
host and exercise version/help, project creation, a real build, dependency
tracking, and filesystem behavior under the configured confinement, including project-local `.nift/` state.

## Chocolatey

`packaging/chocolatey/nift.nuspec` and `tools/chocolateyInstall.ps1` contain
`__VERSION__` and `__CHECKSUM64__` tokens. They are source templates, not a
directly publishable package. After `release.yml` has published the GitHub
release, it calls the reusable `chocolatey.yml` workflow, which:

1. downloads the matching Windows x86-64 release ZIP;
2. calculates its SHA-256 checksum;
3. checks the contained executable reports the expected public version;
4. replaces tokens only in `.build/chocolatey`;
5. packs and retains the `.nupkg`; and
6. pushes it only when `CHOCOLATEY_API_KEY` is configured.

Pull requests exercise token replacement and `choco pack` with inert values but
do not download or publish a release. Before pushing, test install, upgrade and
uninstall in a disposable Windows VM, including command shimming and a small real
Nift project. The Chocolatey Community Repository may also hold a submission for
moderation; workflow success does not imply approval or public availability.

Chocolatey package versions remain replaceable while they are still under
moderation. If automated testing finds a genuine package defect, manually run
`chocolatey.yml` against the release version with `resubmit: true`. This rebuilds
and pushes the exact same version instead of skipping it as an existing feed
entry. Use this only while the version is unapproved and Chocolatey's review
instructions request an exact-version resubmission. Normal runs retain duplicate
submission protection.

If verification fails, inspect its public test log before acting. Requesting a
verifier rerun is appropriate only when the package is already correct and the
test environment produced a spurious result. If the `.nupkg` contains a wrong
URL, checksum or script, fix it and resubmit the exact version during moderation.
Allow for Chocolatey's CDN/moderation delay before concluding that a replacement
was ignored. Questions or recovery explanations belong in the package page's
**Add to Review Comments** box, not email replies, Disqus, or Gist comments.

The historical package embedded `nift.exe` and a duplicate `nsm.exe`, depended on
Git, and included hand-maintained verification checksums. The rewrite instead
downloads the immutable GitHub release, verifies its checksum automatically, and
packages only `nift.exe`. It has no Git dependency. Generated
`VERIFICATION.txt` remains available to Chocolatey moderators.

## Homebrew

`brew install nift` is owned by the external `Homebrew/homebrew-core` formula.
As rechecked on 19 August 2026, the canonical formula publishes Nift 4.0.1 from
`nift-dev/nift` and Homebrew provides bottles for supported Apple Silicon/Intel
macOS and Linux architectures. The legacy 3.0.3 LuaJIT/patch/`nsm` formula has
therefore been superseded.

`packaging/homebrew/nift.rb.in` is the upstream formula for the rewrite.
`homebrew.yml` resolves the immutable source archive and checksum and tests the
formula on Homebrew for macOS arm64 and Linux x86-64. It is called after a GitHub
release and uploads the resolved formula as an artifact; it does not push to
`homebrew-core` or manufacture bottle checksums.

The rewrite has now reached the canonical Homebrew formula, so future ordinary
release work must use Homebrew's supported automatic version-bump path rather
than recreating the original migration manually. **Do not manually open a simple
version-bump pull request against `Homebrew/homebrew-core`.** Only prepare a
manual upstream change if Homebrew maintainers explicitly request one because
the automated path cannot represent a non-routine formula change. Homebrew's own
CI and maintainers own official bottles. Always verify the public formula and a
fresh installation after propagation rather than inferring availability from
Nift's in-repository formula tests.

Historical migration PRs remain useful provenance, but they are no longer the
current installation state. `Homebrew/homebrew-core#299226` was closed with a
request to use Homebrew's automated bump infrastructure; Homebrew subsequently
published 4.0.1 through the canonical formula.

## Flatpak and Flathub

Nift is already published on Flathub with immutable app ID `cc.nift.nsm`. Do not
submit it as a new app or change the ID as part of a normal update. The canonical
`flathub/cc.nift.nsm` manifest was migrated to the `nift-dev/nift` source and
current `nift` command by PR #12 on 18 August 2026; ordinary updates now advance
that manifest's immutable tag URL and checksum without restoring legacy Git,
LuaRocks, patch, or `nsm` inputs.

`packaging/flatpak/cc.nift.nsm.json.in` describes the core source migration for
the current C++ rewrite. It is deliberately a template and deliberately does not
duplicate the canonical repository's AppStream, desktop, and icon assets. For an
actual update:

1. create and validate the public Nift tag and source archive;
2. replace `@VERSION@` and `@SHA256@` with the immutable tag/archive checksum;
3. update `flathub/cc.nift.nsm`, retaining its established metadata/assets;
4. change the command from legacy `nsm` to `nift` only after checking launcher
   and user compatibility;
5. remove legacy Git, LuaRocks, patch and source inputs only after confirming the
   rewrite no longer needs them;
6. build locally with `flatpak-builder`/`org.flatpak.Builder`, run Flathub lint,
   and exercise the CLI against host project files; and
7. submit the update through the existing Flathub repository and observe its
   build/publish result.

The existing listing is the reason an update path exists despite current Flathub
rules for new console-app submissions. Grandfathered presence is not proof that
a major source/runtime migration will be accepted unchanged; reviewers and the
external repository remain authoritative.

## Changing upstream repository references

There is no single package-manager redirect setting. Publish one new version in
each service with its source metadata changed:

- Snap builds from this checkout; `snap/snapcraft.yaml` supplies `source-code`,
  `issues`, and website metadata. If the Snap Store has a connected GitHub build,
  change that connection separately in the Snapcraft dashboard or disable it in
  favor of this repository's workflow.
- Chocolatey's next `.nupkg` carries `packageSourceUrl`, `projectSourceUrl`,
  `bugTrackerUrl`, release URLs, and checksums from `packaging/chocolatey`.
  Published old package versions remain immutable historical records.
- Flathub's `flathub/cc.nift.nsm` manifest must change its source URL to the
  immutable `nift-dev/nift` release. Update its AppStream bugtracker and
  VCS-browser URLs in the same external pull request.
- Homebrew's canonical `nift.rb` is updated through Homebrew's automatic bump
  infrastructure for ordinary releases. Do not open a manual simple-bump pull
  request; the in-repository template only validates the candidate formula.

Archive the old repositories only after the public store entries use the new
source and fresh installations have been verified. Add a short archived-repo
README pointing at `nift-dev/nift` where practical.

## Post-release distribution verification

After a public release and downstream propagation, run the maintained
`.github/workflows/distribution-verification.yml` workflow against the exact
public version. It installs Nift from the public GitHub release archives,
Homebrew, Chocolatey, Snap stable, and Flathub rather than rebuilding those
channels from this checkout. The shared `scripts/distribution_smoke.py` contract
requires the exact version and exercises `version`, `about`, `commands`, a basic
`init`/`build`, and a representative `--target=vercel` build.

The workflow is intentionally strict while stores propagate. An older Homebrew,
Chocolatey, or Flathub package should make that channel fail until the public
store catches up; do not weaken the requested version to manufacture a green
run. Each successful channel uploads normalized JSON evidence and the final job
retains a channel-result summary. See `DISTRIBUTION-VERIFICATION.md` for the
full contract, failure classification, and rerun policy.

## Minify++ packaging boundary

Minify++ is a viable package candidate: its standalone CLI reports 1.1.0 and its
subproject has independent builds, tests, release notes, and clean-package
validation. It should not be installed incidentally by the Nift packages or be
published as a second executable under Nift's package identity. That would blur
the deliberately independent ownership boundary and couple unrelated release
cadences.

Package Minify++ from its standalone canonical repository as a separate product
after reconciling its current `Unreleased` notes, tag/archive convention, install
target, platform validation, public name availability, and website/docs. Reuse
the release architecture here once that repository has its own approved release
contract; do not source a public Minify++ package from Nift's embedded copy.

## Step-by-step release guide

Use this order for a normal `X.Y.Z` production release. Stop at any failed gate,
fix the problem before tagging where possible, and retain exact command/workflow
evidence for the release report.

### 1. Prepare the release candidate

1. Start from the intended clean `main` commit and review unrelated working-tree
   changes before touching release files.
2. Choose `X.Y.Z` with Nick's approval. Update the public version reported by
   `src/CLI.cpp`, `snap/snapcraft.yaml`, release notes and affected public docs.
3. Verify `nift version`, `nift about` and `nift commands`; confirm they are
   release-ready and contain no temporary checkpoint identity. Also confirm
   unknown and help-like invocations fail with the intended diagnostic and
   direct users to `nift commands`. Do not infer that a separate `nift help`
   command exists.
4. Review `PENDING-WEBSITE.md`. Implement and verify every website change targeted
   at `X.Y.Z`, then remove the completed entries or explicitly retarget approved
   deferrals. The queue must contain no unresolved item for `X.Y.Z` before tagging.
5. Complete the release-candidate validation in `RELEASES.md`, including the full
   test suites, sanitizers/platform checks where applicable, the real Nift
   website build, embedded Minify++ synchronization, and residue inspection.
6. Build the same archive layouts the workflow will publish. Extract them into
   clean temporary directories and test the contained executable rather than
   relying only on the repository build.
7. On Windows, confirm the release executable has no MinGW runtime dependency on
   `libgcc`, `libstdc++` or `libwinpthread`. Exercise a representative real Nift
   project on every release platform available for validation.
8. Commit and push all approved release-preparation changes. Recheck that `main`
   and the intended release commit are exactly the state that was validated.

### 2. Create the GitHub release

1. Obtain explicit approval for the public release action.
2. Create the approved signed or annotated `vX.Y.Z` tag at the validated commit
   and push that tag to `nift-dev/nift`.
3. Watch `.github/workflows/release.yml`. All Linux, macOS and Windows artifact
   jobs plus `installer-preflight` must succeed before the GitHub release is
   created. After publication, require `installer-public-smoke` to pass on Linux
   x86-64, macOS ARM64 and macOS x86-64; this proves the live nift.dev script
   matches the tag, verifies the release checksum and can initialize/build a
   fresh project using the installed binary.
4. Confirm the release contains exactly the expected four platform archives and
   `SHA256SUMS`, and that each archive name and embedded executable version match
   `X.Y.Z`.
5. Download at least the checksums and representative archives from the public
   release URL, verify them independently, and record the release URL, tag commit,
   workflow run and final checksums.
6. From this point, treat every published asset as immutable. Never replace an
   archive at the same URL. A workflow rerun should leave an existing release
   untouched.

### 3. Publish and verify Snap

1. Decide before publication whether the connected Snap Store/Launchpad build or
   GitHub Actions owns this release. Avoid running both publication paths without
   a deliberate reason.
2. For connected builds, confirm the store points at `nift-dev/nift`, select the
   release tag/commit as appropriate, and let builds enter `edge` for all current
   supported architectures: amd64, arm64, armhf, ppc64el, riscv64 and s390x.
3. For GitHub Actions publication, configure `SNAPCRAFT_STORE_CREDENTIALS` only
   when direct publication to `stable` is intended. Without it, `snap.yml` builds
   artifacts but does not publish them.
4. Wait for every architecture to reach a terminal result; `riscv64` may take
   substantially longer. Legacy i386 is not a supported core24/Launchpad target.
5. Install the exact `edge` or candidate revision on a clean representative host
   and test version/help, project creation, a real build, dependency tracking and
   filesystem access under strict confinement, especially project-local `.nift/` state.
6. Promote to candidate/stable only with explicit approval. Confirm `snap info
   nift` reports `X.Y.Z` on the intended channel and perform a fresh store install.

### 4. Publish and verify Chocolatey

1. Confirm the final Windows ZIP is publicly downloadable and will no longer be
   replaced. `chocolatey.yml` derives its checksum from that release asset.
2. Let the tag-triggered release call the Chocolatey workflow, or manually run
   `chocolatey.yml` with `version: X.Y.Z`. `CHOCOLATEY_API_KEY` must be configured
   to push; otherwise only the `.nupkg` artifact is produced.
3. Inspect the workflow result and retain the generated `.nupkg`. When practical,
   test install, shimmed `nift` execution, a real project, upgrade and uninstall
   in a disposable clean Windows VM.
4. Confirm the package page shows the submission, then wait for validation,
   verification and moderation. Workflow success means submitted, not approved.
5. If automated verification fails, read its public log. For a real package
   defect while the version is still unapproved, fix the workflow/template and
   manually dispatch the exact same version with `resubmit: true`.
6. Use a verifier-only rerun only for a spurious test result against an already
   correct package. Put any maintainer response in **Add to Review Comments** on
   the package page.
7. Declare Chocolatey availability only after approval and a fresh
   `choco install nift --version X.Y.Z` succeeds from the community repository.

### 5. Update Homebrew

1. Download the resolved formula artifact from `homebrew.yml` and confirm its URL
   and SHA-256 refer to the immutable `nift-dev/nift` tagged source archive.
2. Confirm the workflow tested the formula on both supported macOS and Linux
   runners. Do not copy legacy LuaJIT, patch or `nsm` behavior into the formula.
3. Wait for and monitor Homebrew's automatic bump service. Check existing pull
   requests and formula history, but do not manually open a simple version-bump
   pull request or duplicate the automated update.
4. Prepare a manual `Homebrew/homebrew-core` change only if Homebrew maintainers
   explicitly request it for a non-routine formula change; follow their requested
   audit/test/submission process exactly. Homebrew CI and maintainers own official
   bottles.
5. After merge and bottle publication, run `brew update`, install/upgrade Nift
   from Homebrew, verify `nift version`, and record the merged PR and formula URL.

### 6. Update Flathub

1. Work in the external `flathub/cc.nift.nsm` repository; do not create a new app
   or change the established `cc.nift.nsm` app ID.
2. Point the manifest source at the immutable `nift-dev/nift` `vX.Y.Z` archive
   and set its exact SHA-256.
3. Update AppStream `bugtracker` and `vcs-browser` fields to `nift-dev/nift` when
   migrating from the legacy repository. Preserve the established desktop, icon
   and AppStream assets unless the update deliberately changes them.
4. Reconcile the command with `nift` and remove legacy Git, LuaRocks, patches and
   source inputs only after verifying they are no longer required.
5. Build the complete external manifest with `flatpak-builder` or
   `org.flatpak.Builder`, run Flathub lint, and test Nift against host project
   files.
6. Submit the external pull request and wait for Flathub review/build/publication.
   Verify a fresh Flathub install reports `X.Y.Z` before calling it available.

### 7. Close the release

1. Record the exact tag/commit, GitHub release and workflow URLs, final checksums,
   package-manager PRs/builds/revisions/channels, installation tests and known
   limitations in the release handover.
2. Report each downstream separately as built, submitted, verified, approved or
   publicly installable. These services may finish days apart.
3. Update the website/download instructions only with availability that has been
   confirmed from the public store.
4. Track incomplete downstream work explicitly rather than reopening or mutating
   the GitHub release.

Never describe a release as available through a package manager until its public
store entry resolves to the intended version and a fresh installation succeeds.

## v4.0.0 publication record

The first release through this consolidated pipeline is
[`v4.0.0`](https://github.com/nift-dev/nift/releases/tag/v4.0.0), tagged at
`aa2b11794c79456df447397ea73ee9f71f46b6f1`. Its final Windows x86-64 archive
SHA-256 is
`25b271c8fe0bf7ab9e31eb7533b2f7b49d923ccb99771dea6e80acb6f5bfce5a`.
Do not replace that archive.

During recovery, a release rerun replaced the Windows ZIP after the first
Chocolatey package had recorded checksum
`fe8671922fb702bfda6ead5a1348d076053e4899849572ce5447c2055e06b980`.
Chocolatey correctly rejected the stale checksum. Commit `6d59d75` made existing
GitHub release assets immutable on rerun and added the explicit Chocolatey
moderation-resubmission input. The corrected exact-version submission succeeded
in [Chocolatey workflow run 31972761587](https://github.com/nift-dev/nift/actions/runs/31972761587);
store verification and approval remain separate downstream states.

The generated Homebrew formula passed the repository's macOS and Linux tests and
was submitted as
[`Homebrew/homebrew-core#299162`](https://github.com/Homebrew/homebrew-core/pull/299162).
The connected Snap Store build published 4.0.0 to `latest/edge` while stable was
still 3.0.3; most architectures completed quickly, while `riscv64` remained in
progress much longer. Record the eventual channel/architecture result rather
than inferring it from workflow success. The external Flathub update remains
authoritative in `flathub/cc.nift.nsm` and must be recorded by its actual commit
and publication result.

GitHub currently warns that `actions/checkout@v4` and
`actions/upload-artifact@v4` target the deprecated Node.js 20 action runtime and
are being forced onto Node.js 24. This did not invalidate the v4.0.0 workflows,
but future maintenance should upgrade those actions when supported; do not treat
the warning alone as a reason to alter release artifacts.

## v4.0.1 publication record

[`v4.0.1`](https://github.com/nift-dev/nift/releases/tag/v4.0.1) is an annotated
tag at `6088856529366688539aae6f88b6a44d74244c4b`. The complete
[release workflow](https://github.com/nift-dev/nift/actions/runs/32021660432)
succeeded, including Linux x86-64, macOS arm64, macOS x86-64, Windows x86-64,
publication, Homebrew formula tests, Chocolatey submission, and the GitHub Snap
jobs. The published release contains exactly the four platform archives and
`SHA256SUMS`; all four archives were downloaded and verified against that file:

- Linux x86-64: `34a6785adab50e678978377bbd70690f44e1cd081a6e921fb282c81c968b9177`
- macOS arm64: `4325ef0a0794713a59b1157995ab0a8e948931f736d29697ec128e5f99def1ad`
- macOS x86-64: `391f97c7bddab37e2bfd9896f58b9af1f411e405c7e24b98c1abf539c9d5f581`
- Windows x86-64: `fa075b86fb535b2ca97252968658bbd8936414750412d0aee79e4a542a1b519f`

The immutable tagged source archive used by source-based package managers has
SHA-256 `0724c8e6518ea9ace4275e8f96da39680916d157df1afb4bfbb003678bcdfb52`.
The Linux release archive was extracted and its binary reported `Nift v4.0.1`
with the intended `about` output.

Pre-release validation passed the clean local build and full test matrix, the
external regression suite (17 modules and 575 historical assertions), scaling
and memory guards, the ASan/UBSan build and relevant tests, and an exact website
candidate build of all 46 pages. LeakSanitizer itself could not run in the Codex
desktop ptrace environment, so the sanitizer checks used
`ASAN_OPTIONS=detect_leaks=0`; this is an environment limitation, not a claimed
LeakSanitizer pass.

Downstream state recorded on 2026-08-17:

- The public Snap metadata showed 4.0.1 revision 450 on `latest/stable`, with
  candidate and beta following stable. This confirms channel metadata only; a
  fresh store installation was not performed. `latest/edge` still showed 4.0.0
  revision 443 at the time of inspection.
- Chocolatey workflow submission succeeded and
  [`nift/4.0.1`](https://community.chocolatey.org/packages/nift/4.0.1) was in
  `Submitted`/`Pending` state. It was not yet approved or fresh-install tested.
- The generated Homebrew update was submitted as
  [`Homebrew/homebrew-core#299226`](https://github.com/Homebrew/homebrew-core/pull/299226).
  Homebrew closed that pull request on 2026-08-17 and asked that future simple
  version bumps use Homebrew's automated update infrastructure rather than a
  manually/AI-prepared PR. When rechecked on 2026-08-18, the canonical public
  formula had subsequently advanced to stable Nift 4.0.1 with bottles for
  supported macOS and Linux architectures. Homebrew v4.0.1 availability is
  therefore established at the formula level; retain a fresh install/version
  check in the planned distribution-validation campaign.
- The consolidated Flathub migration was submitted as
  [`flathub/cc.nift.nsm#12`](https://github.com/flathub/cc.nift.nsm/pull/12).
  It changes the legacy `nsm` build to the current `nift` source and command,
  removes obsolete bundled dependencies and patching, and updates AppStream
  project links. Manifest validation passed. An initial build exposed a typo in
  the preserved 256px icon checksum; commit
  `16c53050308024405bd1361ce421acca598ae7d4` corrected it and triggered a new
  build. The rerun also failed on x86-64, with the aarch64 leg cancelled after
  that failure. The AppStream construction was then corrected in commit
  `13b99da4b5ad4b7c9ca58424d459d8d25f6f04bc`; the replacement test build passed
  on aarch64 and x86-64, and PR #12 was merged to Flathub `master` as
  `9d8a657064a72c5f899d15db51afee500ef33120` on 2026-08-18. A fresh public
  Flathub installation/version check remains required before recording 4.0.1 as
  publicly installable evidence.

Re-check and append final Chocolatey approval/install evidence, a fresh Homebrew
install/version result from the canonical 4.0.1 formula, and the repaired Flathub
build/publication/install evidence. Do not rewrite the v4.0.1 GitHub
release to close a downstream task.

## v4.0.2 publication record

The 4.0.2 implementation state through `0368069` and its subsequent
documentation-only readiness reconciliation were validated on 19 August 2026.
The annotated `v4.0.2` tag was published at
`596d12a102751ff5c696dd121364a7d1ef6c5be6`, and the immutable
[GitHub release](https://github.com/nift-dev/nift/releases/tag/v4.0.2) was created
by release run `32160738799`.

Pre-release evidence:

- `src/CLI.cpp`, generated framework metadata, `snap/snapcraft.yaml`, the
  Homebrew pull-request validation version, release notes, and public website
  documentation all identify or describe 4.0.2 consistently. The pending website
  queue has no unresolved 4.0.2 item.
- A clean GCC 15 C++17 build passed; `nift version` reported `Nift v4.0.2`, and
  the supported `about` and `commands` surfaces were reviewed. Nift deliberately
  has no generic `help` command or `init --help` option.
- All implementation-local correctness targets passed, including the 4.0.2 init
  targets and the generated Minify++ JavaScript/JSX/format corpora. Standalone
  Jsonic++ and Minify++ synchronization checks passed for 20 and 24 files.
- The independent regression suite passed all 18 contract modules against both
  the repository candidate and a freshly extracted Linux release-layout archive.
- The scaling guard passed with a 4.14x 2,000-to-10,000-page load ratio. The
  10,000-page RSS guard passed with every measured peak below 16,384 KiB; full
  build median was 19.927584 seconds and no-op updated median was 0.099882
  seconds on this host. These timings are host evidence, not portable promises.
- Checkpoint 7 passed 720 incremental/clean output comparisons, Checkpoint 8
  passed 13 filesystem/transaction cases, and Checkpoint 9 passed 1,217
  sanitizer-backed parser/resource cases. The 12-round, 40-page direct-Nift
  Valgrind integration passed with four deliberate failure/recovery phases.
- The ASan/UBSan executable and 4.0.2 init-target contract passed outside the
  Codex ptrace sandbox with leak detection enabled. GCC emitted known
  libstdc++ `<regex>` `-Wmaybe-uninitialized` diagnostics while compiling the
  sanitizer build; no sanitizer finding occurred at runtime.
- Nift Actions runs `32156523801` (init targets) and `32156523799`
  (cross-platform behavioral corpus) passed on Linux, macOS and Windows at
  `0368069`. The independent regression repository has no GitHub Actions
  workflow; its evidence above is the exact local candidate run.
- The Nift website built all 58 pages with the exact candidate and left both
  website source `stage` and generated `public/main` clean. The corresponding
  pushed heads were `255a5c6` and `65d3d2a` during preparation.

Publication evidence:

- Linux x86-64, macOS arm64, macOS x86-64, and statically linked Windows x86-64
  builds passed. The release contains exactly those four archives and
  `SHA256SUMS`; all archives were downloaded from the public release and passed
  independent checksum verification.
- Final archive SHA-256 values are Linux x86-64
  `edec01f9b28feb4fad96b09026d7d336feccd56615ee944b3cde5a8ecbe4f436`,
  macOS arm64
  `fa4aaa840f7b06dc299ab861d9080fcdccf2fd33a5f25442d1adca1f9aeab3d9`,
  macOS x86-64
  `97ce20737e29389efc37e191604f7ae030bd8094f905951f0154a28fa24e8feb`,
  and Windows x86-64
  `d49ab82068b70bdc1a6ea8c01a4866b54506298ed909f65cbffa261eeefff780`.
- The immutable tagged source archive SHA-256 is
  `fe462915db41574a58236c028c34561596751a8e91e868726a18e57210450b14`.
  The extracted Linux archive contained only `LICENSE`, `README.md`, and `nift`;
  its `version`, `about`, and `commands` surfaces passed.
- Both GitHub Snap builds passed and their `Publish stable Snap` steps succeeded
  for amd64 and arm64. A subsequent public `snap info nift` query reported 4.0.2
  on both `latest/stable` (revision 489) and `latest/edge` (revision 485). Other
  connected-store architectures and a fresh public stable installation remain
  external verification tasks.
- The Chocolatey packaging/submission job passed. The public 4.0.2 package page
  showed the package published on 18 August 2026 but still marked “Pending
  automated review”, with validation, verification, and scanning pending when
  checked. Approval and a fresh public install remain separate downstream states.
- The release run's two Homebrew formula jobs exposed a stale in-repository
  smoke invocation (`nift init .html`) after the release itself was published.
  Commit `889d5eb` changed the validation to the supported
  `nift init --ext=.html` form. Dedicated Homebrew run `32161529965` then passed
  on macOS arm64 and Linux x86-64 against the immutable v4.0.2 source tag. No
  manual `Homebrew/homebrew-core` pull request was opened; ordinary propagation
  is left exclusively to Homebrew's automatic bump infrastructure.
- The Flathub manifest's external-data checker follows the latest
  `nift-dev/nift` GitHub release, but no 4.0.2 update pull request existed when
  checked immediately after publication. Leave that checker to propose the
  ordinary tag/checksum update, then review its build. Public channel propagation
  and clean installed package tests remain downstream work.
- No Homebrew 4.0.2 automatic bump pull request existed when checked immediately
  after publication. Continue waiting for Homebrew's automation; do not replace
  it with a manually opened simple-bump pull request.

## Strict Snap experiment for v4.0.3 development

The Snap recipe is now deliberately staged as `confinement: strict` with the
`home` interface instead of classic confinement. `.github/workflows/snap.yml`
can be manually dispatched with `publish_edge=true` to publish the exact built
artifact to the `edge` channel for confinement testing before any stable
release. Do not promote this confinement change merely because the snap builds.

The decisive test is an installed Store snap operating on an ordinary project
under a normal non-hidden directory in `$HOME`: `init`, `build`, `status`,
tracking mutations and `build-auto` must all be able to read/write the
project-local `.nift/` state. Current Snap documentation describes `home` as
access to non-hidden home files; project-local hidden state is therefore the
known risk and must be tested empirically on the edge artifact. If strict
confinement cannot support arbitrary project-local `.nift/` state cleanly, do
not redesign Nift around Snap; revert to classic and retain the evidence.
