# CP13B exception fixtures (Phase A R2, implemented Phase B)

Test-only fixtures and harness for `try`/`catch`/`throw`/`error()` on the
Runtime 2.0 VM. Phase B implementation is present; the suite is still
evidence rather than part of `make test`.

```sh
python3 tests/runtime2/cp13b/run.py --nift ./nift --mode legacy
python3 tests/runtime2/cp13b/run.py --nift ./nift --mode current
python3 tests/runtime2/cp13b/run.py --nift ./nift --mode future
python3 tests/runtime2/cp13b/run.py --nift ./nift --selftest
python3 tests/runtime2/cp13b/run.py --nift ./nift --trace-selftest
python3 tests/runtime2/cp13b/run.py --nift ./nift --oracle ./nift --mutant N
python3 tests/runtime2/cp13b/run.py --nift ./nift --oracle ./nift --checked-mutant N
python3 tests/runtime2/cp13b/run.py --nift ./nift --oracle ./nift --asan-mutant N
bash   tests/runtime2/cp13b/harness_selftest.sh ./nift
python3 tests/runtime2/cp13b/nullable_error_presentation.py --nift ./nift --switch-nift ./.build/nift-switch
python3 tests/runtime2/cp13b/stress/stress.py --nift ./nift --prepare
bash   tests/runtime2/cp13b/stress/run_error_lifecycle.sh
python3 tests/runtime2/cp13b/mutant_build.py --root . --dry-run
```

- `cases/` and `expected/`: legacy oracle goldens regenerated from the legacy
  engine on this base. `name.m.nft` is copied to `m.nft`;
  `name.init.nft`/`name.lib.nft` support multi-module fixtures.
- `MANIFEST.tsv`: the `NIFT_VM=require` expectation per case after Phase B;
  `@vm` marks a fixture that executes on the VM (checked against the legacy
  golden), otherwise the exact whole-unit refusal reason remains.
- `EXPECT_FUTURE.tsv`: outcome (`vm`, `vm-error`, `fallback:<exact reason>`,
  `limit`), approved policy ids (`D-001#4` -> `D-001#4 recursion`, `D-018c` ->
  `D-018c diagnostics`), VM-only environment, and checks (`uncatchable`,
  `cp16`). Nothing else is an approved difference.
- `MUTANTS.json`: E1–E8, required cases and why each detects, controls,
  checked-build cases, ASan-only cases, `requires_cp16` and fallback reasons.
- `stress/`: two 1M-catch workloads, the accounted-heap/RSS runner and the
  Error ownership unit with its leak probe.
- `adversarial/`: end-to-end fake-binary harness tests (B1/B2/B5) and the
  isolated mutant-build test (B6).
- `mutant_build.py`: clean-worktree mutant build; a non-zero build exit or a
  missing/uncertain binary fails the gate.

`current` requires a refusal before execution with no stdout, no Host trace
(fd-based `NIFT_TRACE_HOST`) and no file side effects. `future` requires
genuine VM execution for `vm`/`vm-error`; fallback reasons are exact;
`limit` proves an uncatchable memory-limit stop. `--mutant` requires
`--oracle`; the unmutated oracle must PASS future mode first, and a case can
detect only after genuine VM execution. Fallbacks, refusals, signals,
sanitizer reports, exit 70/unexpected exits, timeouts, missing/malformed
statistics and oracle mismatches are harness failures, never detections.
Controls must still pass under the mutant. E1–E8 and the E2 checked-build and
E7 ASan controls are validated by Phase B. `cp16` fixtures exercise the
integrated CP16/N1 Host codes.
