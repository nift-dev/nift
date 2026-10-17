# CP11 comparison tables (after review round 1)

Commit `84d9332`, 2026-10-18 08:25 (+11:00), Linux x86-64, 2 vCPU Xeon 2.1 GHz, otherwise idle (load average 1.00 / 0.99 / 0.93 at start, from the preceding build), g++ 13.3 -O2, CPython 3.13.16, valgrind 3.22. "VM (before review)" is the CP11 measurement at `25089e8` (`git show 0a0d43f:docs/runtime-2/evidence/cp11/vm_compare.jsonl`).

| Operation (marginal instr, callgrind) | v4.12 | legacy | VM (before review) | **VM** | CPython 3.13 fn-local | VM vs v4.12 | VM / CPython |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop | 3,066 | 3,129 | 105 | **104** | 388 | 29.48× fewer | 0.27× |
| add_local | 4,957 | 5,002 | 177 | **184** | 653 | 26.94× fewer | 0.28× |
| call | 14,994 | 13,414 | 464 | **467** | 980 | 32.11× fewer | 0.48× |
| array_index | 5,356 | 5,401 | 374 | **380** | 722 | 14.09× fewer | 0.53× |
| array_push | 4,568 | 4,613 | 277 | **286** | 535 | 15.97× fewer | 0.53× |
| object_member | 5,240 | 5,285 | 340 | **347** | 846 | 15.1× fewer | 0.41× |
| string_concat | 9,385 | 9,450 | 3,152 | **3,182** | 1,770 | 2.95× fewer | 1.8× |
| if_branch | 6,006 | 6,050 | 235 | **233** | 820 | 25.78× fewer | 0.28× |
| fib_call_toplevel | 184,321 | 158,662 | 459 | **465** | 629 | 396.39× fewer | 0.74× |
| fib_call_in_loop | 14,766 | 14,760 | 459 | **465** | 512 | 31.75× fewer | 0.91× |

| Whole process, median of 5 (min) | v4.12 | legacy | VM | CPython | peak RSS KiB v4.12 / legacy / VM / CPython |
| --- | ---: | ---: | ---: | ---: | --- |
| fib22 | 1372.3 ms (1315.2) | 1194.7 ms (1147.1) | 7.2 ms (6.7) | 18.3 ms (17.4) | 7,352 / 7,856 / 5,364 / 8,628 |
| fib25 | 5833.8 ms (5725.4) | 5252.9 ms (5056.3) | 16.2 ms (15.9) | 24.8 ms (24.3) | 7,492 / 7,896 / 5,596 / 8,628 |
| loop1m | 507.2 ms (503.2) | 527.1 ms (488.9) | 31.7 ms (27.2) | 66.9 ms (65.3) | 6,736 / 7,280 / 5,616 / 8,628 |
| calls200k | 344.8 ms (343.7) | 337.3 ms (319.2) | 12.4 ms (11.9) | 31.9 ms (31.1) | 6,880 / 7,440 / 5,620 / 8,624 |
| startup | 5.0 ms (4.8) | 4.6 ms (4.5) | 4.8 ms (3.8) | 16.7 ms (15.2) | 6,624 / 7,140 / 5,248 / 8,628 |

| Program | bytes | parse instr | compile instr | execute instr | VM process instr | legacy translate instr | parse / compile / execute µs | cold process ms | in-process compile / cold run / warm run µs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | --- |
| fib22 | 84 | 21,804 | 96,931 | 26,622,154 | 29,234,607 | 15,401 | 27.8 / 51.2 / 2959.9 | 6.77 | 67.0 / 3011.9 / 2906.2 |
| fib25 | 84 | 21,804 | 96,931 | — | — | — | 25.5 / 54.9 / 12175.7 | 16.13 | — |
| loop1m | 61 | 20,159 | 79,983 | 184,000,737 | 186,593,062 | 11,290 | 28.7 / 51.4 / 22862 | 26.79 | 120.7 / 25185.4 / 22996.3 |
| calls200k | 98 | 28,557 | 100,765 | 93,400,731 | 96,024,077 | 17,325 | 29.8 / 57.6 / 8304.5 | 12.23 | 80.0 / 8245.4 / 8178.1 |
| startup | 16 | 5,531 | 72,524 | 383 | 2,568,706 | 4,197 | 20.5 / 42.6 / 6.7 | 3.52 | 47.0 / 9.6 / 0.2 |
| large_500_functions | 66,296 | 15,953,728 | 20,836,549 | 1,862 | 42,185,514 | 8,122,025 | 1757.8 / 2430.7 / 19.8 | 7.72 | 4398.4 / 9.0 / 1.0 |
