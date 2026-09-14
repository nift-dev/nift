# Minify++ testing handover

## Current layers

The Makefile's `test` target runs:

- focused C++ format smoke tests;
- Node-based semantic comparison;
- deterministic generated JavaScript semantic programs;
- generated JSX/TSX syntax and idempotence cases;
- independent PostCSS CSS semantic-tree comparison when PostCSS is available;
- non-JavaScript format idempotence;
- cross-format adversarial/malformed cases;
- CLI behavior.

Retained checkpoint counts are evidence tied to current scripts, not a target.
Quality is the diversity of semantic assumptions attacked.

## Oracle selection

- Exact/golden output: useful for deliberately stable lexical transformation.
- Runtime equivalence: preferred for executable JavaScript when practical.
- Structural parse comparison: strong for JSON.
- Syntax/idempotence: useful discovery/protection for JSX and conservative
  non-JavaScript formats, but not complete semantic proof.
- Real-world corpus: catches interactions absent from generated matrices.
- Differential tools: discovery aid; another minifier's spelling is not contract.

Do not assume `minify(minify(x)) == minify(x)` for every format unless current
contract intentionally guarantees it. Existing idempotence gates protect the
formats/cases they actually exercise.

## Adversarial matrix

### Cross-cutting

Empty/whitespace input, Unicode, mixed newlines, long tokens, deep nesting,
unterminated constructs, malformed input, comments, escapes, and boundary bytes.

### HTML

Inline text whitespace, block/inline boundaries, `pre`/`textarea`, raw
`script`/`style`, comments, entities, attribute quotes, malformed tags, and
embedded language boundaries.

### CSS

Selector/token joining, strings, URLs, comments, functions, custom properties,
numbers/units, `calc()`, modern syntax, and malformed blocks.

### JavaScript

ASI after `return`/`throw`/`break`/`continue`, postfix increments, expression
statement boundaries, operator joining, regex/division/comments, templates and
nested `${...}`, async/generator/class/object/destructuring contexts, modules,
optional chaining, numeric/Unicode syntax, and side-effect evaluation.

### JSX/TSX

Nested elements/fragments, expressions, comments/strings/regex containing angle
characters, generic arrow ambiguities, type/assertion/satisfies contexts, and
malformed nesting.

### JSON/XML/SVG

JSON structural equivalence and malformed rejection. XML/SVG lexical safety,
CDATA/entities/attributes/comments, while retaining conservative non-validator
claims.

## Semantic generation and corpus growth

Generated matrices should vary independent context dimensions rather than create
thousands of cosmetic duplicates. Batch execution where it preserves case
isolation. Any real-world failure should be minimized into a focused permanent
case and retained in the corpus where representative.

## Native safety and fuzzing

Scanner/indexing changes are strong ASan/UBSan candidates. Fuzzing should mutate
valid seeds and malformed inputs, detect crashes/hangs/unbounded memory, minimize
findings, and convert them into deterministic regressions. Runtime differential
fuzzing is especially valuable for supported JavaScript.

`make test-fuzz` deterministically mutates valid seeds for all seven formats and
requires every successful first-pass output to remain accepted on a second pass.
Its default 70,000 cases found three token-manufacturing bugs during introduction;
each was minimized into `minify_smoke.cpp`. `make test-sanitize` runs the direct
smoke, fuzz-smoke, and CLI transaction suite under ASan/UBSan. On desktop hosts
where LeakSanitizer is incompatible with process supervision, set
`ASAN_OPTIONS=detect_leaks=0` and report that limitation explicitly.

The reopened audit also used `FUZZ_CASES=1000000`, producing 7,000,000 mutations
and finding JavaScript comment-delimiter synthesis plus JSX roots incorrectly
located inside JavaScript comments. The ASan/UBSan campaign also passed all
7,000,000 cases. Use large campaigns as periodic evidence; keep the fast default suitable for
ordinary development.

`make test-css-semantics` compiles a CSS-only driver and, when PostCSS plus its
selector/value parsers are installed, compares normalized semantic trees for
original and minified representative modern stylesheets. It is deliberately
independent of Minify++ and complements rather than replaces focused exact tests.

The 115-document non-JavaScript corpus includes a Node-backed JSON structural
oracle. Native minification is orchestrated by the shell and Node reads completed
files, avoiding nested child-process assumptions in desktop/CI wrappers. The CSS
semantic oracle uses the same arrangement. Do not reintroduce Node `spawnSync`
without validating it in the supported process-supervision environments.

## Embedded synchronization

Run `make check-nift-sync NIFT_MINIFYPP_DIR=/path/to/nift/minifypp` whenever the
canonical standalone implementation, public surface, tests, or build contract
changes. It compares the explicit mirrored file set and fails on missing or
different files. Nift-specific integration behavior still requires Nift's own
tests; byte equality is necessary but not sufficient.

## CLI file transactions

The CLI smoke layer protects valid empty reads, a Linux unreadable-regular-file
probe where `/proc/kcore` is available, in-place permission preservation,
malformed-input preservation, symbolic-link refusal, and non-regular destination
rejection. Keep transformation tests separate: these cases protect file commit
semantics rather than scanner output.

## Performance

Measure startup, many small files, large files, per-format throughput, peak RSS,
and output size using semantically green candidates. Record machine, compiler
flags, corpus, iterations, and tool versions. Report tradeoffs honestly.

`make benchmark` builds deterministic large inputs for all seven formats and
reports input/output bytes, median time, throughput, and Linux peak RSS where
`/usr/bin/time` is available. It is a regression checkpoint, not a competitor
comparison. Retain exact CSV output with the host/compiler instead of treating an
old throughput range as a promise.

For real CSS fixtures, `benchmarks/run_css_fixture_benchmark.sh <css-file>...` runs the
C++ API with warmups and median sampling. `benchmarks/run_css_competitive.py` is a
separate same-host CLI harness for Minify++, esbuild and Lightning CSS. Never mix
its process-inclusive wall-clock results with another project's in-process timing
and call that a speed ratio. `benchmarks/privatenumber_nift_adapter.ts` records the
intended adapter for testing Nift in `privatenumber/minification-benchmarks`; only
publish a Nift rank after the actual upstream suite runs against its exact current
artifacts.

The 2026-08-18 comparison checkpoint used the GoalSmashers Bootstrap 4,
Animate.css 4.1.1 and Tailwind fixtures. Minify++ produced 165,836 / 77,454 /
1,973,801 bytes respectively. In-process medians on Linux 6.18.35 x86-64,
g++ 14.2.0 `-O2`, were 1.156 / 0.517 / 12.843 ms over 45 measured iterations after
five warmups. Lightning CSS's published benchmark provides esbuild/Lightning CSS
output-size context for those fixture names; its timings are not same-host
measurements and must not be combined with these local medians as a speed ranking.

The definitive competitive evidence is machine-readable rather than copied from
terminal summaries. Preserve raw timing samples before deriving medians/charts,
retain upstream failures and timeouts, and test public tables against their JSON
source. The 2026-08-18 same-host CSS result and upstream Nift JavaScript snapshot
are retained under `benchmarks/results/`.
