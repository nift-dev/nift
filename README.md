# Nift ⚡

Nift is a fast, lightweight and flexible website generator written in C++. It tracks content, templates and dependencies so it can rebuild only the files that need rebuilding, while leaving the rest of your web stack entirely up to you.

Nift can be used for simple websites, documentation, generated text assets, or as a build layer alongside JavaScript, TypeScript, React, APIs and other tools.

For documentation, examples and downloads, visit **[nift.dev](https://nift.dev)**.

## Features

- Fast, multithreaded builds and incremental rebuilds
- Simple templating with `@content`, `@input(...)`, `@pathto(...)`, `@dep(...)`, `@getenv(...)` and `@ent(...)`
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
nift init .html
```

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

## Documentation

Full documentation is available at **[nift.dev](https://nift.dev)**.

The website covers installation, project structure, tracking, templates, dependencies, incremental builds, configuration, commands and examples.

## Building from source

Nift requires a C++17 compiler and `make`.

```bash
make
```

The standalone JSON tests can be run with:

```bash
make test-json
```

## License

Nift is released under the [MIT License](LICENSE).

Copyright © Nicholas Ham and Nift contributors.
