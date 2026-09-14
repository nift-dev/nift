# JavaScript size optimization checkpoint 6

This measurement compares the untouched optimization baseline (`ed34b73`) with
the first five controlled optimization commits (`27aee03`). Both revisions were
built with GCC 13.3.0 using the repository's `-O2` benchmark target and run on
the same Linux 6.18.35 x86-64 host.

Command:

```sh
make benchmark BENCH_REPETITIONS=10000 BENCH_ITERATIONS=15
```

| Workload | Input bytes | Baseline output | Checkpoint output | Size change | Baseline MiB/s | Checkpoint MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| JavaScript | 850,000 | 739,999 | 729,999 | -10,000 (-1.35%) | 83.5 | 79.0 |
| JSX | 1,160,000 | 1,079,999 | 1,069,999 | -10,000 (-0.93%) | 73.2 | 66.9 |
| HTML | 760,000 | 759,999 | 759,999 | 0 | 99.1 | 95.2 |
| CSS | 1,000,000 | 870,000 | 870,000 | 0 | 243.2 | 251.7 |
| JSON | 710,001 | 700,001 | 700,001 | 0 | 58.5 | 61.7 |
| XML | 790,000 | 790,000 | 790,000 | 0 | 199.2 | 160.9 |
| SVG | 960,000 | 960,000 | 960,000 | 0 | 226.3 | 165.4 |

The synthetic JavaScript unit exercises semicolon removal but not the new
boolean, radix-integer, or quote-choice paths, so this is a lower-bound size
signal rather than a representative bundle comparison. Timing is recorded to
catch large regressions; a single shared-host run is too noisy for small speed
claims, as the unchanged non-JavaScript rows demonstrate.

Before this checkpoint, each optimization commit passed the smoke suite, Node
semantic differential suite, 15,459-program generated semantic corpus, and the
70,000-case deterministic fuzz suite.
