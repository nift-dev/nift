# Benchmarks

Runner: `benchmarks/runtime2/run.py --nift <binary> [--groups ...] [--runs N] [--out file.jsonl]`.
Groups: `startup`, `kernels`, `ops`, `process`, `async`, `site`. Every workload
checks its output against a golden value; a mismatch aborts the run.

## Rules

- **Gates use instructions.** `ops` and `site` profiles use callgrind
  (deterministic). Wall time is reported, never gated, on the shared 2-vCPU host.
- **Startup is separate.** Kernels are whole-process; `ops` measures marginal
  cost per operation (N=4,000 vs 8,000) so startup cancels out.
- **CPython both ways.** `ops` compares with function-local CPython (fast
  locals). Report the interpreter version; the host's `python3` is 3.13.16.
- **Same algorithm, same output.** No HTML-vs-Markdown style asymmetries.
- **Retain raw output** in `docs/runtime-2/evidence/cpNN/` as JSON lines with the
  host fingerprint in the first record.
- **Publish misses.** A gate that is missed is recorded with its cause before
  any further optimisation.

## Baseline (CP00, v4.12.0 `ce8cd36`, Xeon 2.1 GHz, 2 vCPU)

Raw: `evidence/cp00/baseline.jsonl`.

| Operation (marginal) | Nift instr | CPython 3.13 fn-local | Ratio |
| --- | ---: | ---: | ---: |
| Loop iteration | 3,084 | 394 | 7.8× |
| Add-assign | 4,957 | 647 | 7.7× |
| Flat call | 14,970 | 990 | 15.1× |
| Array index | 5,374 | 698 | 7.7× |
| Array push | 4,568 | 506 | 9.0× |
| Object member | 5,240 | 886 | 5.9× |
| String append | 9,381 | 1,787 | 5.2× |
| If + modulo | 6,006 | 779 | 7.7× |
| fib call, top level (legacy path) | 184,340 | 353 | 522× |
| fib call, inside a loop (prepared path) | 14,763 | 353 | 42× |

| Whole process (median of 3) | Nift | CPython 3.13 |
| --- | ---: | ---: |
| Startup (`x := 1` script / `pass`) | 2.1 ms | 13.7 ms |
| fib(22) top level / in loop | 1.415 s / 0.142 s | 0.016 s |
| 1M-iteration loop | 0.565 s | 0.065 s |
| 200k calls | 0.398 s | 0.042 s |
| sort_by 20k | 0.062 s | 0.024 s |
| 50k string appends | 0.153 s | 0.023 s |
| 200 calls passing a 100k array | 0.496 s | — |

| Systems | Result |
| --- | --- |
| 300 × `/bin/true` | Nift 0.478 s · Bash direct 0.360 s · Bash `$()` 0.443 s · Python 0.368 s |
| Futures 1k / 2k / 4k | 0.23 s / 0.82 s / 3.44 s (superlinear) |
| 2k-page Markdown site | full 0.69 s · no-op 0.076 s · one page 0.071 s · shared partial 0.72 s |
| 300-page build profile | 1.98M instructions per page |

The fib CPython figure here (353, fib(10)→fib(12)) is lower than the 509 quoted
in the design report (fib(14)→fib(16)); both are correct for their inputs. Gates
compare like with like using this runner only.

## CP11 — VM proof of concept (`runtime-2-cp11i`, after review round 3)

Runner: `benchmarks/runtime2/vm_compare.py` (`make bench-rt-vm`): the same
workloads and goldens on v4.12, the current legacy path, the VM with
`NIFT_VM=require` (no legacy fallback possible) and CPython 3.13.16. Raw:
`evidence/cp11/vm_compare.jsonl`; record: `checkpoints/cp11-vm-poc.md`.
Round 3 was measured on the final build (`c147f35`) on an **idle** host;
instruction counts are deterministic (round 2's shared-host figures are in
git history).

| Operation (marginal instr) | v4.12 | legacy | VM round 1 | VM round 2 | **VM round 3** | CPython fn-local |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Loop iteration | 3,066 | 3,129 | 104 | 98 | 98 | 397 |
| Add-assign | 4,957 | 5,002 | 184 | 179 | 179 | 649 |
| **Flat call** | 14,994 | 13,414 | 467 | 461 | **461** | 953 |
| Array index | 5,356 | 5,401 | 380 | 381 | 381 | 703 |
| Array push | 4,568 | 4,613 | 286 | 326 | 326 | 526 |
| Object member | 5,240 | 5,303 | 347 | 348 | 348 | 806 |
| String append | 9,385 | 9,450 | 3,182 | 3,257 | 3,256 | 1,797 |
| If + modulo | 6,006 | 6,050 | 233 | 223 | 223 | 829 |
| fib call, top level / in loop | 184,321 / 14,766 | 159,234 / 14,760 | 465 / 465 | 471 / 471 | 471 / 471 | 457 / 427 |

| Whole process (median of 5, idle host) | v4.12 | legacy | VM | CPython |
| --- | ---: | ---: | ---: | ---: |
| fib(22) | 1.266 s | 1.058 s | 0.0062 s | 0.016 s |
| fib(25) | 5.354 s | 4.799 s | 0.016 s | 0.023 s |
| 1M-iteration loop | 0.490 s | 0.483 s | 0.025 s | 0.058 s |
| 200k calls | 0.341 s | 0.299 s | 0.011 s | 0.027 s |
| Startup | 3.6 ms | 3.9 ms | 3.7 ms | 15.7 ms |

Compilation (callgrind, inside `rt::compile_unit`): 78.3k instructions for
`x := 1; print(x)` (75.2k after round 2), 29.9M for a generated
500-function, 66 KB program (27.8M after round 2): round 3's iterative
depth pre-pass and iterative prepared-context walks.

CP11 gate (flat call ≥ 5× fewer than v4.12 and ≤ 2× CPython): **met**,
32.5× fewer and 0.48× CPython (unchanged from round 2). Round 3 changes no
VM hot path (compile-time checks and the out-of-line aggregate-equality
walk only).
