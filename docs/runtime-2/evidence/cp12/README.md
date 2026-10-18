# CP12 phase A evidence: engine selection before execution (D-018)

Branch `runtime-2-cp12-dev` at `aeefaa7` (rebased on `runtime-2` @`f3208f4`),
Linux x86-64, 2 vCPU, no other workload, g++ 13.3 `-O2`, CPython 3.13.16,
valgrind 3.22. Record: `../../checkpoints/cp12-engine-selection.md`;
running table: `../../scoreboard.md`.

| File | Content |
| --- | --- |
| `test-rt-vm.log` | `make test-rt-vm` at `aeefaa7`: PASS (unit, prepared-parser differential, lint, name table, VM corpus + 300 generated + 300 mutants, rt-diff on both engines with the CP11/CP12 policies, adversarial seed 1 at 600, subset suites, generated suites at 500, engine selection, former replay at 600/family, D-018, context consistency) |
| `sanitize.txt` | `make test-rt-vm-sanitize` summary (ASan+UBSan, leaks on): clean |
| `d018-old-vs-new.txt` | `tests/runtime2/vm_d018.py --show`: every approved D-018 change on legacy (old) and the VM (new), and the policy boundary cases |
| `vm_compare.jsonl` | `benchmarks/runtime2/vm_compare.py --runs 5 --slow --label CP12` at `aeefaa7` (groups ops, fib, kernels, phases with compile throughput, e2e) |
| `vm_compare-cp11.jsonl` | the same script on the CP11 binary (`bf32e4f`), same host, minutes later (`--label CP11-bf32e4f`, without `--slow`/`--vm-run`) |

Reproduce: `make test-rt-vm`, `make test-rt-vm-sanitize`,
`python3 tests/runtime2/vm_d018.py --nift ./nift --show`,
`python3 benchmarks/runtime2/vm_compare.py --nift ./nift --baseline ../baseline-src/nift --vm-run .build/rt/vm-run --runs 5 --slow --out vm_compare.jsonl`.
