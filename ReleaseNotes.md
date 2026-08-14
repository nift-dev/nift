# Nift C++ Rewrite — Release Notes

## v1.0.9

### Installation and Makefile portability

- Added `make install` and `make uninstall`.
- Added conventional `PREFIX`, `BINDIR` and `DESTDIR` overrides for Unix packaging and custom installs.
- Unix-like systems install to `/usr/local/bin` by default.
- Windows GNU Make builds use an `.exe` target and default to a per-user Nift directory under `%LOCALAPPDATA%`.
- Removed the hard-coded `/tmp` path from the standalone JSON test; test binaries now live under the local `.build/` directory and are removed by `make clean`.

This file records the development history of the current C++ rewrite. The rewrite began as a clean implementation of the stripped Nift behaviour and has progressively become a replacement candidate.

## v1.0.8

### Presentation and repository polish

- Replaced the development-oriented README with a public-facing GitHub README covering Nift's purpose, quick start, templating model, incremental builds, commands, project structure, performance, testing and contribution guidance.
- Redesigned `nift about` as a proper project introduction with terminal colour, a compact Nift wordmark, a short description and the official `https://nift.dev` address.
- Added `about` to `nift commands`.
- Added this release history.

## v1.0.7

### Performance replacement-candidate pass

- Parallelised incremental `build_reasons` analysis used by `build-updated` and `status`.
- Replaced expensive filesystem-resolving relative-path operations on hot project paths with lexical path operations.
- Added bounded dependency-hash caching and per-build hash refresh deduplication.
- Reduced repeated hashing/writing of shared dependencies.
- Optimised hot page-info JSON serialization.
- Improved renderer hot paths and shared raw-source file reads without caching rendered partial output.
- Restored negative `build-threads` multiplier semantics.
- Ruthlessly optimised the standalone JSON implementation:
  - compact flat object storage instead of allocation-heavy tree maps;
  - faster common string parsing;
  - lower successful-parse diagnostic overhead;
  - streaming loading for large `tracked.json` arrays;
  - fewer temporary allocations.
- Reduced peak memory substantially on the 10,000-page development fixture.
- Reached parity with or outperformed stripped v0.9 across the principal 10,000-page modified/hash benchmarks used during development.
- Kept the deep regression suite green.

## v1.0.6

### Plain `build-auto` logs

- Added an explicit scoped plain-output mode to the console layer.
- Removed ANSI colour sequences from `.nift/build-auto.log`, including output emitted by build worker threads.
- Preserved normal colour in the interactive `build-auto` control banner.

## v1.0.5

### Quieter `build-auto`

- Removed empty `not-tracking` output from successful `info` queries.
- Redesigned `build-auto` as a quiet continuous mode.
- Added immediate `q` to quit on interactive terminals.
- Added `.nift/build-auto.log` for meaningful build output.
- Changed the log writer to overwrite the file only when its contents actually change.
- Kept non-interactive `build-auto` suitable for CI/test harnesses.

## v1.0.4

### Status and inspection output

- Limited elapsed-time output to finite build commands instead of printing it for every command.
- Redesigned `status` as a dry-run incremental analysis similar in spirit to `git status`.
- `status` now reports which pages need rebuilding and why without modifying outputs or page metadata.
- Added compact grouping for large fan-out status results.
- Redesigned `info`, `info-all`, `info-names`, `info-tracking` and `info-watching` around structured JSON-style output.
- Added syntax colouring for interactive JSON output.
- Made redirected inspection output strict plain JSON for scripting.

## v1.0.3

### Safer project creation

- Prevented `nift init` from running inside an existing Nift project.
- Moved the existing-project check before all filesystem mutations.
- Preserved support for initialising Nift inside an ordinary non-empty directory.

## v1.0.2

### Build summaries and explanations

- Added accurate built/up-to-date/failed result accounting.
- Added rebuild-reason reporting to `build-updated`.
- Added concise successful page counts to full and targeted builds.
- Added elapsed-time summaries to finite build commands.
- Added anti-spam grouping for large rebuild sets.
- Reserved the 📦 emoji for the single final successful build summary instead of printing it per page.
- Changed incremental builds to queue only pages that actually need rebuilding.

## v1.0.1

### Real-project compatibility fixes

- Allowed bare parameterised function names such as prose `@input` to remain literal text while preserving errors for malformed actual calls.
- Fixed compatibility with read-only `.info.json` files produced by stripped Nift by making metadata writable only when required and restoring its read-only state.
- Verified the rewrite against an early real Nift documentation-site redesign.

## v1.0.0

### Initial architecture-compatible rewrite

- Reimplemented stripped Nift in modern C++17 while retaining a familiar multi-file architecture.
- Added separate project, parser, watch, filesystem, CLI and build-progress components rather than a single translation unit.
- Added a standalone human-readable JSON implementation with `Document& operator[]` and explicit JSON value types.
- Implemented multithreaded builds.
- Implemented modified, hash and hybrid incremental modes.
- Implemented tracked pages, user dependencies, recursive directory dependencies and watch state.
- Implemented the stripped templating surface including `@content`, `@input`, `@pathto`, `@dep`, `@getenv` and `@ent`.
- Added structured parser/build diagnostics with tracked name, source path, line/column and source context where available.
- Added TTY-aware colour and cleaner command output.
- Added delayed build-progress reporting so fast builds remain silent while longer builds show progress.
- Reached full compatibility with the then-current 280-assertion deep regression suite.

---

The rewrite version is intentionally separate from Nift's public product version while the replacement implementation is being evaluated.
