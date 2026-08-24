# Design question: clearing `.unfinished` on zero-mutation build failures

Answer to the reviewer's CP3-preamble question (investigation only; nothing
implemented in this pass).

## 1. Where the first actual derived mutation occurs (per build path)

All build entry points funnel through `build_all` (build / build X /
`--all` / `--auto` per-pass / `--repair`) or `build_names`:

```
acquire (lock + durable marker)
  -> reconcile_watch()          FIRST possible mutation
  -> reset_build_caches()       (in-memory, no mutation)
  -> job selection              (reads only)
  -> build_many -> build_one    FIRST page mutation
```

`reconcile_watch()` (WatchList.cpp:108):
- writes `.nift/.watch/<dir>/tracked.json` on EVERY pass for each configured
  watch directory (WatchList.cpp:178-181) - unconditional for watch projects;
- removes outputs/info/hash of disappeared pages ONLY when a watched set
  changed (WatchList.cpp:165-170);
- saves the main tracked.json only when `tracking_changed` (WatchList.cpp:185).
- For a NON-watch project (no watch directories): complete no-op.

`build_one` mutation phase (ProjectInfo.cpp), in the load-bearing order:
```
render (in-memory; errors here precede any write)
  -> minify (in-memory; errors here precede any write)
  -> write output(s) / pagination outputs     FIRST page mutation   (701/705)
  -> hash refresh (non-modified modes)                              (713)
  -> stale pagination cleanup (removals)                            (721)
  -> write_page_info (.info.json) LAST                              (623/727)
```
`build_many` runs pages in parallel worker threads, so "first mutation" is the
first successful page write, and a failure of one page does not prevent other
pages from having already written.

## 2. Can we cheaply track mutation_started?

Yes. A single `std::atomic<bool> mutation_started` on `ProjectInfo`, reset after
acquire and set via `mark_mutation()` immediately BEFORE each derived
write/removal:
- before the output / pagination-output write (build_one 701/705),
- before the stale pagination cleanup removals (721),
- before `write_page_info` (already covered by the output write, but explicit),
- before reconcile's disappeared-page output removals (WatchList.cpp:165-170).

The parallel build is safe: the flag is shared/atomic; the first page that
writes sets it, and the failure return happens after all workers join, so the
final check sees the true value.

## 3. The proposed decision rule

```
ordinary build (Clean acquire) or targeted build:
    success                                  -> finish() (clear marker)
    controlled failure, mutation_started==false -> finish() (clear marker)
    controlled failure, mutation_started==true  -> retain (repair required)
    crash/kill                                -> retain (destructor; finish never runs)

build --repair (Clean or Stale acquire):
    success                                  -> finish()
    ANY failure                              -> retain (the original stale
                                                 evidence is still unresolved)
```

The `--repair` distinction is essential: repair must never clear the marker on
failure, even with zero new mutations, because the pre-existing stale derived
state it was asked to reconstruct is still unverified.

## 4. Reliability across concurrent/watch/pagination/minification

- **Concurrent**: the ownership lock serializes mutators; the flag is per
  epoch. No interference.
- **watch (`--auto`)**: per-pass epoch. A pass that fails with zero page
  mutations clears its marker; the watch loop still stops on the error (next
  invocation starts clean). No change to the refusal/stops behaviour.
- **pagination**: pagination outputs and stale-pagination removals both mark
  mutation, so a failure after outputs/cleanup retains the marker. Correct.
- **minification**: minify errors happen in memory BEFORE any write, so a
  build where every page fails at minify records zero mutations and clears the
  marker - correct, nothing was written.
- **watch-state bookkeeping** (`.nift/.watch/<dir>/tracked.json`): deliberately
  does NOT mark mutation. It is an atomic (temp+rename) regenerable bookkeeping
  write that can never be torn, so it does not constitute "derived state that
  may be incomplete." Counting it would make every watch-project build retain
  the marker on failure, defeating the purpose.

## 5. The honest caveat (why the win is bounded)

The reviewer's motivating case - "edit template incorrectly -> build fails ->
fix typo -> build refuses" - is fixed ONLY when the failing build performed
ZERO page mutations. For a single-template site (all pages share the template),
a template syntax error fails every page before any write, so the marker is
cleared and the UX improves exactly as intended.

In a multi-template site where the broken template is used by a SUBSET of
pages, the other pages write successfully -> mutation_started=true -> the
marker is retained and `build --repair` is required, even though each written
file is individually valid (atomic writes) and a normal incremental build
would rebuild just the still-stale pages. This is the reviewer's explicit
conservative rule B ("once anything has been written or deleted, require
repair"), and it is retained. Relaxing B (trusting incremental staleness after
a partial build) is a further design decision, out of scope here.

## 6. Does it complicate CP3?

No - it is orthogonal and complementary. It is one atomic flag plus
mark-mutation calls at the existing write/removal points. With CP3 direct
writes it becomes MORE valuable: a controlled pre-write failure has nothing
torn to repair (no write occurred), so clearing the marker is not only safe
but exactly right. Crash-during-write still leaves the marker (finish never
runs), so crash safety is unaffected.

## 7. Recommendation

IMPLEMENT the distinction, as the first small change of the CP3 work (it
touches the same build_one surfaces). Rationale: cheap (one atomic flag),
sound (the failure-semantics analysis above shows zero-mutation failures have
no inconsistent derived state to repair), and it removes the
fix-typo-then-remember-to-run---repair friction for the common single-template
edit/error/fix cycle. Retain conservative rule B (any actual mutation ->
repair required) and the `--repair`-never-clears-on-failure rule. If desired
later, B could be relaxed to "recoverable-by-incremental" for atomic writes,
but that is a separate decision and not needed now.
