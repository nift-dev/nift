# feature-build-recovery — Loud errors, automatic safe recovery (D-020)

Status: implemented on branch `feature-build-recovery` (from `runtime-2`
@`9391a35`); same-model self-review done; independent adversarial review
pending; not certified (`make certify` is run by the owner).

Contract (D-020): errors are loud, recovery is automatic whenever it is safe,
success means success. An ordinary build failure ends with a non-zero status,
keeps valid build state and last-good outputs, and the next build works once
the problem is fixed, never requiring `--repair`. `.nift/.unfinished` and
`--repair` are reserved for interrupted or uncertain state, and even then
recovery is automated where correctness can be established.

## Phase 1 — investigation (runtime-2 @9391a35)

### How a build mutates state

`build_all` / `build_names` (src/ProjectInfo.cpp):

1. `ProjectOwnership::acquire()` takes the OS lock on `.nift/.unfinished` and
   creates the marker durably (fsync). `Clean` = no marker before; `Stale` =
   marker present, no live owner (crash evidence); `Live` = another process
   owns the lock (refuse); `Failed` = I/O (refuse).
2. `reconcile_watch()` — adds discovered watched pages; for pages whose
   watched content disappeared it **removes** output, page-info, clock record
   and stored hashes, then saves `.nift/.watch/**/tracked.json` and
   `.nift/tracked.json` (both atomic). Idempotent: a retry after a failure in
   the middle converges.
3. Project `pre` hooks (user `.f` scripts; may write anything).
4. Affected-set calculation (`build_reasons`, read-only).
5. `build_many` → per page `build_one`:
   a. legacy per-file `pre build` hooks and the `pre-build` item script (user
      code; may write files),
   b. render (or custom `build` script, which writes the output itself),
      minify — all in memory,
   c. **output write: `write_direct_file(s)` — in-place truncate+write, not
      atomic** (CP3 performance decision; pagination sets are written page by
      page),
   d. `post-build` item script and legacy `post build` hooks (user code,
      usually post-processing the output),
   e. hash-mode snapshot consistency checks ("dependency changed during
      build; retry build"),
   f. removal of surplus pagination outputs (pages `new+1..previous`),
   g. page-info: `write_readonly_file` — **atomic temp+rename** — followed by
      the CP01 modified-mode clock record `.nift/clock/<info path>` (plain
      write).
   After all pages: identity catalogue publication (own crash-safe protocol).
6. Project `post` hooks (only when every page succeeded).
7. `finish_if_epoch_complete`: success → (repair: required sweeps) → remove
   marker. Failure → marker removed **only** when `mutation_started_` is still
   false.

`mutation_started_` is one process-wide flag set by any page that ran a
script or wrote output, and by reconcile removals. Staleness of a page is
decided only from its own page-info (dependencies vs page-info mtime in
`modified`, recorded hashes in `hash`, both in `hybrid`), its clock record,
and whether its outputs exist.

### What can be inconsistent

**(a) Ordinary error in one page while others succeed.** Pages that succeed
commit output then page-info, so they are consistent. A page failing in
steps a–b (syntax/template/expression error, missing content or `@input`,
invalid sidecar, minifier error, failing pre-build script) has written
nothing Nift-owned: its last-good output and old page-info are intact and
consistent with each other. A page failing in steps d–g has a **new output
with old page-info**. It is rebuilt next time only if the reason it was
selected still holds; it is wrongly trusted when the selection reason was
`--all`, a missing output, or anything that the new output satisfies (e.g.
the post-build script that failed never ran to completion but the output now
exists). Because `mutation_started_` is global, in practice **any** failure
in a build where another page wrote output retained the marker, and the next
`nift build` refused with "unfinished build detected … run nift build
--repair" — the lock-out D-020 removes. Measured before the change:
one page with `@input("missing.txt")` among 3 → exit 1, marker kept, next
`nift build` after the fix exits 1 ("unfinished build detected").

**(b) Error in a hook.** Project `pre` hook failure happens before any page;
the marker was retained anyway if reconcile had removed a disappeared watched
page (reconcile's mutations are complete when it returns true). Project
`post` hook failure happens after every page committed: state is fully
consistent, but the marker was retained (lock-out). Per-page pre/post
scripts: see (a).

**(c) SIGKILL** at each step: 1 nothing changed (marker possibly just
created); 2 some disappeared pages' files removed, tracking not yet saved
(idempotent on retry); 3 arbitrary user writes; 5c a **truncated output**
(last-good destroyed — the CP3 in-place write); 5d output written, page-info
old; 5f pagination surplus partially removed while page-info still names the
old count; 5g page-info new but clock record missing/torn (CP01 already
rebuilds on a missing/invalid record); 6 post hook partially run; 7 all done
but marker not yet removed. Every case leaves `.unfinished` without a lock
holder. Before this change the next build refused until `--repair`, which
does a forced full rebuild plus the required sweeps (pagination surplus of
currently paginated pages, orphan page-info).

## Design (as implemented)

1. **Per-page failure transaction** (`build_one` → `build_one_attempt` +
   `settle_failed_page`, src/ProjectInfo.cpp). A failed page is left
   consistent *and* dirty:
   - **Last-good output.** Nothing is written before the in-memory render
     succeeds (unchanged), so render/template/expression/dependency errors
     never touch outputs. Pages that run user scripts (item scripts, legacy
     per-file hooks, custom `build`) can touch outputs before failing; for
     them only, the existing outputs (main + pagination pages) are copied to
     PID-tagged `.nift-tmp-*` siblings before the first script runs
     (`filesystem::backup_file`) and put back on failure — atomically
     renamed for a regular file, written back *through* the link for a
     symlinked output (the build's own write-through semantics, so the link
     and its target survive). Pages without scripts pay nothing. Pagination
     pages beyond the last-good set that a failed attempt created are removed
     at once. Exceptions (the page still stays dirty and is rebuilt next time):
     a page *without* build scripts whose inline template `@script` writes its
     own output and then fails (backing up every page's output is not cheap);
     a page without scripts that fails *after* its output was written
     (hash-mode "dependency changed during build", I/O errors); an output that
     had no previous version (left as the attempt wrote it). Files a failing
     script writes outside its tracked outputs are never restored — scripts
     own their side effects (D-002).
   - **Last-good page-info.** Page-info is not touched by a failure, so it
     still describes the last-good output (the regression-suite contract
     `persistence_concurrency_failure_smoke` checks this byte for byte).
   - **Dirty.** The page joins the **failed-page list** `.nift/failed.json`
     (`{"failed": ["<name>", ...]}`), written atomically once after the
     pages of a build settle, merged with the list on disk (so a long-lived
     `ProjectInfo` such as the automation API's never overwrites another
     process's entries) and re-read at the start of every epoch.
     `build_reasons` adds "previous build of this page failed" for listed
     pages, so they rebuild whatever selected them (`--all`, a missing output,
     a post-build script that never completed). The list holds names only; it
     carries no state recovery depends on, so a damaged list (anything but an
     array of strings) is handled conservatively — every page is dirty, a full
     rebuild converges — and a deleted one only loses the "rebuild me" hint.
     A page leaves the list when it builds; untracked names are pruned on
     every build and by `untrack`/`rm`/`mv`; the file is absent when empty
     (one stat per command).
   - Pages blocked by a failed prerequisite join the list too (not
     validation-only jobs, which were clean).
2. **Marker = uncertainty, not failure.** `unresolved_mutation_` replaces
   `mutation_started_` in the exit decision (`finish_if_epoch_complete`). It
   is set only when a mutation could not be brought back to a consistent
   state: a Nift-owned write failed (output/pagination write, stale-page
   removal, page-info write — I/O, not ordinary errors), an output restore or
   the failed-list write failed, or `reconcile_watch` failed after removing
   files. Every other controlled exit — success, page failures, project
   pre/post hook failures, an unresolvable target — removes the marker. A
   repair (explicit or automatic) additionally requires that `build_many`
   settled every page and that the required sweep converged; pages that fail
   during it are listed as failed, so an ordinary error during recovery does
   not keep the project in recovery.
3. **Automatic recovery of interrupted state.** A `Stale` marker is evidence
   that an epoch died mid-mutation. `build`, `build --all`, `build <names>`
   and `build --auto` announce it on stderr ("warning: unfinished build
   detected … Recovering automatically … (the same as nift build --repair)")
   and run exactly the repair epoch (forced full rebuild, hook mode `repair`,
   required sweeps) under the ownership they already hold
   (`build_all_owned`). Correctness is the repair contract already tested by
   `repair_campaign`, `crash_recovery_adversarial` and BH6/BH7, re-proven here
   at every named kill point. Targeted builds recover the whole project (the
   names are subsumed). `untrack`/`rm`/`cp`/`mv` (and the automation API)
   still refuse on a stale marker — they do not build — and now say to run
   `nift build`. A live owner is still refused.
4. **Test seam.** `NIFT_TEST_CRASH_AT=<point>[:<page>]` SIGKILLs the process
   (`_Exit(137)` on Windows) at: `acquired`, `reconciled`, `pre-hooks`,
   `page-start`, `page-rendered`, `page-output-partial` (half the bytes
   written in place, then killed), `page-output`, `page-post`, `page-info`
   (page-info written, clock record not), `page-done`, `page-failed`,
   `failed-list`, `pages-done`, `post-hooks`, `repair-sweep`. One `getenv` per
   process; same pattern as the existing `NIFT_TEST_*` seams.

Unchanged: in-place output writes (no per-page temp+rename cost for pages
without scripts); atomic page-info; CP01 clock records and CP02 observations
(written only by successful page builds, so a failed page keeps its own);
live-lock refusal; `build --auto`'s loop (owned by feature-site-dx: on this
branch `--auto` still exits after the first failing pass; keep-watching
arrives with feature-site-dx).

## Behaviour before / after

| Situation | Before (@9391a35) | After |
| --- | --- | --- |
| One page fails (e.g. `@input("missing.txt")`), others build | exit 1, `.unfinished` kept; next `nift build` (even after the fix) exits 1 "unfinished build detected … run nift build --repair" | exit 1, loud error, no marker; failed page keeps output + page-info; `status`: "previous build of this page failed"; next build after the fix rebuilds it, exit 0 |
| Failing post-build / legacy post hook / custom build script | output already replaced, marker kept (lock-out) | last-good output restored, no marker, page dirty |
| Failing project `post build` hook | marker kept (lock-out) | no marker; pages committed; hook re-runs next build |
| Failing project `pre build` hook after reconcile removed a watched page | marker kept | no marker (reconcile had completed) |
| Prerequisite fails | dependents blocked; marker kept | dependents blocked and listed; both rebuild after the fix |
| `--repair` whose page has an ordinary error | marker kept → every build refused | pages settled, sweep run, marker cleared, failed page listed |
| SIGKILL / crash mid-build | marker; next build refused until `--repair` | marker; next build recovers automatically (loud), byte-identical to a clean build |
| Output/page-info write fails (I/O) | marker kept | marker kept, message says the next build recovers automatically; it does once the cause is gone |
| Two builds at once | second refused | unchanged |

## Tests

`make test-build-recovery` (`tests/build_recovery.py`, in `make test` and in
the certification guard's REQUIRED list): for each of `modified`, `hash`,
`hybrid`, on a project with plain pages, a 3-page paginated page, a
post-build item script, a legacy per-file post hook, a custom build and a
`depends` chain:

- partial failures (missing dependency + expression error): non-zero, loud,
  no marker, succeeded page committed, failed pages keep output **and
  page-info** byte for byte, failed list exact, status reasons, repeated
  unfixed build, fix → plain build, then a no-op build, then **byte-identical
  outputs + page-info vs a from-scratch build** (the incremental-equivalence
  check used after every scenario, plus "no failed list, no backup files");
- failure under `--all` and targeted; prerequisite failure blocking a
  dependent;
- post-build item script, legacy post hook and custom build failing after
  writing: last-good output restored, page dirty, rebuilt after the fix;
- pagination failure keeps all pages; fix with fewer items removes the stale
  page;
- project `pre build` (no output changes) and `post build` hook failures;
- SIGKILL at every named point (5 epoch points × 1, 7 page points × 4 pages
  — plain, paginated, scripted, custom), targeted and `--all` kills, kill
  while settling a failed page, kill at the failed-list write inside a
  recovery, recovery while a page still fails, kills during the automatic
  recovery itself (`page-output-partial`, `repair-sweep`): marker survives,
  next plain build recovers automatically, no-op afterwards, byte-identical
  to a clean build;
- stale marker + targeted build; untrack refuses on a stale marker and
  points at `nift build`; explicit `--repair` still works;
- uncertain state: an output path obstructed by a directory keeps the marker
  with the message, and the next build recovers once it is removed; damaged
  failed list → every page dirty, then rewritten/removed;
- concurrency: a build holding ownership (`NIFT_TEST_OWNERSHIP_HOLD`) makes
  a second `build` and `build --repair` refuse ("another build appears to be
  running") without touching its marker; four racing `build --all` each
  succeed or refuse cleanly, ending consistent and byte-identical.

Total: 900+ checks, ~45 s. Against the pre-change binary the same script
fails 91 of 119 checks in `modified` mode alone; with the last-good restore
disabled (mutation) the script fails the scripted-page checks.

## Results (Linux, 2 shared vCPUs, run as root unless noted)

Pass: `test-build-recovery`, `test-crash-recovery`, `test-runtime2-clock-lifecycle`,
`test-runtime2-modified-race`, `test-runtime2-dynamic-reads`,
`test-runtime2-fs-freedom`, `test-contracts`, `test-conformance`,
`test-runtime2-rt-diff`, `test-ownership-concurrency`, `test-zero-mutation`,
`test-repair-campaign`, `test-ownership-unit`, `test-v44-hooks`,
`test-v44-automation`, `test-pagination`, `test-pagination-equivalence`,
`test-incremental-state-transitions`, `test-commands`, `test-path-safety`,
`test-v410-build-pipeline`, `test-consumer-snapshots`, `test-control-flow`,
`test-content`, `test-json-schema-integration`, `test-metadata-safety`,
`test-init-lock`, `test-complexity-invariants`, `test-filesystem-boundary`,
`test-runtime2-certification-guard`.
`checkpoint-8-filesystem-transaction` and `repair_campaign`'s sweep-failure
case pass as a non-root user (17/17); as root, checkpoint-8's
unreadable-content case fails identically on the pre-change binary (root
reads mode-000 files). Regression suite `runtime-2` @523d1df: 93/94 as root;
`unreadable_source_smoke` is the same root-only pre-existing failure and
passes as non-root; with the prepared module, 94/95 (the new module passes).
`test-progress-pty` fails identically on the pre-change binary in this
environment ("progress residue"), unrelated. Not run: `make certify`, the
identity certification (needs baseline/probe binaries).

Cost (sanity check, not a benchmark: host load average ~6 on 2 vCPUs).
10k-page project, `modified` mode, interleaved runs of the pre-change and new
binaries: no-op build 0.46–0.65 s user (before) vs 0.48–0.60 s (after); full
`--all` 1.86–1.94 s user / 1.57–1.78 s sys (before) vs 1.92–2.13 s / 1.73–1.87 s
(after). Wall time was dominated by load. The added per-page work is a lookup
in an empty map, plus output backups only for pages that run scripts. The
owner's benchmark campaign should confirm.

## Existing tests changed (only as D-020 requires)

These asserted the old lock-out and now assert the D-020 behaviour:

- `tests/ownership_concurrency.py` cases 3, 4, 6 (failed build left marker;
  stale marker refused `build`/`--all`/targeted; post-SIGKILL refusal).
- `tests/zero_mutation_smoke.py` cases 3, 4, 5, 6, 11, 14, 16 (+ docstring):
  ordinary failures and completed reconcile removals leave no marker;
  SIGKILL/unresolved reconcile → next build recovers; a repair with an
  ordinary page error settles.
- `tests/repair_campaign.py` repair-failure, interrupted-repair,
  sweep-failure (the latter runs only as non-root; verified as `ubuntu`).
- `tests/consumer_dependency_snapshots.py` post-hook-removes-output: no
  marker, last-good output restored, plain build recovers.
- `tests/identity_publication_contracts.py` line 53 (failed build after
  sibling discovery): no marker; plain build after the fix. Runs only in the
  identity certification (needs baseline/probe binaries) — not run here.
- `scripts/checkpoint8_filesystem_transaction.py` two kill/short-write cases:
  the next plain build recovers (announced) instead of refusing.
- `tests/runtime2/clock_lifecycle.sh`: killed build recovered by plain
  `nift build` instead of `--repair`.

Regression suite (`runtime-2` @523d1df): no contract asserts the lock-out
(several delete `.nift/.unfinished` after expected failures, which is now
harmless). Prepared, not pushed: a new `contract/build_recovery_smoke.sh`
wired into `run-contract.sh` (patch:
`docs/runtime-2/evidence/feature-build-recovery/regression-suite.patch`;
`git apply` on the suite's `runtime-2` branch). The new module passes
against this branch and fails against the pre-change binary.

## Residual cases that still keep the marker

Only genuinely uncertain state; each is recovered automatically by the next
build once its cause is gone, and `--repair` does the same explicitly:

- **Nift-owned write failures** (disk full, permissions, a directory where an
  output or page-info belongs): the bytes on disk are not known to be either
  the old or the new version (CP3 outputs are written in place).
- **Restore or failed-list write failures** after a page failure.
- **`reconcile_watch` failing after it removed files** (the only reconcile
  failure that can leave work half done).
- **A repair that could not settle every page** (its pre hook failed, an
  unknown prerequisite) or whose required sweep failed.
- **Process death** (SIGKILL, crash, power loss) at any point.

Not automated: `untrack`/`rm`/`cp`/`mv` on a stale marker refuse and ask for
`nift build` (proposal below).

## Independent review round 1 (fixed)

- **F1** failed-list counts (`1e11`, `1e300`) hung builds and `--repair`:
  the list now holds names only; any other shape is "damaged" → every page
  dirty → bounded full rebuild. Test: eight malformed lists × build, repair,
  status, then convergence to a clean build.
- **F2** extra pagination pages of a failed attempt were remembered only in
  the list: they are now removed when the page fails, and the repair sweep
  registers every currently paginated page's namespace (also at one page),
  so an interrupted attempt's surplus is swept. Tests: deleted, damaged and
  kept list, `--repair`, and a kill after writing the pages.
- **F3** restore replaced a symlinked output with a regular file: restored
  through the link. Tests: links inside the project, outside it and broken
  (dangling), through success, failure, kill-recovery and `--repair`.
- **F4/F5/F7** documentation above (exceptions, `--auto`, kill cost,
  proposals). **F6** pruning on every build and on `untrack`/`rm`/`mv`;
  re-tracking a name starts clean (test). **F8**
  `checkpoint8_filesystem_transaction` now checks the automatic recovery
  itself restores the full output before any `--repair`;
  `consumer_dependency_snapshots` byte-compares the restored output.
- The review's randomized tester is ported as
  `tests/build_recovery_random.py` (bounded: 80 steps × 3 modes, part of
  `make test-build-recovery`; `RECOVERY_RANDOM_STEPS`/`SEEDS` for soaks).
- Repair-sweep limitation (pre-existing): the root `index` page paginates as
  `2.html`, `3.html`, …, a namespace the sweep does not claim (a user's
  `404.html` would match it), so a *crash* after writing extra index pages can
  leave them; failures (not crashes) remove them at once.

Results after the fixes (@0d0c512, run sequentially, `make -j1`; the regression
suite under the host heavy-job lock): `test-build-recovery` (now ~1,000 checks
plus the bounded randomized run, 80 steps × 3 modes, 0 problems) and every
suite listed under Results pass again; `checkpoint-8` 17/17 as non-root;
regression suite 94/95 with the new module (same root-only
`unreadable_source_smoke` failure as the pre-change binary). The review probes
p1–p4, p7 now behave as specified; p8 (inline `@script` scribbling its own
output) is the documented exception.

## Overlap with feature-site-dx

`build --auto` (src/CLI.cpp, watch loop) is not changed here: on this branch
it still exits after a failing pass; keep-watching after errors arrives with
feature-site-dx. It calls
`build_all(false)` on a fresh `ProjectInfo` each pass; after an ordinary
error that call now leaves no marker and a dirty failed list, so a
keep-watching loop rebuilds the failed pages on the next pass and recovers by
itself once the source is fixed. A pass that finds a stale marker recovers
automatically. Note for site-dx: a still-broken page is retried on every
pass (as before, for errors that occur before any write), so its error text
repeats each pass. src/CLI.cpp is touched only in the `untrack/rm` and
`cp/mv` refusal messages (and comments), outside the `--auto` block.

## Proposals (outside this bounded checkpoint)

- **Incremental crash recovery.** Record the epoch's intended page set in the
  marker (one fsync'd write before the first page mutation) so a stale
  marker rebuilds only those pages plus sweeps instead of everything.
- **Restore last-good after a crash in the output write** for pages without
  scripts: needs temp+rename or backups for every page, which CP3 rejected
  for cost; a crash is recovered by rebuilding instead.
- Automatic recovery for `untrack`/`rm`/`cp`/`mv` on a stale marker.
- `nift status` does not report `.nift/.unfinished` today (it can say "up to
  date" after a kill, while the next build will recover); it should say so.
- **SIGINT/SIGTERM handler.** Ctrl-C today is a kill: the marker stays and
  the next build pays a **full rebuild** (the repair epoch: every page, every
  hook in `repair` mode) — seconds on a 10k-page site. A handler that lets
  the current pages finish or cleanly abandon them (settled exactly like a
  failure) and then exits through the normal epoch end would need no marker
  at all; combined with incremental crash recovery above, only a genuine
  crash would cost a full rebuild.
