# Nift v4.0.11

Nift 4.0.11 is a correctness and release-hardening maintenance release driven
by an adversarial WPT-based review of the embedded Minify++ minifier. It
standardizes Nift source comments on opaque forms, syncs WPT-derived Minify++
correctness fixes (including a CSS escape-whitespace defect found only after the
independent conformance oracle was strengthened), and decouples Snap Store
promotion from the GitHub release workflow.

## Opaque Nift source comments

- Single-line `@//` comments and multiline `@/* ... */` comments are removed
  from the generated output and their bodies are never parsed or executed.
- `@#` is ordinary output text rather than comment syntax, so embedded foreign
  source (for example CSS test data) can never silently become a Nift comment.
- The historical `<#-- ... --#>` multiline form is gone. Comment bodies cannot
  execute directives, metadata, dependencies or braces, and an unclosed
  `@/*` fails the build with a source-located diagnostic.

## WPT-driven embedded Minify++ correctness

The embedded Minify++ was validated against 31,155 independently sourced
Web Platform Test cases at WPT `b89af32bc8f4` using a browser-oracle harness
that parses both the original and transformed CSS in Chromium. The campaign
fixed and synchronized:

- browser-style CSS EOF recovery for unterminated comments and strings, and
  bad-string newline handling so following CSS remains visible;
- CSS token-boundary preservation for nesting selectors (`& .child`,
  `.ancestor &`), escaped `::part()` identifiers, attribute-selector namespace
  whitespace, and malformed declaration/block recovery.

## CSS escape-whitespace fix

An adversarial re-review of the conformance oracle found that the structured
CSSOM comparison did not inspect `@counter-style` descriptors, so a real defect
compared as passing. Minify++ collapsed whitespace runs following CSS hex
escapes or escaped whitespace, merging what browsers tokenize as separate
identifiers:

```css
symbols: \2020  \2021   /* two symbols: dagger, double-dagger */
symbols: \2020 \2021    /* one identifier: dagger-double-dagger */
```

The oracle now inspects `@counter-style`/`@property`/`@font-palette-values`
descriptors and `@import`/`@charset`, and Minify++ preserves the whitespace
that separates escape-derived identifiers. Under the strengthened oracle the
full corpus returns to **31,155 / 31,155 passing** with zero
differences/errors/rejections/unverified cases.

## Snap release-workflow decoupling

- The GitHub release performs only non-publishing Snap validation; it does not
  receive Store credentials and does not wait for asynchronous Launchpad
  builders.
- Completed connected builds are promoted later through the manual
  `Promote completed Snap builds` workflow, which uses the pinned Snapcraft
  toolchain, exact-revision selection, strict candidate verification and
  explicit per-revision stable releases (never whole-channel promotion).

## Independent conformance harness

The `minify-conformance` repository's CSSOM oracle was strengthened permanently
so the escape-whitespace bug class (and any descriptor, `@import` or `@charset`
corruption) fails the ordinary harness if reintroduced, with focused
Chromium-backed oracle discrimination regressions.

## Archives

- `nift-4.0.11-linux-x86_64.tar.gz`
- `nift-4.0.11-macos-arm64.tar.gz`
- `nift-4.0.11-macos-x86_64.tar.gz`
- `nift-4.0.11-windows-x86_64.zip`
- `SHA256SUMS`

## Install

Installation methods: GitHub release archives, the curl installer, Snap,
Chocolatey, Homebrew and Flathub.