# Nift v4.0.8

Nift 4.0.8 fixes the build-progress output ordering so final summaries are
never interleaved with an active progress line, replaces the progress
presentation with a restrained green Braille spinner, and adds automated
all-architecture Snap release coordination.

## Build progress

- Deterministic progress shutdown: the renderer stops, joins, erases the
  complete transient line and flushes before any permanent output, so final
  summaries are never written into an active progress line.
- Full-line ANSI erasure on every frame and at shutdown, so resized terminals
  and shorter frames after longer ones leave no residue.
- Per-page build errors are buffered and emitted only after progress has
  stopped, keeping diagnostics and summaries on clean lines.
- Interactive progress shows a restrained green Aeye-style Braille spinner
  (`⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧ ⠇ ⠏`) in front of the stable `building N/M  P%`
  text; only the spinner is coloured, the text stays fixed, and short builds
  remain silent through the existing 200 ms display delay.
- Redirected/non-TTY output stays free of animation escape sequences and
  `NO_COLOR` renders the plain spinner.

## Snap release automation

- Release publication is coordinated for all six declared Snap architectures
  (amd64, arm64, armhf, ppc64el, riscv64, s390x) from the connected build
  service's `latest/edge` revisions: wait for the complete exact-version set,
  release exactly those revisions to `latest/candidate`, verify candidate
  strictly, run the amd64 candidate confinement smoke, promote to
  `latest/stable`, and verify stable.
- Legacy `i386` entries are ignored and reported in edge/stable and block
  candidate promotion rather than being promoted.
- A validated previous-stable rollback snapshot is required before promotion,
  the Snapcraft toolchain is pinned to an immutable revision with a
  non-publishing preflight, and the coordinator supports `--dry-run` rehearsal.

## Persistent project lock

- Every project now has a persistent `.nift/.lock` file. It is Nift's normal
  concurrency infrastructure: it exists so simultaneous Nift commands serialize
  safely and it stays after every build. Its presence does **not** mean a build
  is active or failed, and it never requires repair.
- New projects create it during `nift init`; older projects, including legacy
  `.ownership-gate` projects, acquire it automatically on their next build.
- `.nift/.lock` is ignored by Git. `.nift/.unfinished` remains the separate
  indicator that a mutating operation failed, was interrupted, or otherwise was
  not proven to finish and requires `nift build --repair`.

## Archives

- `nift-4.0.8-linux-x86_64.tar.gz`
- `nift-4.0.8-macos-arm64.tar.gz`
- `nift-4.0.8-macos-x86_64.tar.gz`
- `nift-4.0.8-windows-x86_64.zip`
- `SHA256SUMS`

## Install

Installation methods: GitHub release archives, the curl installer, Snap,
Chocolatey, Homebrew and Flathub.