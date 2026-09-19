# Nift package contract v1

A package is a Git/local repository containing `manifest.json`, Nift `.f` sources, tests and documentation. `manifest.json` requires `name`, `version`, and an `.f` `entry`. Package code uses the existing isolated `@import`/explicit `export` semantics.

Resolution contract:
- `@import("./local.f")` and other path-shaped imports are local files.
- `@import("sqlite")` is a package import resolved from the installed package root.
- package imports execute the package manifest `entry` in isolated scope and expose only explicit exports.
- package-local relative imports/resources resolve relative to the package entry/module that requests them, never the consuming project's current directory.
- dependencies are declared by package name in `manifest.json`; acquisition/version locking is CP31+ and is intentionally separate from this module contract.

Canonical layout:

```
manifest.json
src/main.f
tests/
README.md
```
