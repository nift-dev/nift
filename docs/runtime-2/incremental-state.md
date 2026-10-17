# Incremental build state (Runtime 2.0)

Two kinds of per-page state live under `.nift/`:

| State | Path | Contract |
| --- | --- | --- |
| Page build metadata (page-info) | `.nift/<output>.info.json` | **Reproducible**: identical inputs give identical bytes (a clean rebuild reproduces it exactly; BH6). Holds dependencies, requirements, observed dynamic reads (CP02), snapshots, sidecar certificate. |
| Modified-mode timing record | `.nift/clock/<same path>` | **Correctness state, not reproducible.** Holds `racy-dependencies` (dependencies stamped after the page's render clock) and `tick-hashes` (content hashes of dependencies stamped in the render clock's own tick). Contents depend on timing by design. |

Rules for the timing record (CP01; `tests/runtime2/clock_lifecycle.sh`):

- Written for every page rebuilt in `modified` mode, even when empty; removed
  when a page is rebuilt in `hash`/`hybrid` mode.
- **Missing, unreadable or malformed** (deleted `.nift/clock`, interrupted
  write, metadata from before the record existed, switching back to
  `modified` mode): status rebuilds the page ("page timing record is missing or
  invalid"). It is never treated as "nothing racy".
- A stale racy entry only causes one extra rebuild, after which it is gone.
- Removed with its page (`rm`, untrack, watch-list removal) and pruned for
  pages no longer tracked on the next build that has work to do (never on a
  no-op build), so the directory is bounded by the tracked set.
- A build killed mid-render leaves `.nift/.unfinished`; the existing contract
  requires `nift build --repair`, after which state is consistent.
- Timestamp granularity: the render clock is read back from the filesystem,
  so comparisons use the filesystem's own granularity (coarse clocks such as
  FAT's 2 s make same-tick cases more common; those are resolved by tick
  hashes). Residual risk: an edit after the read within the render clock's
  tick is not detected in `modified` mode; `hash` mode has no such window.
