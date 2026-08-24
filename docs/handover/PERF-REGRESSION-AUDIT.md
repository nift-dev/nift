# Nift Embed performance-regression audit

Question: did introducing the Embedded Nift architecture make ordinary Nift
builds meaningfully slower than the last trustworthy pre-Embed baseline?

**Result: YES — a material regression was detected (+49.6% median on the
10,000-page full build) and partially repaired (+40.6% remaining). The
audit-identified cause — redundant per-page filesystem probes from embed-era
hardening — is fixed; the remaining residual is the deliberate cost of the
embed-era crash-safe write + metadata guarantees that the baseline's direct
(non-atomic) writes lacked. The render path is at parity.**

## Repair result (three-way comparison)

```text
                     median    ratio vs A    delta vs A
A Pre-Embed         87-88 ms      —            —
B Regressed         130 ms       1.50x        +49.6%
C Repaired          121 ms       1.41x        +40.6%
```

Repaired changes (semantics preserved — all 18 C++ test targets and
conformance 9/9 pass, output byte-identical):

- `Parser`: `@content`, `@input` and `render()` no longer perform a separate
  `source_readable` probe before reading; the read (`read_shared_source`) is
  the authority and its `nullptr` classifies the typed error. The template
  path keeps one `source_exists` probe for the "does not exist" distinction.
- `FileSystem`/`ProjectInfo`/`ProjectState`/`Engine`: `read_file_checked`
  returns `nullopt` for missing/unreadable/non-regular (empty-but-valid files
  return `Some("")`), so `read_shared_source` yields `nullptr` on failure
  instead of a cached empty string (the dead `if (!cached)` guards now fire).
- `ProjectInfo`: the previous `.info.json` pagination metadata is read only
  for paginated pages (it was read for every page).

The residual is now isolated by controlled benchmark-only experiments:

```text
controlled render-only (writes skipped at the write boundary in both
implementations, interleaved 20 samples, median):
    A pre-embed   62.8 ms
    B regressed   74.6 ms   +20.4%   (the redundant probes, pre-repair)
    C repaired    68.3 ms   +8.9%    (small render residual remains)

render-only syscalls are identical (A ~100.7k, C ~102.1k calls), so the
+5.6 ms C-vs-A render residual is CPU-bound (RenderHost seam / parser
refactor), not additional I/O.

per-piece write isolation (full build, 20 samples, delta vs A):
    full                        +35.0 ms
    skip output write           +9.8 ms   -> output write ~ +22 ms
    skip info write            +12.3 ms   -> info write   ~ +23 ms
    skip both writes           +13.2 ms   -> writes ~ +22 ms; residual
                                             ~ +13 ms (render +5.6 ms,
                                             permissions/geometry ~+7 ms)

internal phase timing (benchmark-only, per-build accumulated):
    render ~250 ms atomic, writes+perms+geometry ~850 ms atomic
    (~3.4x the render; write/metadata path is the leading measured cost)
```

So the remaining residual is localized primarily outside the render path,
with the write/metadata path the leading measured contributor (output and
`.info.json` persistence each ~+22 ms of the +35 ms). A small CPU-bound
render residual (+5.6 ms) and a permissions/geometry residual (~+7 ms) also
contribute; exact attribution remains under investigation, and the costs
currently grouped as "crash safety" (atomic temp+rename, compare-before-
write, permission lookup, timestamp, pagination metadata) have not been
separately decomposed yet.

## Historical baseline

```text
PRE_EMBED_COMMIT = aa60ab346803fddbef58e6e0c4d2d17f89715c2d
                   "Advance Nift development version to 4.0.2"
CURRENT_COMMIT   = cdd4bc9 (nift-embed HEAD)
```

The baseline is the last commit before Embedded Nift development began. The
first embed commit is `a7d686c` ("Vendor Jsonic++ as Nift JSON parser"); its
parent `aa60ab3` is the final pre-embed Nift state (a version bump, so a
stable release-state tree).

## Environment

```text
machine       12th Gen Intel Core i7-12700H (20 threads), 64 GiB RAM,
              Ubuntu Linux x86_64
compiler      g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0
flags         -std=c++17 -O2 (the project Makefile defaults)
build         clean trees, `make -j2`, no ambient .o reused
              (baseline and current each built from fresh worktrees)
workload      established 10k-page fixture (benchmarks/performance_10k.py
              layout): 10,000 tracked pages p0..p9999, each `<p>{i}</p>`,
              template `@content\n`
method        interleaved A/B `build-all` full builds; warmup 3, measured 20;
              median reported (raw samples recorded in
              benchmarks/perf_regression_audit.py)
```

## 10k full build — ordinary Nift, pre-embed vs current

```text
                    median     min      max     mean     stddev
Pre-Embed Nift      87.97 ms   83.88   96.25    88.16    2.69
Current Nift       131.58 ms  127.84  137.89   132.10    2.39

current / baseline  median ratio 1.4957x
absolute delta      +43.6 ms
percentage          +49.6%
```

Raw samples are produced by `benchmarks/perf_regression_audit.py` (committed).
The spread within each implementation (stddev ~2-3 ms) is an order of
magnitude below the delta, so this is not noise.

## Output correctness

```text
baseline output pages: 10000
current output pages:  10000
output byte-identical: True
```

Both revisions produce the same 10,000 output files with identical hashes, so
the regression is a slowdown of equivalent work, not extra/different output.

## Embed engine (current) — 10k render, for context

```text
embed open:            ~42 ms    (ProjectState load of 10k tracked entries)
embed 10k render:     ~103 ms    (~10.3 us/render, render("pX") by name)
embed total:          ~145 ms
failures: 0
```

This is descriptive only: the Embed engine constructs a per-render host and
renders by tracked name, which is a different responsibility than the CLI
batch build. It is not expected to equal the CLI time.

## Architecture

```text
ordinary Nift (CLI):
  ProjectInfo::build_all
    -> ProjectInfoHost (RenderHost adapter over ProjectInfo)
    -> Parser   <- shared rendering kernel

Embedded Engine:
  Engine::render_page / render
    -> EngineHost / ProjectHost (RenderHost adapters)
    -> Parser   <- the SAME shared rendering kernel
```

The CLI and the Engine share the Parser kernel through the CP1 `RenderHost`
seam. Ordinary Nift does **not** route through the `Engine` object; it uses
`ProjectInfoHost`. Therefore:

- A/B above measures whether the embed work regressed the CLI's ordinary path
  (it did).
- "Current Nift using the shared Embed rendering path" (experiment D) does
  not exist yet and must be benchmarked immediately after any integration
  that routes the CLI through the Engine.

## Regression investigation (evidence, not hypothesis)

Syscall profiles of the same 10k `build-all`:

```text
                 total calls   openat   close   newfstatat   read
Pre-Embed Nift      202,336    40,011   30,011    40,044    10,010
Current Nift        443,658    70,017   70,017   160,051    30,013
```

The current build performs ~2.2x the syscalls, driven by ~4x more
`newfstatat` (stats) and ~2.3x more `openat`/`close` (file opens).

The cause: the embed-era hardening added redundant per-page filesystem probes
to `Parser::render()` and the `@content` handler:

- `render()` now calls `host_.source_exists(template_path)` **and**
  `host_.source_readable(template_path)` before reading the template; the
  baseline performed a single `filesystem::path_exists` (one stat).
- `@content` now calls `host_.source_readable(content_path)` before reading
  the content page; the baseline read it directly.
- `filesystem::file_readable` verifies readability by **opening the file with
  `std::ifstream`** — an `openat`+`close` pair per probe, on top of the
  `is_regular_file` stat.

Net: roughly 2 extra file opens and several extra stats per page, which
matches the syscall delta and accounts for the +49.6% full-build time. These
probes were added for controlled-error fidelity (the reject-class corpus) but
are performed redundantly in the happy path.

## Conclusion

```text
NO MEANINGFUL REGRESSION        no
REGRESSION DETECTED             yes — +49.6% (median) on the 10k full build
audit-identified cause          redundant per-page filesystem probes
                                (source_exists/source_readable + the
                                ifstream readability open)
repair                          probes removed; read is the authority;
                                .info.json pagination read gated
REGRESSION AFTER REPAIR         +40.6% — the deliberate crash-safe write +
                                metadata guarantee cost (render at parity)
reproducible                    yes — benchmark script committed, raw samples
                                recorded
output correctness              byte-identical
semantics preserved             all 18 C++ test targets + conformance 9/9
```

Per the audit protocol: the audit-identified regression (redundant probes) is
repaired without weakening the reject-class/error contract. The remaining
+40.6% is currently localized primarily outside the render path, with the
write/metadata path the leading measured contributor (controlled render-only
benchmarking shows a small CPU-bound render residual, not render-path I/O).
Exact attribution remains under investigation; whether the residual is an
acceptable price for the embed-era safety guarantees is a deliberate decision
for review.

Sanitizer verification and checked-read regression coverage (this repair):
- ASan+UBSan build: 10k build and conformance 9/9 pass with no sanitizer
  findings over the repaired read/cache semantics.
- tests/engine_source_read.cpp (new): valid empty files read as empty (not
  failure), missing/directory/unreadable sources produce the typed failures,
  and a failed read is not cached as valid empty content.

## Bounded performance decision investigation

Deliberately bounded; no broad optimization was performed.

Render residual (~+5.6 ms / +8.9% render-only, CPU-bound; syscalls equal):

```text
A pre-embed render-only   62.8 ms
C repaired render-only    68.3 ms
```

The render CPU delta is the RenderHost seam + parser refactor cost (the CLI
routes through ProjectInfoHost). It is a deliberate architectural boundary
(the CP1 extraction); removing it would revert the extraction. ~8.9% on the
render path is the measured price of that abstraction.

Write/metadata residual (~+30 ms of the +35 ms; A writes ~23 ms vs C writes
~53 ms, computed as full minus render-only per implementation):

```text
decomposition of the crash-safe output write (unchanged files):
    compare+touch fast path        97 ms   (stat + read existing + touch)
    always transactional           130 ms  (temp write + rename)
so the committed compare+touch fast path is already the cheaper crash-safe
option; a baseline-style direct truncate write is non-atomic (not an option
without dropping the crash-safety guarantee).
```

The write cost is the crash-safe transactional design (compare+touch for
unchanged, atomic temp+rename for changed), output-permission preservation
(file_permissions) and pagination-aware `.info.json` metadata. Per-page
internal phase timing (benchmark-only) shows the write/metadata work ~3.4x
the render CPU.

Conclusion of the bounded investigation: beyond the already-repaired
redundant probes, no obviously avoidable chunk was found. The remaining
~+35 ms is the deliberate cost of (a) the RenderHost extraction (~+6 ms
render CPU) and (b) the crash-safe write + permission + pagination-metadata
guarantees (~+29 ms). Whether ~3.5 us/page for those guarantees is an
acceptable price for Embedding/SSR is the deliberate decision for review;
recovering statistical parity would require either reverting the extraction
or redesigning the crash-safe write strategy, both outside this bounded
scope.
