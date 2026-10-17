# Checkpoint roadmap

Ten milestones, 28 checkpoints. Each checkpoint is a set of reviewable commits
with a pre-registered exit gate. Near-term checkpoints (CP00–CP08) are specified
in full here and in `checkpoints/`; later ones are specified to the level needed
for dependency planning and are refined when they become current (the
refinement is recorded in that checkpoint's file before work starts).

Perf gates use deterministic instruction counts (callgrind) as the gate and
wall-clock medians as the reported result. "Today" means v4.12.0 at `ce8cd36`
as measured in CP00.

## Matrix

| CP | Milestone | Title | Depends on | Exit gate (summary) |
| --- | --- | --- | --- | --- |
| CP00 | — | Baseline suite + campaign scaffolding | — | Suite runs < 20 min on 2 vCPU; raw evidence committed |
| CP01 | M1 Correctness | Modified-mode timestamp race (D1) | CP00 | 0 missed edits in 1,000 rapid edit→build cycles; existing suites green |
| CP02 | M1 Correctness | Dynamic reads become dependencies (D2) | CP01 | `exists`/`is_file`/`is_dir`/`stat` absence, `ls` globs, `getenv`/`@getenv` rebuild correctly in all 3 modes; differential test covers them |
| CP03 | M2 FS freedom | Remove project-root confinement; external dependencies | CP02 | Scripts, templates and hooks can read/write outside the project; metadata/output/cleanup protections kept; external deps tracked without perpetual rebuilds |
| CP04 | M3 Quick wins | Run top-level programs on the prepared path | CP00 | fib(22) top-level ≥ 8× fewer instructions; corpus parity |
| CP05 | M3 Quick wins | Remove env lookups and body copies from the call path | CP04 | Flat call ≥ 10% fewer instructions; no behaviour change |
| CP06 | M3 Quick wins | Template scanner dispatch | CP00 | 300-page build ≥ 30% fewer instructions; byte-identical output on corpora |
| CP07 | M3 Quick wins | Pipe-based process capture, PATH cache | CP00 | Launch ≤ Bash + 15%; streaming works; no `/tmp` capture files |
| CP08 | M4 VM proof | Written semantics + differential harness with Host-op trace | CP02 | `semantics.md` v1; harness runs corpus on two binaries and diffs stdout/stderr/exit/diagnostics/outputs/trace |
| CP09 | M4 VM proof | Value model library (`rt/value`) | CP08 | Unit + property tests; ASan/UBSan clean; lint rules enforced |
| CP10 | M4 VM proof | Lexer + parser for full script grammar | CP08 | Parses 100% of corpus scripts; diagnostics identical on the error corpus |
| CP11 | M4 VM proof | **VM proof of concept** (subset, whole-unit fallback) | CP09, CP10 | **GO/NO-GO**: flat call ≥ 5× fewer instructions than today and ≤ 2× CPython; parity on every compiled unit |
| CP12 | M5 Runtime | Resolver complete: globals, `GETDYN`, modules, structs, enums; bail-and-replay removed (D-013) | CP11 | Fallback count for scoping constructs = 0 on corpus; no runtime replay path remains |
| CP13 | M5 Runtime | Errors, try/catch, diagnostics parity | CP12 | Diagnostic corpus byte-identical |
| CP14 | M5 Runtime | Built-ins batch A: collections and strings | CP12 | Batch fallback = 0; per-builtin instruction guards |
| CP15 | M5 Runtime | Built-ins batch B: JSON, bytes, numbers, time | CP14 | As CP14 |
| CP16 | M5 Runtime | Built-ins batch C: filesystem and process via Host ops; pipe-based capture/streaming with explicit ownership (D-011) | CP14, CP03, CP07 | As CP14; Host-op trace parity; pipe mode ≥ competitive with Bash on the D-011 benchmark set; no hangs/leaks |
| CP17 | M5 Runtime | Typed handles end-to-end; registry reclamation (removes D3) | CP16 | No `\x1fnift:` strings in VM paths; handle-forgery tests fail closed |
| CP18 | M5 Runtime | Embedding: C ABI 1.4 program API; bindings | CP13 | ABI 1.3 tests unchanged; new API tested in C and Python |
| CP19 | M6 Shell | Command-mode front end on the shared compiler | CP12 | Shell corpus parity; mode rules specified |
| CP20 | M6 Shell | Spawn + streaming pipelines + redirection on Host ops | CP19, CP07 | Launch ≤ Bash + 10%; 3-stage pipeline streams |
| CP21 | M6 Shell | Interactive layer: completion, history, rc compile, job control | CP20 | Prompt ≤ 3 ms; job-control tests green on Linux/macOS |
| CP22 | M7 Concurrency | VM coroutines + work-stealing scheduler + event loop | CP12 | 10k sleeps of 10 ms in < 0.2 s; linear spawn scaling to 100k |
| CP23 | M7 Concurrency | Capture-only isolates, channels, cancellation | CP22 | Labs concurrency suite; no data races under TSan |
| CP24 | M8 Templates | Template compiler; Host-op dependency recording | CP16, CP02 | Byte-identical output on Labs sites; build time ≤ today; RSS ≤ today + 10% |
| CP25 | M9 Retirement | Fallback count zero; delete legacy evaluator | CP13–CP24 | Zero fallbacks on all corpora for one full test cycle; legacy files removed |
| CP26 | M9 Retirement | Remove parity scaffolding and dead flags | CP25 | Build and suites green; code-size report |
| CP27 | M10 Demonstration | End-to-end campaign: performance, cross-platform, final report | CP26 | All perf targets reported (met or explained); Linux/macOS/Windows CI green |

Parallelism: CP04–CP07 are independent of each other and of CP01–CP03.
CP09 and CP10 are independent. After CP12, the M5 batches, M6 and M7 can
proceed in parallel.

## Standard checkpoint record

Every `checkpoints/cpNN-*.md` contains these sections: Objective, Scope,
Implementation, Compatibility, Tests, Performance, Exit criteria, Risks,
Deliverables, Commit strategy, and (once executed) Evidence and Reviews.

## Near-term checkpoints

### CP00 — Baseline suite and scaffolding
- Objective: reproducible numbers that every later checkpoint is compared with.
- Scope: `benchmarks/runtime2/` (new), `docs/runtime-2/`; no source changes.
- Implementation: one runner `benchmarks/runtime2/run.py` with groups `ops`
  (callgrind marginal instructions per operation vs CPython module-level and
  function-local), `kernels` (fib, loop, calls, sort-by-key, string build, JSON
  transform; cold wall time vs CPython and Bash where meaningful), `process`
  (300 launches vs Bash/Python), `async` (1k/2k/4k futures), `site` (300-page
  callgrind profile; 2k-page full/no-op/one-page/shared-partial wall + RSS),
  `startup`. Output: JSON lines with host fingerprint.
- Compatibility: none affected.
- Tests: the runner verifies every workload's output against a golden value.
- Performance: n/a (this is the baseline).
- Exit: `run.py --all` completes in < 20 min on 2 vCPU; JSON committed under
  `evidence/cp00/`.
- Risks: noisy wall time → gate on instructions; record host.
- Deliverables: runner, evidence, `benchmarks.md`.
- Commits: (1) scaffolding docs, (2) runner, (3) evidence.

### CP01 — Modified-mode timestamp race (D1)
- Objective: an edit made immediately after a build must never be missed.
- Root cause: `write_readonly_file` refreshes unchanged metadata with
  `fs::last_write_time(path, clock::now())` (user-space clock) while the kernel
  stamps ordinary writes from its coarse clock; edits within one tick get an
  older mtime than the metadata.
- Implementation: stamp with the kernel's notion of "now" (`utimensat(…,
  UTIME_NOW)` / `futimens`; `SetFileTime` with current system time on Windows),
  and additionally record each dependency's observed mtime at read time in
  page-info so modified mode compares the dependency against its *own* recorded
  value when available (racy-timestamp rule: a dependency whose mtime is ≥ the
  build-start time is re-checked by hash). Keep `>=` fallback for old metadata.
- Compatibility: metadata gains optional fields; old metadata still read.
- Tests: tight-loop reproducer (1,000 cycles) as `make test-modified-race`;
  existing `test-incremental-modified-immediate` passes on this host;
  incremental differential suites.
- Performance: no-op 10k build within +5%.
- Exit: 0/1,000 missed; suites green; review passes recorded.
- Risks: filesystems with 1–2 s mtime granularity (FAT, some network FS) →
  racy rule hashes them; test with a simulated coarse clock.
- Commits: (1) test that fails, (2) fix, (3) docs/evidence.

### CP02 — Dynamic reads become dependencies (D2)
- Objective: every read that can change rendered bytes is either recorded or
  explicitly documented as untracked.
- Implementation: new page-info sections, all optional and versioned:
  `absent` (paths whose non-existence was observed; stale if they appear),
  `listings` (directory paths whose entry list was observed, with a listing
  fingerprint; stale if entries change), `env` (name → observed value or null).
  Record from: `exists`/`is_file`/`is_dir`/`stat`/`file().exists()`
  (existing → dependency, missing → absent), `ls`/glob (each scanned
  directory → listing), `getenv`/`env`/`@getenv` (env). Process output stays
  untracked by design; document it and point to `@dep`.
- Compatibility: pages that used these reads rebuild once after upgrade.
- Tests: reproducer suite (absent→present, glob add/remove, env change) in all
  three incremental modes; extend the randomized differential test to generate
  these reads.
- Performance: no-op 10k within +5%; listing fingerprints only for pages that
  used globs.
- Exit: reproducers green in modified/hash/hybrid; differential test extended.
- Risks: over-recording causing spurious rebuilds (e.g. `ls` of large trees) →
  record listing fingerprints per directory, not per file.

### CP03 — Filesystem freedom
- Objective: Nift programs get Python/Bash filesystem semantics by default.
- Findings (v4.12): standalone scripts are already unconfined unless
  `--fs-root`/`NIFT_FS_ROOT` is set; project builds (templates, `@script`,
  build hooks) reject paths outside the project ("path must stay inside the Nift
  project", e.g. `ParserExpression.cpp` checks on `resource_path_authority_
  .project_root`); `@input` of an absolute path works but its dependency is
  stored as a `../../..` relative path that `metadata_path_is_safe` rejects, so
  the page rebuilds on every build.
- Implementation: drop `enforce_project_root` for general filesystem
  operations; keep `--fs-root` as an explicit opt-in policy; keep integrity
  checks that protect Nift-owned state (tracked output paths, `.nift/`
  metadata, cleanup/`rm`, package-entry containment); store external
  dependencies as absolute paths (`external:` prefix in page-info) and validate
  them with normal status checks; remove per-operation canonical containment
  work from general operations.
- Compatibility: deliberate behaviour change approved by owner (decision D-002).
- Tests: outside-directory read/write, absolute, `..`, symlink in/out,
  neighbouring repo, external dependency incremental behaviour, cleanup never
  touching unmanaged files, `--fs-root` still enforced when given.
- Performance: path-op instruction counts reported (expected decrease).
- Risks: some tests encode confinement as a contract → update them as an
  explicit, documented contract change, not silently.

### CP04 — Top-level programs on the prepared path
- Objective: remove the 10–12× penalty for code outside loops.
- Implementation: investigate why `execute_native_program` → `Parser::parse`
  takes the legacy route for top-level statements while loop bodies use the
  prepared statement executor; route top-level statement lists through the
  same executor with the same admission rules.
- Tests: full suite; NRS/PRS if available; differential corpus once CP08 exists
  (CP04 may land before, using existing suites + targeted side-effect probes).
- Performance gate: fib(22) top-level instructions ≥ 8× lower.

### CP05 — Call-path hygiene
- Implementation: snapshot `NIFT_*` feature flags once per process; stop
  `trim_copy`ing function bodies per call; reuse scope maps where safe.
- Gate: flat call ≥ 10% fewer instructions; no behaviour change.

### CP06 — Template scanner dispatch
- Implementation: replace per-position keyword `compare` chains in
  `Parser::parse` with first-character dispatch and `memchr` scanning; cache
  `strlen` of keyword constants.
- Gate: 300-page build ≥ 30% fewer instructions, byte-identical outputs.

### CP07 — Process capture and PATH cache
- Implementation: replace temp-file capture with pipes drained by `poll`
  (Windows: overlapped pipes or threads); cache PATH resolution per PATH value.
- Gate: launch ≤ Bash + 15%; streaming test; no files created in `/tmp`.

### CP08 — Semantics and differential harness
- Implementation: `semantics.md` v1 from probes and source; `tools/rt-diff/`
  runs a corpus against two `nift` binaries and compares stdout, stderr, exit
  code, diagnostics, output files and an ordered Host-op trace (trace emitted
  by a `NIFT_TRACE_HOST=fd` hook added to the legacy engine).
- Gate: harness runs the whole corpus on two identical binaries with zero
  diffs, and detects seeded differences.

## Later checkpoints (summary specifications)

CP09–CP27 follow the matrix above. Their detailed records are created when they
become current. Non-negotiable gates already fixed now:

- CP11 is a genuine go/no-go. If calls are not ≥ 5× cheaper than today, stop and
  report to the owner with profiles before continuing.
- CP24 may not regress website build time or memory beyond the matrix limits.
- CP25 requires the fallback counter to read zero across all corpora.
- CP27 reports every performance target from the owner's table, met or not.
