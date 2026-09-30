# Nift package contract v1

A package is a Git/local repository containing `manifest.json`, Nift `.f` sources, tests and documentation. A package manifest requires a portable lowercase `name`, a semantic `version`, and a contained relative `.f` `entry` made from lowercase portable path components; `description` and `dependencies` are optional. A consumer project may instead contain only `dependencies`. Recognized fields are strict and malformed metadata is rejected rather than replaced.

Direct dependencies use an explicit source and requested revision:

```json
{"dependencies":{"sqlite":{"source":"sqlite","ref":"latest"}}}
```

`.nift/packages.lock.json` records the same `source`, the original `requested` revision, and either a full exact Git commit or `local`. Existing locks must be complete and consistent with the manifest. Graph-shaped transitive locks are intentionally deferred to the dependency-graph checkpoint.

Package code uses isolated `import`/explicit `export` semantics. Script land retains `@import` as a compatibility spelling; template top level continues to use the `@import` directive.

Resolution contract:
- `import("./local.f")` and other path-shaped imports are local files.
- `import("sqlite")` is a package import resolved from the installed package root.
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

Packages that offer process, FFI or future native implementations keep backend
selection inside their exported package facade rather than encoding variants in
the package name or manifest. The canonical terminology, selection, resource,
error and streaming rules are maintained in
[`docs/handover/BACKEND-PACKAGES.md`](../handover/BACKEND-PACKAGES.md).
