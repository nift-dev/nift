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

The workflow creates the GitHub release only after all platform jobs succeed.
This is build automation, not evidence that every release-candidate gate has
been performed; record that evidence before tagging.

## Snap

The Snap is named `nift` and supports every architecture currently listed by
Snapcraft's Launchpad remote-build service: amd64, arm64, armhf, ppc64el,
riscv64, and s390x. It runs `usr/bin/nift`, uses classic confinement because
Nift is a CLI build tool operating on arbitrary user projects, and builds the
same portable C++ source through its Makefile. An architecture-specific build
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

Before a stable release, install the produced `.snap` on a clean representative
host and exercise version/help, project creation, a real build, dependency
tracking, and filesystem behavior under classic confinement.

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

The historical package embedded `nift.exe` and a duplicate `nsm.exe`, depended on
Git, and included hand-maintained verification checksums. The rewrite instead
downloads the immutable GitHub release, verifies its checksum automatically, and
packages only `nift.exe`. It has no Git dependency. Generated
`VERIFICATION.txt` remains available to Chocolatey moderators.

## Homebrew

`brew install nift` is owned by the external `Homebrew/homebrew-core` formula.
As inspected on 17 August 2026, it still publishes legacy Nift 3.0.3 from
`nifty-site-manager/nsm`, depends on LuaJIT, applies two legacy patches, and tests
the `nsm` command. Its API marks the formula for automatic version bumping, but
the old source URL cannot discover tags created in `nift-dev/nift`.

`packaging/homebrew/nift.rb.in` is the upstream formula for the rewrite.
`homebrew.yml` resolves the immutable source archive and checksum and tests the
formula on Homebrew for macOS arm64 and Linux x86-64. It is called after a GitHub
release and uploads the resolved formula as an artifact; it does not push to
`homebrew-core` or manufacture bottle checksums.

For the first rewritten release, use the generated formula in a normal
`Homebrew/homebrew-core` pull request, changing the source to `nift-dev/nift` and
removing LuaJIT, the legacy patches, and `nsm` expectations. Homebrew's own CI
and maintainers build and attach official bottles. Once the canonical formula
points to the new repository, its automatic bump service should be able to find
later tags, but verify every update rather than assuming it occurred.

## Flatpak and Flathub

Nift is already published on Flathub with immutable app ID `cc.nift.nsm`. Do not
submit it as a new app or change the ID as part of a normal update. As inspected
on 17 August 2026, the canonical `flathub/cc.nift.nsm` manifest still publishes
2.4.12 and builds the legacy `nifty-site-manager/nsm-flatpak` source, plus bundled
Git and LuaRocks and a legacy patch.

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
- Homebrew's `Homebrew/homebrew-core` `nift.rb` must be changed by pull request.
  The in-repository template cannot redirect `brew install nift` by itself.

Archive the old repositories only after the public store entries use the new
source and fresh installations have been verified. Add a short archived-repo
README pointing at `nift-dev/nift` where practical.

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

## Release sequence

```text
validated release candidate
  -> reconcile public version and package recipe
  -> create and push approved signed/annotated tag
  -> GitHub artifacts and checksums complete
  -> install/test the actual archives
  -> Snap build/install test and approved channel publication
  -> Chocolatey VM test and approved submission
  -> Homebrew generated-formula test and homebrew-core update
  -> Flathub external-manifest update, lint, review and publication
  -> record exact URLs, identities, checksums, outcomes and remaining limits
```

Store publications are downstream results and may complete at different times.
Never describe a release as available through a package manager until its public
store entry resolves to the intended version and a fresh installation succeeds.
