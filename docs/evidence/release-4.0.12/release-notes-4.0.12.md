# Nift v4.0.12

Nift 4.0.12 makes `@path(...)` the canonical checked project-path spelling,
retains the legacy `@pathto(...)` compatibility spelling, and synchronizes the
embedded Minify++ to the standalone 1.1.2 implementation. It also carries
release-workflow and action-runtime maintenance that does not change the
language surface.

## Canonical `@path(...)`

- `@path(...)` is the recommended spelling for checked, project-aware links to
  tracked pages and local assets.
- It uses exactly the same parser branch, interpolation, escaping, tracked-name
  lookup, concrete project-file lookup, current-output-relative path
  calculation, tracked-`404` root-absolute behavior, project-boundary and
  symlink validation, path-context errors, requirement registration and
  incremental invalidation as the compatibility spelling.
- Nift's maintained fixtures, generated init projects, the v0.0.8 canonical
  handover, examples, workflow fixtures, regression mirrors, cross-platform
  scenarios and the dogfood website were migrated to `@path(...)`.

## Retained `@pathto(...)` compatibility

- `@pathto(...)` remains supported and is not removed or deprecated. Old
  projects continue to build unchanged.
- Rendering, dependency tracking, requirement registration, error handling,
  escaping, interpolation and `.info.json` `reqs` are byte-identical between
  `@path(...)` and `@pathto(...)`. Diagnostics name the spelling actually
  written while sharing the same failure class and source location.
- `@pathtopage(...)` is a distinct pagination directive and is unchanged.

## Synchronized embedded Minify++ 1.1.2

- The embedded Minify++ is byte-identical to the standalone 1.1.2 content for
  the 24-file synchronization contract, including implementation, headers,
  CLI, release notes, tests, generated/adversarial gates and support scripts.
- The embedded copy carries the conformance-hardened HTML, JavaScript and
  JSX/TSX correctness fixes (recoverable HTML attribute quotes, template-literal
  regular-expression lexing, JSX root/ASI boundaries) with their smoke
  regressions.

## Evidence bounds

- Conformance evidence is structural, lexical and runtime projection at pinned
  upstream revisions; it is not a claim of complete language or rendering
  conformance.
- Reviewed checkpoints: HTML 9,651/9,651 (WPT `aed18189…`); JavaScript 48,011
  eligible with all 39,741 runnable on the pinned Node runtime passing and zero
  transformed failures (Test262 `419d3e0a…`); JSX/TSX 221/221 (TypeScript
  `1e4744d…`); JSON 93/93 (`1ef36fa…`); XML 535/535 (SHA-256-pinned W3C archive);
  SVG 1,176/1,176 (WPT `aed18189…`); CSS 31,155/31,155 at WPT `a53926ac…` (the
  living CSS dashboard continues to track upstream WPT at its own recorded
  revision).

## Packaging and action-runtime maintenance

- Release workflows pin actions to Node-24-backed majors and build release
  candidates from the reviewed product commits; this is CI/release engineering
  and does not change the user-facing language.
- The canonical handover is v0.0.8 and teaches `@path(...)`.