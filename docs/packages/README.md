# Nift package contract v1

A package is a Git/local repository containing `manifest.json`, Nift `.f` sources, tests and documentation. A package manifest requires a portable lowercase `name`, a semantic `version`, and a contained relative `.f` `entry` made from lowercase portable path components; `description` and `dependencies` are optional. A consumer project may instead contain only `dependencies`. Recognized fields are strict and malformed metadata is rejected rather than replaced.
Direct dependencies use an explicit source and requested revision:

```json
{"dependencies":{"sqlite":{"source":"sqlite","ref":"latest"}}}
```

`.nift/packages.lock.json` records the **complete resolved dependency graph**:
every installed package (direct and transitive) as one node with its
canonical/resolved source identity and exact Git commit (or `local`), plus
each node's outgoing requirement edges (the source spelling and requested ref
each parent declared). The root manifest declares only direct dependencies;
transitive requirements come from package manifests and are resolved and
installed deterministically.

```json
{
  "lockfileVersion": 2,
  "packages": {
    "a": {
      "source": "https://github.com/org/a.git",
      "commit": "<40-hex>",
      "requirements": {
        "b": {"source": "github:org/b", "requested": "latest"}
      }
    },
    "b": {
      "source": "https://github.com/org/b.git",
      "commit": "<40-hex>",
      "requirements": {}
    }
  }
}
```

The lock is the exact, reproducible closure: one node per package name;
conflicting requirements for the same name (different canonical source or
different resolved commit) are deterministic failures; import cycles and
self-dependencies are deterministic failures. Git dependencies are
content-pinned by exact commit. Local dependencies (`commit == "local"`) are
graph/path-deterministic with live contents (not content-reproducible across
machines).

`nift packages [name] [--json]` explains why a package is installed, listing
every root-to-package dependency path and what each parent requested, derived
from the root manifest and the v2 lock alone (no installed-package reads).
`nift install` accepts a valid v1 direct-only lock and migrates it to v2 while
preserving the locked direct commits; read-only operations never rewrite a v1
lock.

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
  to that package root, including symlink/reparse traversal checks. Package
  modules retain frozen lock provenance (`source`, `requested`, exact commit or
  `local`, package name/root, and project). Delayed imports acquire a short
  package read lock, compare current installed/lock identity with that frozen
  provenance, read the child, and release the lock before executing it. Changed
  remote ownership is a controlled stale-package error rather than an import
  from a replacement generation. Unchanged local provenance intentionally keeps
  following the live local source. Bare package names remain package-store
  lookups.
- dependencies are declared by package name in `manifest.json`; acquisition/version locking is CP31+ and is intentionally separate from this module contract.

Resource path contract:

- `module_path()` returns the absolute normalized directory of the currently
  executing file-backed script or module. `module_path("assets/name")` returns
  an absolute normalized path relative to that directory.
- `package_path()` returns the absolute normalized root of the package that owns
  the current module. `package_path("assets/name")` resolves from that root.
  Calling it from code with no package owner is an error.
- Both functions accept zero or one argument. The optional argument must be a
  non-empty string. Empty strings are rejected deliberately rather than acting
  as a second spelling of the zero-argument form. Absolute paths, rooted paths,
  Windows drive-qualified paths, UNC paths, and escapes outside required
  containment are errors.
- Package-owned results are canonically confined to the package root, including
  existing symlink/reparse-point prefixes. The final path does not need to
  exist, so a missing target under a contained parent is valid. Package-owned
  `module_path(relative)` has the same package confinement as `package_path`.
- Package module environments and worker snapshots share immutable package lock
  provenance, not an open lock. Initial imports, delayed relative imports, and
  each resource resolution take a short package read lock and validate the
  current lock entry plus installed package identity. The lock is released
  before imported code runs or a path string is returned, so synchronous child
  `nift add/install/update/remove` commands cannot deadlock behind their parent.
  Failed imports still roll back their uncommitted module graph. A returned path
  is only a string: replacement may occur after validation and before a later
  filesystem operation, so callers that need atomic I/O must perform that I/O
  through an appropriately coordinated operation rather than treating the path
  string as a lasting lease.
- Ownership follows the module environment described above: exported functions,
  methods, lambdas, callback values, nested calls, aliases/re-exports, and
  thread/future worker snapshots continue to resolve against their defining
  module after they escape into consumer code or the process CWD changes.
- `module_path` has no CWD fallback. Inline `-e`/eval programs, the REPL, stdin,
  and in-memory embedding sources without a backing file receive a controlled
  error. Ordinary standalone local modules retain their existing filesystem
  authority; project/embed hosts and `--fs-root` continue to enforce their
  existing containment policies. Configured filesystem authority is
  canonicalized once when the parser/invocation is constructed and copied
  exactly to workers, so a relative `--fs-root=.` remains anchored to the
  original working directory after `cd()`.
- These functions only produce path strings. They do not read, create, or
  require the target to exist, and they do not change `open`, `file`,
  `ifstream`, `exists`, `copy`, `remove`, or any other filesystem API.

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
