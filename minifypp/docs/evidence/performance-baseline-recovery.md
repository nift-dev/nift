# Performance baseline recovery

Date: 2026-09-14

## Same-revision benchmark evidence

The external `privatenumber/minification-benchmarks` history now contains two
runs of the exact same Minify++ production revision, `9ba9b4f`. The newer run
remains 12/12 valid in conservative, structured and aggressive modes, but its
reported timings are slower than the earlier run by roughly 24% conservative,
15% structured and 15% aggressive on average. Because the source revision is
identical, those historical snapshots cannot by themselves distinguish runner
drift from a product regression.

The benchmark harness must therefore refresh a small same-run control cohort
whenever Minify++ is refreshed. The selected controls are OXC, SWC, esbuild and
Terser. Future Minify++ performance decisions should compare both absolute
Minify++ timings and movement relative to those same-run controls.

## Local profile

A disposable `-O2 -pg` build of `benchmarks/minify_benchmark.cpp` was run with a
large repeated JavaScript workload. This is a diagnostic profile, not a
replacement for the 12-artifact external benchmark.

The largest named JavaScript costs were:

- concrete syntax construction;
- safe binding/parameter renaming;
- scope graph construction;
- reference resolution;
- semantic-fact construction.

The profile also showed very high vector-growth activity while constructing
syntax data. `build_js_concrete_syntax` creates at most one syntax node per
input token plus the root, so the outer node and child-vector storage can be
reserved exactly enough up front without changing semantics.

## First retained optimization

Reserve `tokens.size() + 1` entries for `JsConcreteSyntax::nodes` and
`JsConcreteSyntax::children` before construction. Alternating local synthetic
runs showed approximately 2% improvement in the structured scope workload and
6% in the aggressive workload; conservative JavaScript was within noisy
single-digit variation. The smoke suite remains green.

This optimization is provisional until the external benchmark confirms no
validation, size or throughput regression on the 12 real artifacts.
