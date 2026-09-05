# Nift v4.0.10

Nift 4.0.10 embeds the approved Markup++ converter and its vendored cmark
engine, adds the `@markup` directive for Markdown, AsciiDoc and
reStructuredText, and reworks `@json` around six name-first forms with inline
JSON bodies and named schemas.

## Embedded Markup++ and `@markup`

- Nift embeds the approved Markup++ C++17 library and its vendored cmark 0.31.1
  engine under `markuppp/`, synchronized byte-for-byte with the standalone
  repository (`make test-markuppp-sync`; 41 canonical files).
- The new `@markup(format){...}` inline and `@markup(format, path)` file-backed
  directives render Markdown, AsciiDoc or reStructuredText. Formats are `md`,
  `adoc` and `rst`, with long aliases `markdown`, `asciidoc` and
  `restructuredtext`.
- Conversion follows a fixed ordering: resolve the source, record the
  dependency, evaluate Nift template syntax in the current scope, convert once
  through Markup++, then append the generated HTML directly. Generated HTML is
  never fed back through the Nift template parser, so directive-like text
  produced by conversion remains literal.
- File sources and host-resolved AsciiDoc/RST includes are project-bound,
  automatically recorded dependencies. Includes participate in the same
  input-loop tracking as `@input`, so a cycle that closes through a nested
  `@markup` directive is reported as a cycle rather than exhausting the parse
  depth guard.
- Markdown retains Markup++'s raw-HTML default; AsciiDoc `pass:[...]`/`++++`
  passthrough remains available; reStructuredText raw directives fail closed
  with a documented converter diagnostic.
- Inline `@markup`/`@json` brace counting ignores quoted strings, comments and
  backtick code spans/fences so braces inside Markdown samples and JSON strings
  do not corrupt the block.

## Name-first `@json` with six forms

`@json` is now strictly name-first. The supported forms are:

- File-backed: `@json(name, path)`, `@json(name, schema-path, path)`,
  `@json(name, schema-name, path)`.
- Inline: `@json(name){...}`, `@json(name, schema-path){...}`,
  `@json(name, schema-name){...}`.

- Inline bodies are evaluated by the Nift template language before JSON
  parsing, so existing values can supply scalar content.
- Binding names and named-schema references are static identifiers. In the
  schema position a bare identifier is a schema binding name that must already
  exist; a quoted string is a schema path. A missing bare schema name is a
  build error and never falls back to a same-named file.
- Schema bindings are immutable and reusable: an earlier inline schema can
  validate later file-backed or inline data. Validation happens before the new
  binding becomes visible, and failed validation never creates a partial
  binding.
- Data and schema files are automatic page dependencies; inline JSON adds no
  file dependency. Traversal, absolute-path escape, symlink escape and
  unreadable files remain controlled.

## Breaking change: path-first `@json` retired

The old path-first forms `@json(path, name)` and `@json(path, name, schema)`
are no longer supported. Templates using them fail with a clear identifier
diagnostic; the independent `nift-regression-suite` has been migrated to the
name-first contract.

## Embedded converter binding and packaging support

- The C ABI library, Go, C#, Node and Python bindings now compile and link the
  complete Markup++ C++ sources and all fifteen cmark C sources.
- Python wheel/sdist packaging stages and builds the full embedded converter
  source set, and the sdist manifest includes the required C and `.inc` files.
- `make clean` removes every binding, packaging and Python-cache build product.

## Bounded compatibility wording

- Markdown: CommonMark 0.31.2 compliant (652/652 official examples).
- AsciiDoc: bounded Asciidoctor-core 2.0.26 profile (AD0–AD11, AC0–AC9). This
  is not "Asciidoctor compatible" with every extension and never implies drop-in
  compatibility.
- reStructuredText: bounded Docutils-core 0.23 profile (RST0–RST14). This is
  not Docutils/Sphinx compatible beyond the documented profile.

## Archives

- `nift-4.0.10-linux-x86_64.tar.gz`
- `nift-4.0.10-macos-arm64.tar.gz`
- `nift-4.0.10-macos-x86_64.tar.gz`
- `nift-4.0.10-windows-x86_64.zip`
- `SHA256SUMS`

## Install

Installation methods: GitHub release archives, the curl installer, Snap,
Chocolatey, Homebrew and Flathub.