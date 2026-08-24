# CP4 report: final `build --repair` reconstruction contract

## Final repair contract

> `nift build --repair` distrusts regenerable build-derived state and
> deterministically reconstructs a known-good derived tree from authoritative
> project inputs, removing only files Nift can establish as its own derived
> artifacts (never wiping the output tree, never touching user-managed files).

## Authoritative / derived classification (audited from the repository)

AUTHORITATIVE (never swept): .nift/config.json, .nift/tracked.json,
.nift/.watch/watched.json (watch configuration), content/, templates/,
data/schemas/contract inputs, and user-managed files in public/.

DERIVED / RECONSTRUCTIBLE: tracked page primary outputs, pagination outputs,
.nift/public/**/*.info.json, .nift/**/*.hash (stored dependency hashes
mirroring the source tree), regenerable per-dir watch bookkeeping.
init --target deployment configs are init-time artifacts, not page outputs;
not swept.

## Repair sweep implementation (ProjectInfo::repair_derived_state)

Called after a successful repair rebuild, before the marker is removed:
1. Pagination surplus sweep: for every currently-paginated tracked page (the
   rebuild's fresh .info.json gives the current count), files in its
   `public/<dir>/<base>-<N>.<ext>` namespace with N beyond the count are
   removed (ownership established - the namespace is in active Nift use).
2. Orphan .info.json sweep: every .nift/public/**/<name>.info.json whose page
   is no longer tracked is removed together with its primary output (exact
   path from a readable info's "output" field, else name + config output-ext
   heuristic) and any pagination pages a readable info records.
3. Stale stored-hash sweep: .nift/**/*.hash (excluding .nift/public,
   .nift/.watch, config/tracked) whose mirrored source path no longer exists
   is removed.

Current-page info files are identified by comparing info PATHS (the info
filename derives from the output path - e.g. the "/" page's info is
index.info.json), not filename-derived names.

## Ownership boundary protecting user-managed public files

The sweep never reads "delete public/*". Files not matching a tracked page's
primary output or an actively-paginated pagination namespace are left alone
(verified: the campaign's public/keepme.txt survives every repair case).

## Corrupt / missing / torn-state handling

Corrupt or missing .info.json is treated like missing (regenerated); a torn
generated output is regenerated; stale pagination surplus beyond the current
count is swept even when the old metadata is corrupt (for currently-paginated
pages). Stated limitations (CP4-DESIGN.md): (1) a user file named exactly
`<trackedpage>-<N>.<ext>` beyond a currently-paginated page's count is
indistinguishable from a stale pagination artifact and removed; (2) a
currently-NOT-paginated page with corrupt/missing history may retain stale
`<base>-<N>` files (ownership not establishable); (3) a removed page whose
.info.json was also deleted leaves its output indistinguishable from a user
file.

## Evidence (tests/repair_campaign.py, 109 checks, all pass)

- Corruption cases (each: repair succeeds, tree converges, marker cleared,
  second repair idempotent, ordinary build clean, user file preserved):
  delete output, truncate output, garbage output, valid-but-wrong output,
  altered permissions, partial pagination set, stale pagination surplus,
  corrupt .info.json, removed .info.json, orphan removed page, mixed
  corruption across several pages.
- Pagination 3->2 shrink with CORRUPT old info: stale blog-3 swept.
- Repair failure (broken template): non-zero, marker retained; fix -> repair
  succeeds, marker cleared, tree converges.
- Interrupted repair (killed mid-epoch via the hold hook): marker retained,
  ordinary build refuses, second repair converges.
- Concurrency: build refuses while repair owns; second repair refuses while
  repair owns; repair refuses while build owns; no two epochs simultaneous.

## Watch recovery

Repair runs reconcile_watch() normally under ownership. A corrupt per-dir
watch state makes reconcile fail, so repair FAILS CLOSED (non-zero, marker
retained, no guessing from corrupt watch bookkeeping); removing the corrupt
regenerable per-dir state and repairing again succeeds. This avoids the
disappeared-page detection gap that pre-deleting valid watch state would
create.

## Full regression

47 targets / 308 PASS lines / exit 0 (conformance 9/9, ownership 38,
zero-mutation 16, repair campaign 109, pagination ordering, crash recovery).

## Ordinary-build performance sanity

10k unchanged: ~105.2 ms (+24.8% vs pre-Embed), byte-identical - unchanged
from CP3. The repair sweep is a cold branch on the repair-only path.

## Commit / hygiene

(committed by the CP4 checkpoint). Clean tree: no binaries, objects, or
extra worktrees. Rust build --repair parity remains the recorded post-CP4
requirement (implement only after this C++ contract is frozen).

## CP4.1 correction (reviewer HOLD): trust boundary + sweep error propagation

### Final public-file ownership rule

Repair deletes a public/output-tree file ONLY where ownership is established
independently of derived metadata:
- current tracked page primary outputs (authoritative tracked.json + config),
- current pagination outputs (the successful current render), and
- the currently-paginated `<base>-<N>` namespace beyond the current count
  (documented namespace-collision rule).
Derived metadata is never used to authorize deletion.

### Orphan-output limitation

An orphan `.info.json` is unquestionably Nift-owned (its location proves it) and
is deleted. Its historical public output is PRESERVED: the output path is only
knowable from the derived metadata, which `--repair` must distrust (a
corrupt-but-valid info could name any path; a custom historical output
extension makes a default-ext guess unsafe). The former `info["output"]`
deletion and the `name + config.output_ext` fallback are REMOVED.

### Malicious/corrupt metadata cases (hostile tests)

- orphan info `"output": "public/keepme.txt"` + user keepme.txt -> preserved
- orphan info `"output": "../outside.txt"` -> nothing outside the project touched
- orphan info `"output": "/absolute/path"` -> cannot delete
- corrupt orphan info + user file at the guessed default path -> preserved
- orphan page historically custom ext (.php) + user file at default ext
  (.html) -> default-ext user file preserved
All: repair succeeds, orphan .info.json removed, user file preserved.

### Sweep error semantics

`repair_derived_state()` now returns bool; REQUIRED operations propagate
failure and repair retains .unfinished and returns non-zero:
- pagination surplus removal (remove_owned_file failure),
- orphan .info.json removal (fs::remove error),
- incomplete output-tree or .nift/public traversal (no silent break+certify).

BEST-EFFORT: stale stored-hash cleanup is regenerable cache hygiene; hash
failures do not fail repair (hashes are invalidated independently by
stored_hash_changed). Documented.

### Hash-sweep result (verified)

The previous mirrored-path check kept the `.hash` suffix, so every stored hash
was classified stale. Fixed: `mirrored.replace_extension()` strips the suffix
before the existence test. Hash-mode campaign case confirms an orphaned hash
(data source deleted + @dep removed) is removed while valid hashes are kept.

### Sweep-failure test

Read-only `.nift/public/<dir>/` blocks the required orphan-info removal:
repair returns non-zero, marker retained, ordinary build refuses; restore the
directory mode -> second repair succeeds and clears the marker. (Permission-
based; skipped when running as root.)

### Repair campaign count/results

All repair_campaign.py checks pass (hostile-metadata, sweep-failure, hash-mode,
the 11 corruption cases with the orphan case reframed to the conservative
preserve rule, pagination shrink with corrupt info, failure, interrupted
repair, concurrency).

### Full regression

47 targets / 341 PASS lines / exit 0.
