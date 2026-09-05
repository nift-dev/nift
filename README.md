# Nift ⚡

Nift is a fast, lightweight and flexible website generator written in C++. It tracks content, templates and dependencies so it can rebuild only the files that need rebuilding, while leaving the rest of your web stack entirely up to you.

Nift can be used for simple websites, documentation, generated text assets, or as a build layer alongside JavaScript, TypeScript, React, APIs and other tools.

For documentation, examples and downloads, visit **[nift.dev](https://nift.dev)**.

## Project status

Nift has completed its planned Checkpoints 0–10 deliberate hardening campaign. The current development tree is `Nift v4.0.5`, following the public v4.0.4 release, and the project has moved from synthetic hardening into **distribution, dogfooding and field evidence**. Existing regression, sanitizer, filesystem, parser, incremental and cross-platform gates remain maintained; new hardening work should be driven by concrete findings or newly justified guarantees rather than arbitrary checkpoint numbers.

## Features

- Fast, multithreaded builds and incremental rebuilds
- Simple templating with exactly-one rendered `@content`, `@input(...)`, `@pathto(...)`, `@dep(...)`, `@getenv(...)`, `@ent(...)`, structured inline or file JSON via `@json(...)`, Markdown/AsciiDoc/reStructuredText conversion via `@markup(...)`, project contracts via `$[...]`, bounded `@for` / `@if` control flow, short-circuit logical conditions, lazy ternary rendering, `@join`, UTF-8-safe `@substr`, and opt-in multi-output pagination
- Modified-time, hash and hybrid incremental build modes
- File and directory dependency tracking
- Automatic and explicit dependency support
- Human-friendly build errors with source locations
- `status` command showing what needs rebuilding and why
- Continuous `build --auto` mode
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
nift init --handover
```

`--handover` also writes a canonical `HANDOVER.md` in the project root so the
project is ready for AI-assisted and human-directed work.

A new project contains a persistent `.nift/.lock` file. It is Nift's normal
concurrency infrastructure: it exists so simultaneous Nift commands serialize
safely, and it stays after every build. Its presence does **not** mean a command
is running and never requires repair — unlike `.nift/.unfinished`, which is
evidence that a mutating operation failed, was interrupted, or otherwise was not
proven to finish, and requires `nift build --repair`.
`.nift/.lock` is automatically ignored by the generated `.gitignore`, so it is
never committed. Do not delete it while Nift may be running, and do not create
it by hand — older projects acquire it automatically on their next build.

See [`docs/PLATFORM-TARGETS.md`](docs/PLATFORM-TARGETS.md) for the supported
targets, generated files, extension contract, and platform boundaries.

Build incrementally (rebuilds only what changed):

```bash
nift build
```

Rebuild every tracked page:

```bash
nift build --all
```

Or watch continuously:

```bash
nift build --auto
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

Objects can be iterated with `@for((key, val) : object){...}`. Loops expose reserved lexical metadata through `$[loop.index]`, `$[loop.index0]`, `$[loop.first]`, `$[loop.last]` and `$[loop.length]`, and can be stably ordered with `by ... asc|desc`. Pure `$[...]` value expressions support numeric `+`, `-`, `*`, `/` and integer-valued `%`, comparisons, `!`, short-circuit `&&` / `||`, and parentheses with conventional precedence. Conditions use the same value-expression semantics and truthiness rules. Lazy `$[condition ? true : false]` rendering uses the shared evaluator and parses only the selected branch. Small `@join` and UTF-8-safe `@substr(value, pos, length)` helpers cover presentation-oriented string work without introducing a general scripting runtime.

A tracked entry can also opt into pagination with a positive `items-per-page` value. `@item{...}` captures rendered items, exactly one `@paginate` inserts the paginated result, pagination templates receive `$[paginate.items]`, `$[paginate.current]`, `$[paginate.total]`, `$[paginate.first]`, `$[paginate.last]`, `$[paginate.previous]` and `$[paginate.next]`, and `@pathtopage(n)` resolves absolute generated page links. Signed forms such as `@pathtopage(+1)`, `@pathtopage(-1)` and `@pathtopage(+$[offset])` resolve relative to the current pagination page. The complete generated page set remains one tracked dependency/invalidation unit even though its pages may render concurrently.

## Documentation

Full documentation is available at **[nift.dev](https://nift.dev)**.

The website covers installation, project structure, tracking, templates, dependencies, incremental builds, configuration, commands and examples.

## Building from source

Nift requires a C++17-capable compiler and GNU Make. The supported build uses the standard C++ library and pthreads; there is no project-local runtime/package dependency tree.

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
