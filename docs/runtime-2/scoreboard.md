# Runtime 2.0 scoreboard

A running table of the VM's performance and coverage, **one row per
milestone, appended, never rewritten** (owner request, CP12). The first two
rows are fixed references (v4.12 and CPython 3.13); they keep their
provenance and are only re-measured when the host changes (a new reference
row is then added, the old one kept).

Methodology (`benchmarks/runtime2/vm_compare.py`, see `benchmarks.md`):
"instr" = callgrind marginal instructions per operation (N=4,000 vs 8,000)
or per recursive call (fib(10)→fib(12)), deterministic; times = whole-process
wall median of 5 (ms); RSS = peak RSS of fib(22) (KiB); compile = callgrind
instructions inside `rt::compile_unit` and NIFT_VM_STATS wall µs, for the
16-byte script `x := 1; print(x)` ("small") and the generated 500-function
program (66,296 bytes), throughput per KiB of source and per function;
end-to-end = the `records20k` workload (`vm_compare.py` group `e2e`: 20,000
records built and processed with arrays, objects, string building and
functions), run with `NIFT_VM=1`, so the VM column shows its actual outcome
(it grows with coverage); corpus = rt-diff shared-corpus script entries run
on the VM (`vm` + `vm-error`) / total; general = `vm_gen.py` seed 1, 300
programs, share run on the VM. VM rows: `NIFT_VM=require` for the
micro-benchmarks (genuine VM execution).

| Milestone / commit / date | flat call instr | loop instr | fib call instr | string append instr | fib(25) ms | 1M loop ms | 200k calls ms | startup ms | peak RSS fib22 KiB | compile instr small / 500-fn | compile throughput small; 500-fn | e2e records20k ms (VM outcome) | corpus on VM | general VM % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- | --- | ---: |
| **Reference: v4.12** (`ce8cd36` binary; measured in the CP12 run, `aeefaa7` checkout, 2026-10-18) | 14,994 | 3,066 | 184,439 (top level; 14,766 in a loop) | 9,385 | 6,034 | 552.8 | 353.7 | 4.6 | 7,480 | — (no VM; tree-walking) | — | 1,390 | — | — |
| **Reference: CPython 3.13.16** (function-local code; same run) | 952 | 387 | 615 (top level; 518 in a loop) | 1,787 | 24.8 | 92.7 | 41.6 | 21.4 | 8,664 | — | — | 44.9 | — | — |
| CP11 final (round 3: `c147f35`, code = `bf32e4f`, 2026-10-18; `evidence/cp11/vm_compare.jsonl`, idle host) | 461 | 98 | 471 | 3,256 | 15.6 | 24.5 | 11.4 | 3.7 | 5,620 | 78,338 / 29,891,783 | 5.0 M instr, 2,298 µs per KiB; 462 k instr, 48 µs per KiB, 59.8 k instr, 6.3 µs per fn | 1,324 (fallback: prepared-fn-unprepared; CP11 binary re-measured in the CP12 run) | 75 / 757 (+ replayed units, CP11 record) | 22.7 (+ 8.0 replayed) |
| CP12 phase A, D-018 (`aeefaa7`, 2026-10-18; `evidence/cp12/vm_compare.jsonl`) | 426 | 107 | 287 | 3,248 | 11.3 | 30.4 | 15.0 | 4.5 | 5,704 | 93,796 / 63,235,789 | 6.0 M instr, 3,533 µs per KiB; 977 k instr, 111 µs per KiB, 126.5 k instr, 14.3 µs per fn | 1,284 (fallback: prepared-fn-unprepared) | 103 / 786 (no replay) | 24.0 (no replay) |

Notes per row:

* CP11 final: the micro-benchmark values are round 3's (idle host). Round 1
  (`84d9332`, `evidence/cp11/vm_compare-tables.md`) measured flat call 467,
  loop 104, fib 465, append 3,182, fib(25) 16.2 ms, startup 4.8 ms, RSS
  5,364 KiB; rounds 2/3 measured flat call 461. Re-running the CP11 binary
  on the CP12 host gives the same instruction counts (461 / 98 / 471 /
  3,257) and fib(25) 16.8 ms, 1M loop 28.0 ms, 200k calls 13.5 ms, startup
  4.6 ms (`evidence/cp12/vm_compare-cp11.jsonl`).
* CP12 phase A: load average 3.1 → 2.4 at the start of the run (decaying
  from the preceding sanitizer run; no other process): instruction counts
  are exact, wall times indicative (CPython's 1M loop took 74 ms in the CP11
  re-run minutes later vs 93 ms here). Compile cost doubled on the
  500-function program (kind inference, the D-018 may-double analyses,
  preparability and place-call checks); the end-to-end workload is still
  refused (a loop-called function legacy cannot prepare), so its time is
  legacy's.
