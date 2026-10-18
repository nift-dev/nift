# CP11 evidence: VM proof of concept (after review round 2)

Branch `runtime-2-cp11i` (`runtime-2` @`4287adb` with CP09 + CP10 certified,
plus CP11 and its review-round-1 fixes), Linux x86-64, 2 vCPU Xeon 2.1 GHz,
otherwise idle during the measurements, g++ 13.3 `-O2`, CPython 3.13.16,
valgrind 3.22. Record: `../../checkpoints/cp11-vm-poc.md`. The pre-review
evidence (commit `25089e8`, loaded host) is in git history
(`git show 0a0d43f:docs/runtime-2/evidence/cp11/`).

Round 2 (this refresh): branch `runtime-2-cp11i` at `b2183e4` (+ record
commit), measured on a **shared** host (other sessions' builds; load
average 1.0–3.5): instruction counts are deterministic, wall times and
suite durations are indicative only. Files below are round 2's unless
marked; round 1's versions are in git history (`git show 594f44b:docs/runtime-2/evidence/cp11/`).

## Files

| File | Content |
| --- | --- |
| `vm_compare.jsonl` | `benchmarks/runtime2/vm_compare.py --runs 5 --slow` at `b2183e4` (round 2; round 1 at `84d9332`): host fingerprint and load, then `ops` (callgrind marginal instructions, N=4,000 vs 8,000), `fib` (marginal per recursive call), `kernels` (wall median/min of 5, peak RSS), `phases` (callgrind instructions inside `rt::parse_script`, `rt::compile_unit`, `rt::Vm::run`, `rt::run_script_on_vm`; legacy `Parser::translate_function_program`; NIFT_VM_STATS wall µs; in-process cold vs warm runs) |
| `vm_compare-tables.md` | round 1's tables (the round-2 tables are in the record and `benchmarks.md`) |
| `profile-call.txt` | round 1: `callgrind_annotate` of the flat-call workload (N=8,000 and 4,000) at `84d9332` |
| `test-rt-vm.log` | `make test-rt-vm` (unit, prepared-parser differential, lint, name-table check, VM corpus, 300 generated + 300 mutants, rt-diff, adversarial gate seed 1 with 18 families, subset suites): PASS in 4 min 20 s (shared host) |
| `sanitize.txt` | `make test-rt-vm-sanitize` (ASan+UBSan, `-fno-sanitize-recover=all`, leak detection on, relocate-on-mutation reference check): unit tests, prepared-parser differential, corpus + 150 generated + 150 mutants, adversarial gate seed 2 sampled to ≤ 250 programs per family (3,099), on `.build/nift-sanitize`: clean, 34 min (shared host) |
| `rt-diff-vm-outcomes.json` | NIFT_VM=1 outcome of every rt-diff script entry after round 2 (777 scripts: 757 shared + 9 round-1 + 11 round-2 regression scripts) |
| `rt-diff-vm-outcomes-round1.json` | the same after round 1 (766 scripts) |
| `rt-diff-vm-outcomes-integrated-before-fixes.json` | the same for the integrated build before the review fixes (`38fd2df`, 757 scripts) |
| `reviewer-material-round2.txt` | round 2: the nine repros' outcomes, the round-2 generators, and round 1's material re-run on the round-2 build |
| `reviewer-material.txt` | round 1: | the reviewer's kept divergent programs (`fy1/`, `fz4/`) and case files `c1`–`c12` on the fixed build, and the reviewer's `fuzz2`/`fuzz3` generators (1,000 programs each) before and after the fixes |

## Reproduce

```
make nift .build/rt/vm-run
python3 benchmarks/runtime2/vm_compare.py --nift ./nift --baseline ../baseline-src/nift \
    --vm-run .build/rt/vm-run --runs 5 --slow --out vm_compare.jsonl     # ~6 min
make test-rt-vm                      # ~3-5 min on a 2-vCPU host
make test-rt-vm-sanitize             # ~20-35 min including the sanitized build
python3 tests/runtime2/vm_parity.py --nift ./nift --gen 0 --rt-diff --outcomes rt-diff-vm-outcomes.json
python3 tests/runtime2/vm_adversarial.py --nift ./nift --seed 1 --count 600 --report adversarial.json
```

`vm_compare.py`'s generated 500-function program now names its functions
`g<k>` (was `f<k>`): `f32`/`f64` are FFI type names in the generated legacy
name table, so `f32` would make the whole program fall back.
