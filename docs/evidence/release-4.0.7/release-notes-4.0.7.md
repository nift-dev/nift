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