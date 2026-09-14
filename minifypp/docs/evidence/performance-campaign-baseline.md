# JavaScript performance campaign baseline

Baseline production lineage: `9ba9b4f` plus the allocation-only syntax reserve checkpoint `4c694d4`.

The external 12-fixture benchmark is the acceptance oracle. A performance-sensitive checkpoint is accepted only when it preserves every real-bundle post-validation result and is measured against same-run controls (OXC, SWC, esbuild and Terser) when the external benchmark is available.

## Current competitive position

The same-run benchmark retained with this workspace shows the intended product ladder:

- conservative/default is the latency-sensitive path and is competitive with the fastest native competitors on large bundles;
- structured spends additional CPU for smaller output and is approximately SWC-class on several large fixtures;
- aggressive deliberately spends substantially more CPU for compression, but should remain materially faster than Terser on the large fixtures where both complete.

Absolute historical GitHub milliseconds are not a regression oracle: the exact `9ba9b4f` source measured materially differently across runner epochs. Future comparisons must use either old/new Minify++ binaries in the same job or same-run control minifiers.

## Acceptance rules

- Any new semantic or post-validation failure: reject.
- Aggregate throughput regression above 2%: inspect before accepting.
- Aggregate regression above 5%: reject unless accompanied by a deliberately approved and measured trade-off.
- Regression above 10% on a large representative fixture: reject.
- Default mode should remain effectively flat unless a compelling reason is documented.
- A claimed optimization should improve at least two representative large fixtures or demonstrate a clear scaling win, not merely a microbenchmark.
- Preserve raw samples and medians; do not certify from one timing sample.

## Campaign fixtures

The profiling harness is designed around React/Moment for small-to-medium behavior and D3, ECharts, AntD and TypeScript for large-bundle scaling. The external benchmark remains authoritative for final 12-fixture certification.
