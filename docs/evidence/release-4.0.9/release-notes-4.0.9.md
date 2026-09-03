# Nift v4.0.9

Nift 4.0.9 gives ordinary CSS and JavaScript direct ownership in the output
tree, makes build and status output consistently file-oriented, fixes a CLI
read-integrity defect that could silently render unreadable sources as empty,
and hardens Snap publication with honest rollback reporting and guarded
per-revision stable releases.

## Direct asset ownership

- `nift init` now creates the empty starter `style.css` and `script.js` directly
  under the configured output directory's `assets/css/` and `assets/js/`
  directories. It no longer creates a default `content/assets/` directory and no
  longer records those static files in `.nift/tracked.json`.
- These are ordinary user-owned frontend assets: builds neither copy nor rewrite
  them. Explicit tracking remains available when you want generated or minified
  assets with real build-time Nift processing.

## File-oriented terminology

- `nift build` and `nift status` output now consistently speaks about tracked
  files rather than pages, matching the product's general-purpose behavior.

## Read-integrity fix

- A tracked content file, `@input` source, or template that exists but cannot be
  read now fails the build with a clear `… is not readable` diagnostic and
  leaves the previously successful output intact. Previously the CLI could
  silently render such a source as empty content and report success. This fixes
  a latent CLI regression that also affected v4.0.7 and v4.0.8.
- An empty but readable file remains a valid, distinct state and builds normally.

## Snap publication hardening

- Snap stable publication is coordinated for all six declared architectures from
  the connected build service, using guarded per-revision `latest/stable`
  releases rather than whole-channel promotion. Rollback reporting now classifies
  any target-version stable entry as non-recoverable, so a failed or partial
  publication is reported honestly and a rerun preserves already-correct
  assignments.

## Archives

- `nift-4.0.9-linux-x86_64.tar.gz`
- `nift-4.0.9-macos-arm64.tar.gz`
- `nift-4.0.9-macos-x86_64.tar.gz`
- `nift-4.0.9-windows-x86_64.zip`
- `SHA256SUMS`

## Install

Installation methods: GitHub release archives, the curl installer, Snap,
Chocolatey, Homebrew and Flathub.