# CP11 evidence: VM proof of concept (after review round 1)

Branch `runtime-2-cp11i` (`runtime-2` @`4287adb` with CP09 + CP10 certified,
plus CP11 and its review-round-1 fixes), Linux x86-64, 2 vCPU Xeon 2.1 GHz,
otherwise idle during the measurements, g++ 13.3 `-O2`, CPython 3.13.16,
valgrind 3.22. Record: `../../checkpoints/cp11-vm-poc.md`. The pre-review
evidence (commit `25089e8`, loaded host) is in git history
(`git show 0a0d43f:docs/runtime-2/evidence/cp11/`).

## Files

| File | Content |
| --- | --- |
| `vm_compare.jsonl` | `benchmarks/runtime2/vm_compare.py --runs 5 --slow` at `84d9332`: host fingerprint and load, then `ops` (callgrind marginal instructions, N=4,000 vs 8,000), `fib` (marginal per recursive call), `kernels` (wall median/min of 5, peak RSS), `phases` (callgrind instructions inside `rt::parse_script`, `rt::compile_unit`, `rt::Vm::run`, `rt::run_script_on_vm`; legacy `Parser::translate_function_program`; NIFT_VM_STATS wall µs; in-process cold vs warm runs) |
| `vm_compare-tables.md` | the same data as tables, with the pre-review VM column |
| `profile-call.txt` | `callgrind_annotate` of the flat-call workload (N=8,000 and 4,000) at `84d9332` |
| `test-rt-vm.log` | `make test-rt-vm` at `84d9332` (unit, lint, name-table check, VM corpus, 300 generated + 300 mutants, rt-diff, adversarial gate seed 1): PASS in 2 min 46 s |
| `sanitize.txt` | `make test-rt-vm-sanitize` (ASan+UBSan, `-fno-sanitize-recover=all`, leak detection on): unit tests, corpus + 150 generated + 150 mutants, adversarial gate seed 2 sampled to ≤ 250 programs per family, on `.build/nift-sanitize` |
| `rt-diff-vm-outcomes.json` | NIFT_VM=1 outcome of every rt-diff script entry after the fixes (766 scripts: the 757 shared ones + 9 review regression scripts) |
| `rt-diff-vm-outcomes-integrated-before-fixes.json` | the same for the integrated build before the review fixes (`38fd2df`, 757 scripts) |
| `reviewer-material.txt` | the reviewer's kept divergent programs (`fy1/`, `fz4/`) and case files `c1`–`c12` on the fixed build, and the reviewer's `fuzz2`/`fuzz3` generators (1,000 programs each) before and after the fixes |

## Reproduce

```
make nift .build/rt/vm-run
python3 benchmarks/runtime2/vm_compare.py --nift ./nift --baseline ../baseline-src/nift \
    --vm-run .build/rt/vm-run --runs 5 --slow --out vm_compare.jsonl     # ~6 min
make test-rt-vm                      # ~3 min on an idle 2-vCPU host
make test-rt-vm-sanitize             # ~20 min including the sanitized build
python3 tests/runtime2/vm_parity.py --nift ./nift --gen 0 --rt-diff --outcomes rt-diff-vm-outcomes.json
python3 tests/runtime2/vm_adversarial.py --nift ./nift --seed 1 --count 600 --report adversarial.json
```

`vm_compare.py`'s generated 500-function program now names its functions
`g<k>` (was `f<k>`): `f32`/`f64` are FFI type names in the generated legacy
name table, so `f32` would make the whole program fall back.
