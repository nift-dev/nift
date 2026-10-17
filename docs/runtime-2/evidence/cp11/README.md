# CP11 evidence: VM proof of concept

Branch `runtime-2-cp11` (CP09 + CP10 + CP11), Linux x86-64, 2 vCPU Xeon
2.1 GHz (shared with two concurrent reviews; load average 2.6–3.8 during the
runs, so wall times are noisy and are not gated), g++ 13.3 `-O2`, CPython
3.13.16, valgrind 3.22. Record: `../../checkpoints/cp11-vm-poc.md`.

## Files

| File | Content |
| --- | --- |
| `vm_compare.jsonl` | `benchmarks/runtime2/vm_compare.py --runs 5 --slow` at `25089e8`: host fingerprint, then `ops` (callgrind marginal instructions, N=4,000 vs 8,000), `fib` (marginal per recursive call), `kernels` (wall median/min of 5, peak RSS), `phases` (callgrind instructions inside `rt::parse_script`, `rt::compile_unit`, `rt::Vm::run`, `rt::run_script_on_vm`; legacy `Parser::translate_function_program`; NIFT_VM_STATS wall µs; in-process cold vs warm runs) |
| `vm_compare-tables.md` | the same data as tables |
| `parity-seed3.log` | `tests/runtime2/vm_parity.py --gen 600 --seed 3 --rt-diff` (see note below) |
| `rt-diff-vm-outcomes.json` | NIFT_VM=1 outcome (vm / fallback / replayed, with reason) of every rt-diff script entry in that run |
| `profile-call.txt` | `callgrind_annotate` of the flat-call workload (N=8,000) on the VM |

## Reproduce

```
make nift .build/rt/vm-run
python3 benchmarks/runtime2/vm_compare.py --nift ./nift --baseline ../baseline-src/nift \
    --vm-run .build/rt/vm-run --runs 5 --slow --out vm_compare.jsonl     # ~25 min
make test-rt-vm                    # unit + corpus + 300 generated (+300 mutants) + rt-diff
python3 tests/runtime2/vm_parity.py --nift ./nift --gen 600 --seed 3 --rt-diff
```

## Note on `parity-seed3.log`

It reports one failure: mutant seed 3 index 313 is an endless loop (the
mutation turned `c5 += 1` into `c5 -= 1`). Legacy hit the 30 s timeout; the
VM, being faster, hit the harness's 256 MiB output-file limit first (SIGXFSZ).
Neither run terminates, so their outputs are not comparable. The harness now
classifies "both runs hit a time/output limit" as `nonterminating` (counted,
not compared); every other entry in the run is identical.
