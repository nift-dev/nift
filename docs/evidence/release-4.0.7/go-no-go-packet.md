# Nift 4.0.7 release candidate — go/no-go packet (CLI-RC04R, revised)

CLI-RC04R repair checkpoint. Nothing was tagged, released, published or
deployed. This packet is the review deliverable; `v4.0.7` execution (CLI-RC05),
post-release verification (CLI-RC06) and the 4.0.8 development bump (CLI-RC07)
remain deferred pending explicit authorization.

## 1. Candidate heads and tag target

| Repository | Head | Role |
|---|---|---|
| canonical `nift` (behavioral/documentation base) | `c4faede` | binary behaviour identical to the accepted P8 head `71db053`; +HANDOVER identity fix |
| canonical `nift` (evidence v1) | `ba96321` | go/no-go packet v1 + release-notes draft |
| canonical `nift` (CLI-RC04R workflow repair) | `0a009c0` | release-workflow repair (permissions, rehearsal mode, notes-file, fail-closed publish, action pinning) |
| canonical `nift` (proposed `v4.0.7` tag target) | `TBD` | = the final CLI-RC04R commit recording this revised packet (named precisely after it is committed) |
| website source (`nift-dev.github.io` `stage`) | `c50d4bc` | docs audit |
| website generated (`public/main`) | `02ceb29` | rebuilt public site |
| CLI contract suite (`nift-regression-suite`) | `eaae07a` | reconciled to the unified CLI grammar (on `6bcf539`) |
| `nift-embed` | `a63e6e5` | unchanged, unarchived, outside the release |
| `nift-rs` | `37db797` | unchanged, outside the release |

Version: **Nift 4.0.7** (`src/CLI.cpp:34`). Post-release dev identity: **4.0.8**.

The packet records its own evidence commits layered over candidate base
`c4faede`; the external go/no-go response names the exact proposed tag head
(field above, filled after this commit). Authorization must identify that one
immutable commit.

## 2. Four-platform release rehearsal (native runners, non-publishing)

Run: **`33142977130`** (workflow_dispatch, `version=4.0.7`, commit `0a009c0`) —
**completed/success**. Per-job conclusions:

| Job | Result |
|---|---|
| unix (ubuntu-latest, linux-x86_64) | success — `archive smoke PASS: linux-x86_64`, `Nift v4.0.7` |
| unix (macos-15, macos-arm64) | success — `archive smoke PASS: macos-arm64`, `Nift v4.0.7` |
| unix (macos-15-intel, macos-x86_64) | success — `archive smoke PASS: macos-x86_64`, `Nift v4.0.7` |
| windows (windows-latest) | success — `archive smoke PASS: windows-x86_64`; MinGW-runtime rejection check PASS |
| installer-preflight | success (`make test-installer`) |
| rehearse | success — exact four-asset set validated, SHA256SUMS built |
| publish | **skipped** (dispatch is non-publishing) |
| installer-public-smoke | skipped (requires publish) |

Each build job extracts its own archive on its native runner and runs the
extracted binary `version` + a fresh-project `init` → `build --all` → `status`
smoke; the Windows job also asserts the extracted executable has no MinGW
runtime DLL dependency. The rehearsal and the real release share the same
`release.yml` build/staging logic — there is no second packaging implementation.

### 2.1 Artifacts (rehearsal bundle `rehearsal-4.0.7`, all SHA-256 verified)

| Artifact | Size (bytes) | SHA-256 |
|---|---|---|
| `nift-4.0.7-linux-x86_64.tar.gz` | 489 356 | `f8a6a3314b090a3b3ee5443a953775c4b8bb2be6d2b0ac916f82e340797bc221` |
| `nift-4.0.7-macos-arm64.tar.gz` | 372 543 | `1f41d83adb027d68849eb063ac70ad54a0ec50126585418cdfebe2e9f1bec597` |
| `nift-4.0.7-macos-x86_64.tar.gz` | 396 521 | `7478581d02275ad19eec8bd60e05236625d4c7eab62e067831ed637415390818` |
| `nift-4.0.7-windows-x86_64.zip` | 1 388 263 | `cfdb704076f66b0b82bffb0c34ac1f08c484622b31c12a45d9179e776bf4ef32` |
| `SHA256SUMS` | 386 | all four entries verified (`sha256sum -c` OK) |

### 2.2 Contained paths and executable architecture (independently inspected)

- `nift-4.0.7-linux-x86_64/nift`, `README.md`, `LICENSE` — `nift` = ELF 64-bit x86-64, runs `Nift v4.0.7`, no missing dynamic dependencies.
- `nift-4.0.7-macos-arm64/nift`, `README.md`, `LICENSE` — `nift` = Mach-O 64-bit arm64.
- `nift-4.0.7-macos-x86_64/nift`, `README.md`, `LICENSE` — `nift` = Mach-O 64-bit x86_64.
- `nift-4.0.7-windows-x86_64/nift.exe`, `README.md`, `LICENSE` — `nift.exe` = PE32+ x86-64 console.

No linux-arm64 archive (established decision; Snap provides Linux ARM64).

## 3. Test evidence

### 3.1 Local (Linux, candidate binary)
`make test` PASS; external contract suite **22/22 modules + historical/ruthless PASS**; ASan/UBSan clean; performance/scaling + memory guards PASS; P7 reduced-CLI isolation gate PASS.

### 3.2 Remote CI at the CLI-RC04R commit `0a009c0`
- Release rehearsal run `33142977130`: success (see §2).
- Test integrity guards: **success**.
- Checkpoint 10 cross-platform equivalence: **success**.
- Init targets: **success**.
- Performance regression guards: initial run failed on full-build scaling
  (7.93x vs 7.00x threshold) on the shared runner; **re-run passed** (success).
  The commit changed only `.github/workflows/*.yml`, so this was a
  machine-load flake consistent with the documented variance of these guards.
- Packaging matrix: last success at `71db053` (unchanged; not touched by this push).

## 4. Release-workflow audit (corrected)

- **Permissions**: workflow-scope `permissions: contents: read`; only the
  `publish` job declares `permissions: contents: write`. The unix/windows/
  installer-preflight/rehearse/smoke jobs have read-only contents.
- **Action pinning**: every third-party release/publishing action across
  `release.yml`, `snap.yml`, `chocolatey.yml`, `homebrew.yml` and
  `distribution-verification.yml` is pinned to a reviewed full commit SHA:
  `actions/checkout@11d5960a…` (v4) and `fbc6f3992…` (v5),
  `actions/upload-artifact@ea165f8d…`, `actions/download-artifact@d3f86a10…`,
  `msys2/setup-msys2@66cd2cce…`, `snapcore/action-build@b391f430…`,
  `snapcore/action-publish@9334eecb…`, `actions/setup-python@ece7cb06…`
  (`actions/setup-homebrew` was already SHA-pinned). No mutable major-tag
  references remain in the release path.
- **Reviewed release notes are used**: `publish` checks out the exact tag and
  runs `gh release create … --notes-file docs/evidence/release-$version/
  release-notes-$version.md` (no `--generate-notes`). The exact body that will
  be published is the reviewed file `docs/evidence/release-4.0.7/
  release-notes-4.0.7.md` (contents in §8). The publish job fails if the notes
  file is absent from the tagged commit.
- **Fail-closed existing release**: if a release for the tag already exists, the
  publish job verifies (a) the exact four-asset set + `SHA256SUMS` are present,
  (b) each asset's SHA-256 equals the freshly built artifact, and (c) the
  release body equals the reviewed notes file; any mismatch fails the job.
  An arbitrary or stale release is never silently accepted.

## 5. Installer / upgrade / checksum evidence
- `make test-installer` (preflight): PASS (in CI and locally).
- Checksum positive (`sha256sum -c` on the rehearsal bundle): all four OK.
- Checksum negative (tampered archive): detected.
- Upgrade/reinstall smoke over an existing prefix: PASS.
- Per-archive extracted `init` → `build --all` → `status` smokes: PASS on all
  four native runners (§2).

## 6. Documentation changes and no-leak proof
Unchanged from packet v1 and re-verified: 18 website pages updated (stage
`c50d4bc`, public `02ceb29`); site built 75/75 with the candidate;
`check_handover_display.py` PASS; homepage diff empty; zero Embedded
Nift/SSR/binding promotion in `content/` + `public/`; canonical `HANDOVER.md`
identity fixed (`c4faede`).

## 7. Proposed tag and release notes
- Tag: `v4.0.7` (annotated) at the exact tag target in §1.
- Release body = the reviewed notes file (below), published verbatim by the
  workflow (`--notes-file`). No Embedded Nift mention.

## 8. Reviewed release body (exact text the workflow will publish)

```text
# Nift v4.0.7

This release unifies the build and inspection command grammar and adds an
explicit repair path for interrupted builds.

## Build

- `nift build [names...]` — incremental build, or build the named pages explicitly.
- `nift build --all` — build every tracked page.
- `nift build --auto` — continuous watch build (polling every 200 ms; press `q` to stop in an interactive terminal).
- `nift build --repair` — repair/reconstruct derived build state after an interrupted or failed build.
- The historical verbs `build-all`, `build-updated`, `build-names` and `build-auto` are removed; invoking them now reports an error with the replacement spelling.

## Inspect

- `nift info [names...]` — resolved metadata for the named pages, or all tracked entries when no names are given.
- `nift info --all`, `nift info --names`, `nift info --tracking`, `nift info --watching`.
- The historical `info-all`, `info-names`, `info-tracking` and `info-watching` verbs are removed.

## Other changes

- The `-n` and `-s` display flags on `build` and `status` are removed (rejected as unknown options); `-p` remains for full per-page detail.
- A build that does not complete successfully leaves an `.unfinished` marker; the next build refuses with a clear diagnostic, and `nift build --repair` reconstructs the derived build state.

## Archives

- `nift-4.0.7-linux-x86_64.tar.gz`
- `nift-4.0.7-macos-arm64.tar.gz`
- `nift-4.0.7-macos-x86_64.tar.gz`
- `nift-4.0.7-windows-x86_64.zip`
- `SHA256SUMS`

## Install

Installation methods: GitHub release archives, the curl installer, Snap,
Chocolatey, Homebrew and Flathub.
```

## 9. Normal package-channel execution plan (CLI-RC05/RC06)
Unchanged: tag → `release.yml` (unix ×3, windows, installer-preflight, publish,
installer-public-smoke ×3) → Snap stable (amd64/arm64) → Chocolatey
(moderation downstream) → Homebrew auto-bump (no manual PR) → Flathub external
PR → `distribution-verification.yml` once stores propagate. npm/PyPI/NuGet/
crates.io deferred (not a 4.0.7 gate).

## 10. Planned 4.0.8 post-release bump (CLI-RC07)
Unchanged: advance to `Nift v4.0.8` in `src/CLI.cpp`, `snap/snapcraft.yaml`,
`ReleaseNotes.md`, `docs/guarantees/registry.json`; update regression-suite
assertions; commit + push `nift` + `nift-regression-suite` separately; write the
v4.0.7 report in `docs/handover/RELEASES.md`.

## 11. Repository cleanliness
- canonical `nift`: clean (0 porcelain, 0 residue).
- website `stage` `c50d4bc` + `public/main` `02ceb29`: clean.
- `nift-regression-suite` `eaae07a`: clean.
- `nift-embed` `a63e6e5`, `nift-rs` `37db797`, `nift-embed-regression-suite`
  `be6c28b`: clean and unchanged.

## 12. Known limitations
Unchanged: binaries unsigned; release is CLI-only; embedded engine/bindings/
corpus/Rust in-tree but unpublished and unpromoted.