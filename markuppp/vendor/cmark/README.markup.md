# Vendored cmark

Markup++ vendors the parser and HTML renderer from cmark 0.31.1 as its strict
CommonMark engine. The source is built into Markup++; users do not need a
system-installed cmark library.

- Upstream: <https://github.com/commonmark/cmark/tree/0.31.1>
- Release: 0.31.1
- License: BSD 2-Clause; see `COPYING`
- Local generated headers: `cmark_export.h` and `cmark_version.h`
- Excluded upstream components: CLI, XML/LaTeX/man/CommonMark renderers, build
  system and tests

Do not modify vendored parsing behavior without a corresponding upstream issue
or a documented Markup++ compatibility patch and regression.
