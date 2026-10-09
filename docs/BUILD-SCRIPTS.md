# Build Scripts

These features are part of unreleased v4.10 development.

Per-item scripts run `pre-build` → custom `build` or normal render → `post-build`.
A failed phase stops that item and all its later phases. A post failure fails the
invocation; successful metadata is committed only after post succeeds. Independent
DAG branches continue. Interrupted/partially written builds require `build --repair`.

## Configuration and discovery

`pre-build`, `build`, and `post-build` are optional non-empty project-local `.f`
path strings in `.nift/tracked.json`. `build` replaces normal template/content
rendering and minification. Its script must leave a regular file at the tracked
output path; a missing output fails. It owns the output bytes. Content still exists
and participates in incremental tracking.

For `content/about.html`, canonical sidecars are:

- `content/about.pre-build.f`
- `content/about.build.f`
- `content/about.post-build.f`

The content extension is replaced by the lifecycle suffix, including nested paths.
An explicit field wins over discovery. Deprecated hyphenated pre/post sidecars
remain a fallback. If dotted and hyphenated sidecars both exist without an explicit
choice, loading fails with a diagnostic. Both are never executed automatically.

Existing space-separated keys (`pre build`, `post build` and their mode-specific
variants) remain supported with their existing additive matching. Configuring both
spellings of one generic tracked hook is an error. Legacy hooks retain standalone
process environment behavior and serialize affected builds. Project-level hooks
in config.json retain their existing behavior.

## Item script context

New fields and discovered sidecars use the existing native Parser with tracked
item/project context. They may read/write project-local paths, import scripts and
use the normal runtime. They cannot change process-global cwd/environment through
standalone-only functions. Independent item scripts can therefore execute in parallel.
Each phase has a fresh script scope; communicate across phases through files.
`getenv()` and `env()` provide invocation-local values:

| Variable | Value |
| --- | --- |
| NIFT_HOOK_PHASE | pre-build, build or post-build |
| NIFT_HOOK_MODE | all, updated, auto, repair or names |
| NIFT_HOOK_TARGET | tracked name |
| NIFT_HOOK_CONTENT | project-relative content path |
| NIFT_HOOK_OUTPUT | project-relative output path |
| NIFT_HOOK_TEMPLATE | configured template path, possibly empty |
| NIFT_HOOK_ROOT | absolute project root |

Filesystem paths in these scripts resolve against the project root. The invoking
process cwd is unchanged; external commands can select their cwd explicitly with
`cmd(...).cwd(getenv("NIFT_HOOK_ROOT"))`. Returned text does not become the output.

## Completion dependencies

`depends` is an optional array of exact tracked names. Missing names, self edges,
duplicates and cycles are rejected before a build; cycle diagnostics show the path.

`nift build search-index` includes its prerequisite closure. Requested targets
retain the existing forced targeted-build behavior; clean prerequisites are
validated without rebuilding. Stale prerequisites build first. `build --all`
builds the full graph; incremental and build-auto use the same scheduling path.
Workers execute independent ready items concurrently with O(V+E) planning and no
polling. Projects without depends use the existing atomic-index worker path with
zero dependency-graph allocations.

`depends` orders completion; it does not invalidate a dependent when a prerequisite
changes. Add the prerequisite output as an ordinary file dependency (for example
with `@dep` or `.deps.json`) when its bytes affect your output. Clean dependents
are rechecked after prerequisites execute, so generated file changes can rebuild
them in the same invocation. Renaming a prerequisite updates its references;
removing one is rejected while retained tracked items depend on it. Failed prerequisites
block descendants with an explicit diagnostic and do not commit descendant metadata.

Scripts, content, script-import/file dependencies and structural script selection
are recorded in incremental metadata. Added/removed sidecars, changed scripts,
content, relevant tracked metadata and normal file/config dependencies can rebuild
an item. Discovery is refreshed on each load, including build-auto passes.

## Complete example

Tracked entries:

```json
{"tracked":[
  {"name":"api-index","title":"API","build":"scripts/api.f"},
  {"name":"search-index","title":"Search", "depends":["api-index"],
   "pre-build":"scripts/search-pre.f", "build":"scripts/search.f",
   "post-build":"scripts/search-post.f"}
]}
```

Keep `content/api-index.html` and `content/search-index.html` as source inputs.
The API script writes its tracked output using the same FileValue pattern below.

`scripts/search-pre.f`:

```nift
print("Preparing " + getenv("NIFT_HOOK_TARGET"))
```

`scripts/search.f`:

```nift
input := file("public/api-index.html")
input.open("r")
api := input.read()
input.close()
output := file(getenv("NIFT_HOOK_OUTPUT"))
output.open("w")
output.write("Search data: " + api)
output.save()
output.close()
```

If API output bytes should invalidate search, add `public/api-index.html` to
`content/search-index.deps.json` using the normal user-dependency format.

`scripts/search-post.f`:

```nift
if(!file(getenv("NIFT_HOOK_OUTPUT")).exists()) { throw error("Output missing") }
print("Finished " + getenv("NIFT_HOOK_TARGET"))
```

Run `nift build search-index` to build/validate API first, then run all three
search phases. Pre, build and post errors all propagate to the command status.
