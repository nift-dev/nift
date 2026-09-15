# CP28 memory/lifetime findings (evidence)

Environment: Linux 7.0.0-29 x86_64, tmpfs /tmp, i7-12700H, g++ 15.2.0.

## struct_instances_ retention
- `Parser` is a function-local in `ProjectInfo::build_one` (per page), so
  `struct_instances_` and all parser state are destroyed when each page render
  completes. No cross-page retention.
- Empirical: two sequential pages each constructing 50,000 structs peaked at
  the same RSS as a single such page (31.7 MB vs 31.5 MB), not 2x.
- Within a page, instance bookkeeping grows O(instances created) by design
  (reference semantics: aliases must resolve to live instances). Measured
  linear scaling with no super-linear blowup:
    50,000 instances -> 31.5 MB peak RSS
   100,000 instances -> 58.6 MB peak RSS
   200,000 instances -> 112.1 MB peak RSS
  (~560 B/instance, purely linear).

## Allocation growth / repeated builds in one process
- `valgrind-memory-safety-checkpoint-4` (watch-mode endurance, 30 cycles under
  Valgrind): RSS flat at 3.8 MB across the run -> no allocation growth.
- `memory-safety-checkpoint-4-watch-sanitize` (100 cycles under ASan): PASS.

## Sanitizer + Valgrind on v4.2 feature workloads
- All CP28 workloads (struct/method/alias/copy/deepcopy/while/function-control)
  run clean under ASan+UBSan+LSan (halt_on_error, detect_leaks=1).
- Targeted Valgrind memcheck (--error-exitcode=99, --leak-check=full,
  definite+possible leaks) on deepcopy/struct/method/alias workloads: clean.

## Maintained memory walls (all PASS on the CP28 candidate)
- checkpoint-3 core lifecycle (57 phases)
- checkpoint-4-watch-sanitize (100 cycles)
- checkpoint-6-sanitize integration (12 rounds, 30 pages, 4 failures)
- checkpoint-9 parser fuzz under ASan (1217 cases, 986 controlled errors)
- valgrind-memory-safety-checkpoint-6 (12 rounds, 40 pages, 4 failures)
- valgrind-memory-safety-checkpoint-4 (30 cycles, flat RSS)