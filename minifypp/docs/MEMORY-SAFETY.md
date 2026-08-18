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
