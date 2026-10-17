# CP05 evidence (2026-10-17, Linux x86_64, root sandbox)

Reference: runtime-2 `a3e15b5`. `ops.py BIN loop call call_top fib_top`
(marginal callgrind instructions per operation).

| Operation | Reference | CP05 | Change |
| --- | ---: | ---: | ---: |
| Flat call in a loop (`call`, incl. loop scaffold) — gate ≥10% | 15,048 | 13,397 | −11.0% |
| Flat call, loop scaffold subtracted | 11,922 | 10,295 | −13.6% |
| Flat call at top level (legacy path) | 155,136 | 139,367 | −10.2% |
| fib call at top level | 184,409 | 158,069 | −14.3% |
| Loop iteration (unchanged path) | 3,126 | 3,102 | −0.8% |

300-page site (callgrind): full build 357,147,091 → 357,151,179 (+0.001%),
no-op 114,807,695 → 114,807,908 (+0.0002%). Gate ≤ +1%.

## Equivalence
- `tools/rt-diff` (CP08 harness, run from the `runtime-2-cp08` worktree) with
  A = reference, B = CP05, over vm-corpus, semantics probes, cp410 probes,
  conformance cases, binding apps, 262 extracted smoke scripts and 1,200
  generated programs (seed 5), `--no-trace` (neither binary has the trace
  hook): 1,695 entries (1,683 scripts, 12 sites), **0 differing**.
- `make -k -j4 test`: environmental set, plus two load-induced failures that
  pass when rerun alone: the lambda-cache guard object (compiler killed by the
  OOM killer while other jobs ran; `test-v48-callback-guard` and
  `test-v410-callback-overlay-guard` pass alone) and
  `test-v44-language-foundation` (wall-clock scaling guard; the reference
  binary fails the same guard intermittently under the same load, and
  callgrind shows set-add scaling at 2.00× for 2× work for both binaries:
  ref 400.1M/798.3M, CP05 399.1M/796.1M).
- `tests/runtime2/*.sh`: all pass (`scanner_parity.sh` with the reference
  binary as `NIFT_REF`: 72 sites, 165 scripts).
- Regression suite (`nift-regression-suite` `runtime-2`, `9ae99c9`): 92/94;
  failures only `unreadable_source` (root) and `v44_executable_script` (no
  `nift` on PATH).
- `scripts/test_integrity_check.py tests scripts`: 0 findings.

## Certification after review fixes (rebased on 262d3ec)
- rt-diff (CP08 harness snapshot 2fae2d2, traces off: reference has no hook):
  runtime-2 vs CP05, committed corpora + extracted smoke + 2,000 generated
  (seed 505): 2,417 entries, 0 differing.
- `make test-all`: known environmental set (root, /usr/bin/time, Go toolchain
  download blocked, no dotnet) plus `test-v44-language-foundation`
  (v44_perf_scaling_smoke wall-time ratio under parallel load; passes 3/3 alone
  with both the reference and CP05).
- `tests/runtime2/*.sh`: all pass. Regression suite: 93/94 (unreadable_source:
  root).
- Memory reproducers (review #1): 11.4 MB, equal to the reference.

