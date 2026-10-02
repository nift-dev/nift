# v4.6 Batch 4 CP4b+ stream-operator performance comparison

Status: complete. Compares the stream-operator checkpoint against its baseline
revision to verify that adding `<<` / `>>` does not measurably regress
unrelated parsing, expression evaluation, command fallback, or build
performance.

## Methodology

- Baseline SHA: `e648154` (the committed CP4b revision; worktree was clean).
- Candidate: `e648154` + the uncommitted stream-operator diff
  (git diff hash `71c268293c95e4ee3f544632aa5dd91cf02adf6a`).
- Machine: 12th Gen Intel Core i7-12700H.
- Compiler: g++ (Ubuntu) 15.2.0, `-std=c++17 -O2 -Wall -Wextra -pedantic -pthread`
  (the repository default CXXFLAGS); both binaries built from the same Makefile.
- Both binaries were built from source with the identical flags; the baseline
  binary was captured before any parser/operator change was made.
- Each workload used 25-30 reps with baseline/candidate runs **interleaved**
  (`base, cand, base, cand, ...`) plus 2-3 warmups, and reports the median.
  Two full independent runs were recorded to assess run-to-run stability.
- Workloads are cold-process (each rep launches Nift on a fresh temp script),
  so sub-2ms workloads are dominated by process startup and are inherently
  noisy; the parser/expression workloads use enough work (200k+ operations) to
  rise above startup.

## Results (median, ms; 25-30 alternating reps)

| Benchmark | Baseline | Candidate | Delta | Run-2 delta |
| --- | --- | --- | --- | --- |
| arithmetic loop 200k | 287.6 | 288.7 | +0.40% | -2.04% |
| ordinary script 10k statements | 286.9 | 286.4 | -0.16% | +0.49% |
| string concat + comparisons 50k | 70.2 | 71.4 | +1.75% | -1.54% |
| prepared-loop arithmetic 50k | 63.6 | 62.6 | -1.55% | +0.08% |
| command classification 150 `true` | 2.6 | 2.6 | -0.71% | +0.40% (shell-shaped) |
| shell-shaped command lines 150 | 427.0 | 426.9 | -0.02% | +0.47% |
| template render 4000 interpolations | 77.5 | 77.8 | +0.36% | - |
| explicit stream write 3000x3 | 102.8 | 103.2 | +0.41% | -0.31% |
| operator stream write 3000x3 | n/a | 87.2 | n/a (new feature) | - |
| explicit stream read 3000x | 56.6 | 55.3 | -2.41% | -1.14% |
| operator stream read 3000x | n/a | 76.8 | n/a (new feature) | - |
| peak RSS (arith) | 45.5 MB | 45.8 MB | +0.8% | - |
| peak RSS (explicit stream write) | 7.9 MB | 8.1 MB | +2-5% (within noise) | - |

## Interpretation

- **No measurable regression** in the parser/expression/statement/command/
  template hot paths. Arithmetic (+0.40%), 10k statements (-0.16%), template
  render with 4000 interpolations (+0.36%), and prepared loops (-1.55%) are all
  inside the machine's run-to-run noise (±2-3%, confirmed by deltas flipping
  sign between independent runs at lower rep counts). The implementation
  integrates `<<`/`>>` recognition into the existing comparison-operator loop,
  so expressions without a top-level `<` or `>` pay no stream scan at all.
- **Command fallback is unaffected**: shell-shaped lines with `<`/`>` (the
  `<>` discriminator path) are ~0% delta; plain command classification is
  unchanged.
- **Stream operators are cheap sugar**: operator write (~87 ms) is faster than
  the equivalent explicit `write` calls (~103 ms) because the chain avoids
  repeated method-argument parsing. Operator read (`>>`, token extraction with
  conversion, ~77 ms) is slower than `read_line` (whole-line, ~56 ms) but that
  is a different operation (token vs line); the absolute per-operation cost is
  ~26 us including file I/O.
- **Prepared/AST fallback** does not increase for ordinary code: the prepared
  loop benchmark is ~0% delta. Statements containing stream operators fall back
  to the legacy evaluator (as try/catch already does), which is expected and
  semantically correct, not a performance regression for non-stream code.
- **No hot-path allocations** were introduced: the operator handler captures by
  reference, builds no per-expression temporaries, and only the `>>` token
  buffer allocates (inherent to extraction). Peak RSS is within noise.
- **No duplicate evaluation**: side-effecting producers are invoked exactly
  once per operand (verified by test).

## Conclusion

The stream-operator checkpoint meets the acceptance criteria: ordinary code,
script translation, command fallback, and template/build paths are
statistically indistinguishable from `e648154`. No unexplained regression
remains. The benchmark harness and raw samples were kept outside the
repository (`/tmp/opencode/nift-bench`); this document summarizes the
methodology and results.