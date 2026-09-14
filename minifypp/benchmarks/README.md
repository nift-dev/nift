# Minify++ benchmark notes

`make benchmark` remains the canonical in-process regression benchmark for the
Minify++ C++ API. It is intentionally not a competitor ranking.

## Same-host CSS comparison

`run_css_competitive.py` compares complete CLI invocations on exactly the same CSS
files. Example:

```bash
python3 benchmarks/run_css_competitive.py \
  --minifypp ./minify \
  --esbuild /path/to/esbuild \
  --lightningcss /path/to/lightningcss \
  --warmups 5 --iterations 45 \
  --json benchmarks/results/css-competitive.json \
  --csv benchmarks/results/css-competitive.csv \
  bootstrap-4.css animate.css tailwind.css
```

The JSON is authoritative: it records every timing sample, median, quartiles,
input/output/gzip bytes, commands, versions, host, build flags and fixture
provenance. CSV is a derived summary. Do not mix these process-inclusive medians
with in-process API timing.

Retained checkpoint CSV/text evidence lives under `benchmarks/results/`; public fixture files themselves are not vendored.

The public comparison checkpoint uses the GoalSmashers CSS benchmark copies of
Bootstrap 4, Animate.css 4.1.1 and Tailwind CSS because Lightning CSS publishes
esbuild/Lightning output sizes for those exact fixture sizes. The retained
Minify++ measurements were made independently on the checkpoint host; competitor
published times are context only and are not treated as same-host measurements.

## privatenumber JavaScript benchmark integration

`privatenumber_nift_adapter.ts` is a reference adapter for
`privatenumber/minification-benchmarks`. That benchmark gives each minifier an
adapter that accepts source text and returns minified source. Nift's public CLI is
file-oriented, so the adapter uses an isolated temporary `.js` file and invokes:

```bash
nift minify input.js
```

Set `NIFT_BIN=/absolute/path/to/nift` when running the upstream benchmark. The
adapter is intentionally kept here rather than vendoring/forking the upstream
suite. Any published Nift result should come from an actual run of the upstream
suite on its exact current artifacts and should record the upstream commit.

The 2026-08-18 upstream run at commit
`fe89864fdf0e9e8f178d5cfa704f4b032986aade` used Nift 4.0.2 commit `aa60ab3`
and five runs for each of 12 artifacts. Nift passed every integrity check but
ranked last among successful tools under the upstream size-heavy score on every
artifact. Ant Design and TypeScript also exposed large-input latency of 2.110
and 2.368 seconds respectively.

`results/2026-08-18-js-privatenumber.json` retains current-run results and
failures. JShrink could not run without PHP/Composer, so stale upstream JShrink
data is excluded. Closure Compiler failed without Java and remains recorded. A
D3 probe reconfirmed byte-identical standalone Minify++ and Nift output.
