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
- Path-shaped relative imports such as `import("./helper.f")`, including imports
  made later by exported functions, methods, lambdas, callbacks, and re-exported
  child callables, resolve from the source module containing that import. A
  missing sibling is an error and never falls through to a same-named file in
  the consuming project. Package-owned relative imports are canonically confined
  to that package root, including symlink/reparse traversal checks, and delayed
  imports reacquire the package read lock. Bare package names remain
  package-store lookups.
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
