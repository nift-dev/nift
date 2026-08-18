# Memory-safety evidence contract

This repository uses `scripts/memory_safety.py` to record reproducible memory/resource-safety evidence. The runner is intentionally small and dependency-free so the same contract can be used by Nift, Jsonic++, Minify++ and tscc.

## What counts as evidence

Sanitizer and Valgrind findings are memory/lifetime failure oracles. Process RSS is a separate operational signal: allocators may retain freed capacity, so RSS growth alone is not called a leak. Long-running workloads must define a warm-up period and then evaluate whether memory settles into a bounded operating band.

The maintained sanitizer baseline is AddressSanitizer + LeakSanitizer + UndefinedBehaviorSanitizer. On Linux, focused Valgrind runs provide an independent leak check. `/usr/bin/time -v`, when available, records peak RSS for each subprocess repetition.

## Runner

Typical sanitizer evidence:

```sh
python3 scripts/memory_safety.py \
  --project PROJECT \
  --mode sanitizer \
  --iterations 10 \
  --output .build/memory-safety/example.json \
  --command './instrumented-workload --args'
```

Focused Valgrind evidence uses the same command with `--mode valgrind`; the runner adds full leak checking and a non-zero error exit code. `--mode rss` records process-level timing/RSS without sanitizer-specific environment variables.

`--warmup-iterations` excludes whole-command warm-up repetitions from RSS summaries. `--duration-seconds` can extend repeated commands for soak-style workloads. Persistent-process settled-memory sampling is workload-specific and will be added by the relevant project checkpoint rather than inferred from subprocess peak RSS.

## JSON result schema

Each result records:

- schema version, project and exact Git commit;
- UTC timestamp and OS/machine metadata;
- compiler and available Valgrind version;
- mode and exact argument vector;
- requested/completed iterations and duration;
- per-run exit status, elapsed time, peak RSS, finding classes and log tails;
- aggregate peak/RSS samples and a pass/fail summary.

A run passes only when every recorded process exits successfully and the runner detects no sanitizer/Valgrind finding. Raw JSON is build evidence, not a timeless project claim; public website results must also state the tested commit, platform/toolchain and workload.

## Checkpoint-0 smoke gate

`make memory-safety-smoke` proves that this repository can build an instrumented workload and emit the common evidence shape. It is infrastructure validation only, **not** the project's dedicated leak campaign or production memory-safety verdict.

## Checkpoint 1A — lifetime corpus and sanitizer/RSS soak

Validated 2026-08-18 against Jsonic++ commit `702c6c46e0dca757ef8d1ca9a51ef7f79c39bb3d` on Linux x86_64 with GCC 14.2.0.

The maintained `tests/json_memory_lifetime.cpp` workload repeatedly exercises valid scalar/object/array parse + dump round trips, a 2,048-item large document, 128-level nesting, malformed inputs including failures after substantial partial allocation, object/array mutation, copy/move assignment, large string ownership transitions, named-array streaming, callback early termination, and streaming failure after 2,048 successful items.

Evidence from the checkpoint:

- ordinary smoke and adversarial tests: pass;
- ASan + LSan + UBSan lifetime corpus: 120 in-process iterations, zero sanitizer findings;
- non-sanitized lifetime/RSS corpus: 400 in-process iterations, pass;
- non-sanitized current RSS: 10,624 KiB after warm-up, 10,688 KiB at midpoint, 10,688 KiB at completion on this run;
- process peak RSS for the non-sanitized run: 10,676 KiB as reported by `/usr/bin/time -v`;
- exact standalone/Nift mirror synchronization: pass; Minify++ vendored-header synchronization: pass.

The RSS result is evidence of a settled operating band for this workload, not a general leak proof. The sanitizer run's much larger RSS reflects sanitizer allocator/quarantine behavior and is not used as the steady-state memory baseline.

No parser defect or lifetime failure was found in Checkpoint 1A, so no production parser change was required.

## Checkpoint 1B — independent Valgrind confirmation

Checkpoint 1B is complete. On Linux 7.0.0-29-generic x86_64, Valgrind 3.26.0 ran `./.build/jsonic-memory-lifetime --iterations 40` against Jsonic++ commit `b9d0ff3` using the maintained wrapper. The run completed in 28.95 seconds with peak process RSS of 215,992 KiB. Valgrind reported 0 errors, 0 bytes in use at exit, 6,579,515 allocations and 6,579,515 frees, and “All heap blocks were freed -- no leaks are possible”.

The machine-readable result is retained at `docs/evidence/memory-safety-checkpoint-1b-valgrind.json`. Together with Checkpoint 1A's clean ASan/LSan/UBSan run and stable 400-iteration RSS soak, this satisfies the Jsonic++ memory/lifetime exit gate for the maintained corpus.
