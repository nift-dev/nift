# Nift ⚡

Nift is a fast, lightweight and flexible website generator written in C++. It tracks content, templates and dependencies so it can rebuild only the files that need rebuilding, while leaving the rest of your web stack entirely up to you.

Nift can be used for simple websites, documentation, generated text assets, or as a build layer alongside JavaScript, TypeScript, React, APIs and other tools.

For documentation, examples and downloads, visit **[nift.dev](https://nift.dev)**.

## Project status

Nift has completed its planned Checkpoints 0–10 deliberate hardening campaign. The current development tree is `Nift v4.0.2`, and the project has moved from synthetic hardening into **distribution, dogfooding and field evidence**. Existing regression, sanitizer, filesystem, parser, incremental and cross-platform gates remain maintained; new hardening work should be driven by concrete findings or newly justified guarantees rather than arbitrary checkpoint numbers.

## Features

- Fast, multithreaded builds and incremental rebuilds
- Simple templating with `@content`, `@input(...)`, `@pathto(...)`, `@dep(...)`, `@getenv(...)`, `@ent(...)` and structured JSON data via `@json(...)`, config-declared project contracts via `$[...]`, plus constrained `@for(...){...}` / `@if(...){...}` control flow
- Modified-time, hash and hybrid incremental build modes
- File and directory dependency tracking
- Automatic and explicit dependency support
- Human-friendly build errors with source locations
- `status` command showing what needs rebuilding and why
- Continuous `build-auto` mode
- JSON-based project and build metadata
- No prescribed frontend framework or application architecture
- Small native C++ executable with no runtime framework dependency

## Getting started

Build and install Nift:

```bash
make
sudo make install
```

On Unix-like systems this installs `nift` to `/usr/local/bin` by default. Custom prefixes and package staging are supported, for example:

```bash
make install PREFIX="$HOME/.local"
make install DESTDIR="/tmp/package-root" PREFIX="/usr"
```

Use `make uninstall` with the same `PREFIX` if you want to remove a manual installation.

Create a project:

```bash
mkdir my-site
cd my-site
nift init
```

The standard starter is HTML. Use `--ext=.php` (or another extension) for a
different generic project, or prepare a supported static host directly:

```bash
nift init --target=vercel
nift init --target=cloudflare
```

See [`docs/PLATFORM-TARGETS.md`](docs/PLATFORM-TARGETS.md) for the supported
targets, generated files, extension contract, and platform boundaries.

Build the project:

```bash
nift build-all
```

Rebuild only files affected by changes:

```bash
nift build-updated
```

Or watch continuously:

```bash
nift build-auto
```

Run:

```bash
nift commands
```

for the built-in command reference.

## Example

A Nift template can be as simple as:

```html
<!doctype html>
<html lang="en">
<head>
    @input("templates/head.html")
</head>
<body>
    @content
</body>
</html>
```

Nift renders the tracked page's content at `@content`, processes inputs and dependencies, and records enough information to make subsequent builds incremental.

Structured project data can be loaded directly from JSON:

```html
@json("data/site.json", site, "schemas/site.schema.json")
<h1>$[site.title]</h1>
<p>$[site.sections[3].items[0].label]</p>
```

JSON object/member and array/index access can be chained arbitrarily. The optional third argument validates the document against Nift's documented JSON Schema subset before binding it. Both data and schema files automatically become page dependencies, and parsed documents are shared immutably across pages during a build.

When structured data needs repetition or selection, Nift keeps the control-flow surface deliberately small:

```html
@for(item : site.items by item.title asc){
    @if(item.visible){
        <a href="$[item.url]">$[loop.index]. $[item.title]</a>
    }
}
```

Objects can be iterated with `@for((key, val) : object){...}`. Loops expose reserved lexical metadata through `$[loop.index]`, `$[loop.index0]`, `$[loop.first]`, `$[loop.last]` and `$[loop.length]`, and can be stably ordered with `by ... asc|desc`. Conditions support truthiness, `!`, equality and strict numeric/string ordering comparisons over scalar JSON values/page metadata; there is intentionally no general scripting or expression runtime.

## Documentation

Full documentation is available at **[nift.dev](https://nift.dev)**.

The website covers installation, project structure, tracking, templates, dependencies, incremental builds, configuration, commands and examples.

## Building from source

Nift requires a C++17 compiler and `make`.

```bash
make
```

The focused parser/data tests can be run with:

```bash
make test-json
make test-json-binding
make test-control-flow
make test-content
make test-comments
```

## License

Nift is released under the [MIT License](LICENSE).

Copyright © Nicholas Ham and Nift contributors.


## Architectural rules

The stripped rewrite is guided by an explicit design checklist in [`ARCHITECTURE_RULES.md`](ARCHITECTURE_RULES.md). The short version is that Nift owns dependency-aware build-time composition and may optionally optimise final outputs, while source-language compilation, arbitrary shell execution and neighbouring tool domains remain external.

The embedded `minifypp/` subtree follows the same boundary: it is a self-contained library/CLI/test project that Nift consumes through a public header, so it can be extracted later without depending on Nift's project model.
