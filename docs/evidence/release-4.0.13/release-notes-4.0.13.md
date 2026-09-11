# Nift v4.0.13

Nift 4.0.13 advances the embedded Minify++ to the standalone 1.1.3
implementation and strengthens release and packaging safety. It contains no
change to the user-facing Nift language surface: the canonical `@path(...)`
spelling and the retained `@pathto(...)` compatibility spelling, dependency
tracking, required-file behaviour, escaping, interpolation and `reqs`
behaviour are unchanged since v4.0.12.

## Synchronized embedded Minify++ 1.1.3

- The embedded Minify++ is byte-identical to the standalone 1.1.3 content for
  the 24-file synchronization contract (implementation, headers, CLI, release
  notes, tests and support scripts).
- The Minify++ CLI smoke now derives its expected version from the single
  authoritative `cli/main.cpp`, so a version bump can never leave the assertion
  stale.
- The public Minify++ API format version remains `1`.

## Reliability and release-safety improvements

- A fail-closed version-consistency check now runs on every push, pull request
  and manual dispatch: it asserts that the executable version in `src/CLI.cpp`
  and the Snap metadata version in `snap/snapcraft.yaml` agree, and that any
  expected or tag version matches both. A disagreement fails before any
  packaging or publication step.
- The GitHub release workflow now publishes only the release archives and then
  stops; Chocolatey, Homebrew and Snap are separate manual steps that require
  a distinct approval. A successful GitHub release no longer automatically
  starts package-manager work.
- The Performance regression guards workflow is now manually dispatchable so a
  complete pre-release matrix can be run against a single candidate commit.

## Compatibility

- `@path(...)` remains the canonical checked project-path spelling.
- `@pathto(...)` remains fully supported and byte-identical in behaviour to
  `@path(...)`; old projects continue to build unchanged.
- `@pathtopage(...)` is a distinct pagination directive and is unchanged.

## Evidence bounds

- Conformance evidence is structural, lexical and runtime projection at pinned
  upstream revisions; it is not a claim of complete language or rendering
  conformance. The conformance checkpoint revisions are unchanged from v4.0.12.