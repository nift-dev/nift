# Incremental build state (Runtime 2.0)

Per-page state under `.nift/`:

| State | Path | Contract |
| --- | --- | --- |
| Page build metadata (page-info) | `.nift/<output>.info.json` | **Reproducible**: identical inputs give identical bytes (a clean rebuild reproduces it exactly; BH6). Holds dependencies, requirements, observed dynamic reads (CP02), snapshots and the sidecar certificate. |
| Modified-mode build time | page-info mtime | v4.12 rule: a dependency whose mtime is `>=` page-info's mtime rebuilds the page. Page-info is stamped from the filesystem clock (`utimensat(..., NULL)`), so immediate and atomic saves are caught. No timing records, hashes or render locks are written. |

Equal timestamps rebuild conservatively. On coarse filesystems a
back-to-back build may rebuild until the next timestamp tick; v4.12 has the
same behaviour. Modified mode does not promise to catch edits made during a
running build, metadata-preserving replacements, multiple writes in one tick,
backdated mtimes, or backwards clock steps. Hash mode remains the opt-in for
content-change detection.

## Failed-page list (D-020, feature-build-recovery)

`.nift/failed.json` = `{"failed": ["<tracked name>", ...]}`, present only
while some page's last build failed (or was blocked by a failed
prerequisite). A failed page keeps its page-info and (with the exceptions in
`checkpoints/feature-build-recovery.md`) its last-good output; the list adds
the status reason "previous build of this page failed", so the page is rebuilt
whatever selected it. It holds names only — no state recovery depends on — so
a damaged list marks every page dirty (bounded full rebuild) and a deleted one
loses only the hint. A page leaves the list when it builds successfully;
untracked names are pruned on every build and by `untrack`/`rm`/`mv`. Written
atomically after the pages of a build settle, merged with the list on disk.
