# Authoritative plan reconciliation (pre-implementation sync)

Consolidation pass against the recap. Verified against the current repository,
handovers, performance audit, design reviews, and the Rust programme.
Analysis only; no production code changed.

## A. RETAINED (agree with current repo / handover / evidence)

1. Nift Embed objective: one template language, build-time + request-time via
   backend languages; Nift is templating/glue, not an application framework.
   Consistent with `docs/handover/NIFT-BINDINGS-PROGRAMME.md` and EMBED.md.
2. Canonical C++ -> small stable C ABI -> thin bindings; bindings contain no
   Nift semantic logic. Consistent with the bindings programme.
3. nift-rs as the deliberate independent conformance experiment (not a
   permanent multi-implementation strategy). Its NR0-NR12 programme is
   complete and signed off; it continues passing the shared corpus
   independently.
4. Conformance corpus is a first-class guarantee; every future binding gets an
   acceptance gate through the C ABI.
5. Merge gate: at least one real binding working end-to-end through the C ABI
   before nift-embed merges into main Nift. Main-repo integration remains
   HOLD.
6. Performance attribution (PERF-REGRESSION-AUDIT.md): atomic temp/rename
   ~25-30 ms/10k, Embed render seam ~5-6 ms, permission preservation ~noise,
   no unexplained bookkeeping chunk.
7. Persistence split: authoritative state (`.nift/config.json`,
   `.nift/tracked.json`) retains atomic persistence; regenerable derived state
   (outputs, `.info.json`, pagination outputs) may use direct writes under the
   project-level recovery protocol. CONFIRMED the authoritative half already
   holds today: `save_json_file` -> `filesystem::write_file` ->
   temp+replace (JsonFile.cpp:16-17, FileSystem.cpp:252-262).
8. `.nift/.unfinished` invariant: exists == the previous/current
   derived-state mutation epoch has not been proven to complete.
9. Two-layer model: process-held OS lock (live ownership) + persistent
   `.unfinished` (crash evidence); flock/LockFileEx abstraction; no PID
   ownership/liveness/timestamps/UUID logic. `reconcile_watch()` confirmed as
   the first derived-state mutation in `build_all`/`build_names`.
10. Mutation ordering: acquire lock -> durably establish `.unfinished` ->
    mutation epoch (reconcile_watch, writes, pagination cleanup, metadata) ->
    remove `.unfinished` last -> release lock.
11. Ordinary builds REFUSE on `.unfinished`; no silent automatic recovery.
12. Explicit repair = `nift build --repair`, usable without a marker, refuses
    if the lock is live-held, no `--force`.
13. `--repair` semantics distinct from `--all` (distrust + reconstruct/sweep).
14. Pagination ordering is a hard, regression-tested contract: outputs ->
    stale pagination cleanup -> `.info.json` LAST.
15. All derived-state mutators participate in exclusion, including
    untrack/rm/del/cp/mv (via `remove_page_build_state`, CLI.cpp:333-341); the
    list must be re-audited during implementation.
16. Final build grammar: bare `build` incremental; positional names;
    `--all/--auto/--repair`; modes mutually exclusive; no `--updated`; no
    `--names`; `/` is the index name; old verbs removed without aliases.
17. `info` follows the same grammar (positional names already work today,
    CLI.cpp:1000).
18. Repository hygiene before every handoff (revert instrumentation, remove
    worktrees/fixtures, make clean, verify git status).

## B. CONTRADICTIONS / REFINEMENTS

No recap item contradicts an established requirement or current evidence. One
factual nuance to tighten:

- Recap section 6 calls the pagination-history read "small but measurable."
  The comparative profiling measured it at ~12 ms/10k on the unchanged
  workload - the SECOND-largest residual after atomicity (~25-30 ms), and the
  largest single non-atomicity cost. Recommend wording it as "~12 ms/10k, the
  dominant non-atomicity residual" so the implementation order reflects that
  it is a deliberate cost (pagination lifecycle) with a known lever (probe
  removal / ever-paginated registry), not a rounding error.

## C. MISSING (agreed items the recap did not restate)

1. Compare+touch status: compare+touch persistence remains CURRENT production
   behavior and should stay until the `.unfinished` direct-write model is
   adopted; adoption then supersedes it for regenerable state (see D). Not a
   contradiction - an explicit "until then" handover note.
2. CI/pipeline recovery behavior: after a crash the next ordinary build
   REFUSES. Pipelines must call `nift build --repair` (idempotent) or be
   documented as failing closed until repaired. Not in the recap.
3. Cross-platform evidence: the flock/LockFileEx abstraction must be tested on
   Linux, macOS, and Windows with equivalent observable semantics (from the
   earlier review message; not restated in the recap).
4. `-p` orthogonal option retention across all build/info modes.
5. `build --auto` (watch) semantics under the marker: acquire per mutation
   pass, not for the whole session; a refusal inside the watch loop stops the
   watch with the repair message rather than spamming. (Section 15 mentions
   "correct per-mutation-pass ownership" - retained here as an explicit
   behavior.)
6. `.nift/build-auto.log` is build-derived bookkeeping written by `build-auto`;
   it joins the derived-state set (regenerable, not authoritative).
7. nift-rs programme completion status (NR0-NR12 signed off, 106-case
   corpus, Rust passing independently) as context for section 3.

## D. SUPERSEDED (do not follow these)

1. Automatic `.unfinished` recovery (silent full rebuild on marker presence) -
   superseded by refusal + explicit `build --repair`.
2. `nift build-repair` verb - superseded by `nift build --repair`.
3. `nift build --names X` - superseded by positional names (`nift build X`).
4. `nift build --updated` - never adopted; bare `build` is incremental.
5. Historical verbs `build-all`, `build-updated`, `build-names`, `build-auto` -
   removed, no aliases, no deprecation period.
6. `info-all`, `info-watching` spellings - folded into `info --all` /
   `info --watching` (see E).
7. compare+touch per-file persistence - superseded FOR REGENERABLE STATE if
   the `.unfinished` direct-write model is adopted; retained in production
   until then and for authoritative state.
8. PID ownership / process-liveness polling / timestamps / UUID ownership /
   stale-lock heuristics for the marker - explicitly rejected; the OS-held
   lock answers liveness.
9. Benchmark-only diagnostics (NIFT_NO_PREV / NIFT_NO_PERMS /
   NIFT_NO_COMPARE) - reverted, prototype-only, not production.

## E. CLI recommendation - the whole info family

Inspection resolves the open question: current `info-names` (CLI.cpp:992-997)
takes NO arguments and emits a JSON list of ALL tracked names
(`{"tracked": [...]}`). It is genuinely the "list the tracked names
themselves" operation - NOT "info for particular names" (that is the existing
`info <names>` path at CLI.cpp:1000-1019). The reviewer's test "if it is just
the old way of requesting info for particular names, it should disappear"
fails: it is a distinct, cheap, script/benchmark-facing query
(tracking_scaling_benchmark.py:36,:55 measures it precisely because it does
not open per-page `.info.json` files).

Recommended final surface:

```
nift build               incremental (stale pages)
nift build /             build index
nift build foo bar       build named pages
nift build --all         full build (trusted state)
nift build --auto        watch/continuous
nift build --repair      distrust + reconstruct derived state

nift info                normal info (all tracked entries)
nift info /              info for index
nift info foo bar        info for named pages
nift info --all          explicit all-entries mode (= bare info; documented
                         equivalence, NOT invented different semantics)
nift info --watching     watching info            (folds info-watching)
nift info --tracking     tracking-file + count + entries (folds info-tracking)
nift info --names        list the tracked names themselves (folds info-names;
                         bare, takes NO arguments)

nift status              unchanged
```

Mutually exclusive modes:
- build: positional names / --all / --auto / --repair
- info:   positional names / --all / --watching / --tracking / --names

`info --names` is the ONE surviving `--names`, and it is safe from the
terminology tangles: it is a flag that takes no arguments, so `info --names`
(list names) can never be confused with `info foo` (info for foo); the parser
rejects `info --names foo` as a mode+names conflict. Rationale: the operation
genuinely exists, is distinct (cheap names-only listing), and folding it keeps
the verb-suffix pattern (`info-names`, `info-tracking`) from surviving the
cleanup. If zero `--names` anywhere is preferred, the alternative is to keep
`info-names` as a standalone verb - but that perpetuates exactly the pattern
this cleanup targets; dropping the query and reworking
tracking_scaling_benchmark.py to parse tracked.json directly is the third
option. Recommendation: `info --names` (bare).

`status` stays a top-level verb (23 files depend on it; genuinely a different
query). `info --all` vs bare `info` are equivalent today (CLI.cpp:1021-1033);
keep the explicit `--all` for symmetry with `build --all` and document the
equivalence.

## F. Implementation order (safest checkpoint sequence from HEAD)

Each checkpoint: implement, run the full battery (33 test targets +
conformance 9/9 + sanitizer where relevant), update docs, clean the tree,
commit, STOP for review.

0. Commit this reconciliation as the authoritative plan record.
1. CLI migration (independent of persistence): new `build`/`info` grammar,
   mutual-exclusivity errors, remove old verbs and `info-all`/`info-watching`/
   `info-tracking`/`info-names`, add `--repair`/`--auto`/`--names` modes,
   `status` unchanged; migrate the ~55-60 surface files (tests, benchmarks,
   conformance runner + gen_golden, help/error text, README/PERFORMANCE/
   HANDOVER, docs; CI is indirect via scripts; do not rewrite historical
   evidence records). Verify with the existing battery - this lands the
   grammar without touching persistence.
2. Lock + `.unfinished` productionization (still atomic writes):
   flock/LockFileEx abstraction; acquisition protocol (O_EXCL + flock; refusal
   on live/stale); mutation-boundary placement before `reconcile_watch()`;
   `fsync(marker)`; refusal UX; `build --repair` as a distrust forced rebuild;
   all-derived-mutator exclusion (untrack/rm/cp/mv); watch per-pass ownership;
   pagination-ordering regression test. Cross-platform (Linux/macOS/Windows)
   evidence for the lock.
3. Direct-write persistence for regenerable state under the now-production
   `.unfinished` protocol; authoritative state stays atomic. Verify:
   byte-identical conformance, pagination equivalence, output-permissions,
   crash-recovery, A/B/C benchmark (~105 ms target), sanitizer builds.
4. Repair hardening + permanent adversarial recovery suite (the 13-state
   campaign as regression tests); power-loss fsync note; docs.
5. Cross-platform + benchmark evidence checkpoint: full matrix, published
   numbers, PERFORMANCE/HANDOVER updates.
6. Bindings programme: design C ABI -> first real binding -> conformance
   through the binding -> static+SSR dogfood example. This precedes the merge
   gate decision for Embed into main (HOLD until then).
7. Per-checkpoint hygiene: revert instrumentation, remove worktrees/fixtures,
   make clean, verify git status before every handoff.

## G. STOP

Stopping for review here. No production code changed; no worktrees, binaries,
or objects in the tree.
