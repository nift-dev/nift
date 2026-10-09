# Nift v4.10.0

Nift v4.10 is a build-workflow and performance release following v4.9.0. It adds
tracked-item build pipelines, makes rewrite/redesign additive inside existing Nift
projects, and retains a bounded set of runtime performance improvements. The C ABI
remains **1.3**.

## Tracked-item build pipelines

Tracked items can now declare a per-item lifecycle in `.nift/tracked.json` using
the optional `pre-build`, `build` and `post-build` script fields, plus a
completion-order `depends` array.

- Per-item scripts run `pre-build` → custom `build` or normal render → `post-build`.
  A failed phase stops that item and its later phases; a post-build failure fails
  the invocation. Successful metadata is committed only after post-build succeeds.
  Independent dependency branches keep running.
- A custom `build` script replaces normal template/content rendering and
  minification. It owns the output bytes and must leave a regular file at the
  tracked output path; a missing output fails. The content file still participates
  in incremental tracking.
- Canonical sidecars are discovered next to the content file by replacing the
  content extension with the lifecycle suffix, for example
  `content/about.pre-build.f`, `content/about.build.f` and
  `content/about.post-build.f`, including nested paths. An explicit field wins
  over discovery. Deprecated hyphenated pre/post sidecars remain a fallback; if
  dotted and hyphenated sidecars both exist without an explicit choice, loading
  fails with a diagnostic rather than executing either. Existing space-separated
  keys and project-level `config.json` hooks keep their existing behavior.
- Each phase runs in a fresh native script scope with invocation-local
  environment values: `NIFT_HOOK_PHASE`, `NIFT_HOOK_MODE`, `NIFT_HOOK_TARGET`,
  `NIFT_HOOK_CONTENT`, `NIFT_HOOK_OUTPUT`, `NIFT_HOOK_TEMPLATE` and
  `NIFT_HOOK_ROOT`. Filesystem paths resolve against the project root; the
  invoking process working directory and environment are unchanged.
- `depends` is an optional array of exact tracked names. Missing names, self
  edges, duplicates and cycles are rejected before a build, and cycle diagnostics
  show the path. `nift build <name>` includes the prerequisite closure: stale
  prerequisites build first and clean prerequisites are validated without
  rebuilding. `build --all` builds the full graph. `depends` orders completion
  and does not by itself invalidate a dependent when a prerequisite changes; add
  the prerequisite output as an ordinary file dependency when its bytes affect
  your output. Renaming a prerequisite updates references, removing one is
  rejected while retained items depend on it, and a failed prerequisite blocks
  its descendants with an explicit diagnostic.
- Independent ready items execute concurrently with O(V+E) planning and no
  polling. Projects without `depends` use the existing atomic-index worker path
  with no dependency-graph allocations. Interrupted or partially written builds
  are repaired with `build --repair`.

The reference is `docs/BUILD-SCRIPTS.md`; the website documents the workflow at
Build Scripts, General build systems and Asset pipelines.

## Transformation workspaces: additive existing-project init

`nift init --rewrite` and `nift init --redesign` now add missing transformation
workspace files inside an existing Nift project. Existing configuration, tracking
state, workbooks, guidance, README, AGENTS, HANDOVER and every other existing file
are preserved byte-for-byte, including reruns and when another transformation mode
already exists. Existing-project scaffolding always keeps existing files even when
append/replace policy flags are supplied; fresh-project initialization keeps its
existing policy rules. Rewrite and redesign remain experimental.

## Runtime performance

This release retains a bounded set of general runtime improvements; gains are
workload-dependent and are not portable speed guarantees.

- Callback and capture allocation reductions: shared exact lambda captures and
  overlay callback frames, removal of discarded default capture slots, and
  allocation-free native call-name probes.
- Object-member traversal reduction: hot runtime paths avoid redundant object
  member traversals.
- Recursive glob and traversal: recursive glob scans and glob directory prefixes
  are shared, glob sorting and relative-path overhead are reduced, and string
  replacement builds its result linearly.
- Prepared filesystem operations: canonical stat/copy/move recipes are shared
  across repeated evaluations.
- Large strings: bounded canonical large-string expressions are prepared instead
  of rebuilt.
- FileValue and save ownership: FileValue operations are prepared and clean saved
  buffers are shared, reducing ownership churn.

These changes were characterized by local paired profiling and retained scaling,
allocation and lifetime guards. They are **not** new official cross-language
benchmark results. The frozen official series remains unchanged and the official
post-v4.9 benchmark campaign stays deferred.

## Portability and correctness

- Windows: native aliases are resolved for build cache invalidation so alias and
  symlink targets invalidate correctly.
- macOS/portable C++17: the shared-ownership check no longer relies on the removed
  `std::shared_ptr::unique()` and uses a portable C++17-equivalent test.

## Documentation

New and updated documentation covers Build Scripts, General build systems, Asset
pipelines and reconciled website wording (release-version, transformation-policy
and benchmark framing). No command or language syntax is removed or deprecated by
this delta, and no new major language feature is introduced. Public C headers and
C ABI **1.3** are unchanged from v4.9.0.

## Remaining costs and deferred work

Sort factory/capture/frame/instance/retention architecture, persistent member
indexing, RuntimeValue layout changes, canonical Jsonic++ work and VM/JIT remain
deferred. Multiple declared outputs per item are not implemented. A comparative
Nift/Make/Ninja benchmark was not run and no Make/Ninja performance or feature
parity is claimed. None of these is a release blocker.

## Certification and release boundary

The retained runtime passes native, maintained and aggregate binding walls,
GCC/Clang first-party and native-wrapper warning gates, sanitizers, exact-candidate
independent NRS 94/94 and pinned PRS 12/12, deterministic performance guards, and
Linux/macOS/Windows hosted certification. Release preparation repeats the
authoritative local matrix and exact-SHA hosted readiness checks before the
non-publishing artifact rehearsal.

The canonical rehearsal stages four public archives: Linux x86_64, macOS ARM64,
macOS x86_64 and Windows x86_64, plus a SHA256SUMS manifest. Installer preflight,
live-installer byte equality, extracted-archive smokes and exact-set inspection
remain release gates. These reviewed notes do not authorize a tag, publication or
Chocolatey submission; explicit final release authorization is required after
rehearsal and artifact inspection.
