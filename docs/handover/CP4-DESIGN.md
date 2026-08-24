# CP4 design: final `build --repair` reconstruction contract

Design statement (deliverable per review §21), before implementing the
destructive sweep.

## 1. Authoritative vs derived classification (from the actual repository)

AUTHORITATIVE (never written by the repair sweep):
```
.nift/config.json
.nift/tracked.json
.nift/.watch/watched.json          watch directory configuration (watch/unwatch)
content/**                         page source
templates/**                       templates
data/**, schemas/**, contracts/config inputs
user-managed files in public/      ordinary untracked output assets
```
DERIVED / RECONSTRUCTIBLE (eligible for the sweep, rebuilt by repair):
```
public/<name>.<ext>                tracked page primary output
public/<name>-<N>.<ext>            pagination outputs
.nift/public/<dir>/<name>.info.json   per-page metadata
.nift/**/*.hash                    stored dependency hashes (mirror source tree)
.nift/.watch/<dir>/tracked.json    regenerable per-dir watch bookkeeping
```
Generated deployment configs written by `init --target=...`
(.vercel/output/config.json, content/staticwebapp.config.json,
.amplify-hosting/deploy-manifest.json) are init-time artifacts, not build-page
outputs; repair does not sweep them.

No file has genuinely mixed authoritative/derived semantics in the repair
sweep's scope. The one ambiguity is USER files in public/ matching the
pagination namespace (below).

## 2. Ownership / sweep strategy

The output tree is NOT wiped. Repair only removes files it can establish as
Nift's own derived artifacts, from a pure-function ownership model rooted in
authoritative state:

- PRIMARY OUTPUTS: for every currently tracked page, `output_path(name)` is
  owned (pure function of tracked.json + config). Overwritten by the rebuild.
- PAGINATION OUTPUTS: for every currently tracked page that is CURRENTLY
  paginated (the rebuild renders `public/<dir>/<base>-<N>.<ext>` for N in
  2..count), files in that namespace beyond the current count are stale and
  removed - ownership is established because the namespace is demonstrably in
  active Nift use. Files that are themselves a tracked page's primary output
  are exempt. A currently-NOT-paginated tracked page's `<base>-<N>` namespace
  is NOT swept: without readable history there is no evidence those files were
  Nift's rather than user-owned (the normal build's history-based cleanup
  covers the readable-history case; the corrupt/missing-history case is a
  stated limitation, below).
- ORPHAN INFO FILES: every `.nift/public/**/<name>.info.json` is Nift-owned
  derived metadata. If `<name>` is no longer tracked, the info, the primary
  output (exact path from the info's "output" field when readable, else the
  name + config output-ext heuristic), and any pagination pages recorded in a
  readable info are removed.
- STALE HASHES: `.nift/**/*.hash` (excluding .nift/public and .nift/.watch)
  whose mirrored source path no longer exists are removed.
- WATCH BOOKKEEPING: per-dir `.nift/.watch/<dir>/tracked.json` is regenerated
  by reconcile; repair runs reconcile normally (it is not pre-deleted) - see
  watch handling below.

## 3. The ownership boundary and its exact limitation

Repair never removes files in the output tree that do not match a tracked
page's primary or pagination namespace (e.g. public/images/, public/robots.txt,
or any file whose base is not a tracked page name). It never reads "delete
public/*".

LIMITATION (stated precisely):
1. A user file named exactly `<trackedpage>-<N>.<outext>` (N>=2) beyond the
   current count of a CURRENTLY-paginated tracked page is indistinguishable
   from a stale pagination artifact and will be removed by repair. Nift's
   pagination writes into that namespace, so Nift treats the in-use namespace
   as its own; the collision is documented. All other user files are safe.
2. A currently-NOT-paginated tracked page whose previous .info.json is corrupt
   or missing may retain stale pagination files (`<base>-<N>`); repair cannot
   establish ownership without history, so it leaves them rather than risking a
   user file.
3. A page that was previously tracked but whose .info.json was also deleted
   (so no orphan metadata remains) leaves its output indistinguishable from a
   user file; repair cannot remove it.

## 4. Exact semantic difference between `--all` and `--repair`

`build --all`: forces every tracked page through the normal render/write path,
TRUSTING the previous .info.json for pagination lifecycle and ordinary
invariants. It does not sweep orphans, does not treat corrupt .info.json
specially (previous_pagination_pages becomes 0), and a corrupt per-dir watch
state fails the build.

`build --repair`: distrusts ALL derived state. It rebuilds every page, then
performs the ownership-aware sweep above (orphans, pagination surplus, stale
hashes), so the derived tree CONVERGES to the state implied by authoritative
inputs. Corrupt/missing/torn .info.json is treated like missing (regenerated);
stale pagination beyond the current count is removed even when the old
metadata is corrupt (for currently-paginated pages). Repair may be slower than
a normal build; reconstruction correctness is the goal.

Convergence/idempotence: after a successful repair, the derived tree is
canonical; a second `build --repair` produces the same tree (the sweep finds
nothing stale, the rebuild rewrites identical bytes).

## 5. Watch reconciliation under repair

Repair runs reconcile_watch() normally under ownership. A CORRUPT per-dir
watch state makes reconcile fail, so repair FAILS CLOSED (non-zero, marker
retained, no guessing from corrupt watch bookkeeping). Removing the corrupt
regenerable per-dir state (user action) and repairing again succeeds. This is
consistent with the repair-failure semantics and avoids the disappeared-page
detection gap that pre-deleting valid watch state would create.

## 6. Repair lifecycle (unchanged from CP2/CP3)

acquire (refuse live; allow stale) -> mutation_started=false -> reconcile ->
full rebuild (output -> stale pagination cleanup -> .info.json LAST) -> sweep
(repair only, after successful rebuild) -> finish on success; retain marker on
any repair failure. Works with or without a pre-existing .unfinished.
