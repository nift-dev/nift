# Benchmark-focused optimizer checkpoint 1

Date: 2026-09-12

This freezes the pre-campaign JavaScript baseline at Minify++ commit `12832d3`.
The upstream `minification-benchmarks` harness was run on the same host for
three process-inclusive samples per fixture and mode. Every emitted file was
retained by the harness under its ignored `results/` tree and passed the
harness's artifact validation.

| Artifact | Conservative bytes | Structured bytes | Aggressive bytes | Aggressive median ms |
|---|---:|---:|---:|---:|
| antd | 4,457,458 | 2,562,762 | 2,511,941 | 4,944.9 |
| d3 | 400,627 | 348,621 | 347,308 | 901.4 |
| echarts | 1,791,302 | 1,220,757 | 1,192,909 | 3,767.2 |
| jquery | 144,260 | 102,654 | 102,276 | 145.1 |
| lodash | 148,438 | 96,573 | 95,929 | 197.1 |
| moment | 97,951 | 77,012 | 76,927 | 48.9 |
| react | 40,659 | 30,062 | 29,659 | 32.0 |
| terser | 640,813 | 579,675 | 577,857 | 638.5 |
| three | 948,919 | 739,284 | 729,267 | 1,595.3 |
| typescript | 5,911,587 | 4,492,261 | 4,446,368 | 7,930.6 |
| victory | 1,448,095 | 828,105 | 812,279 | 1,892.6 |
| vue | 198,365 | 149,276 | 146,970 | 214.3 |

Raw size is deterministic across the three samples. Timing is evidence for
this host only. Subsequent checkpoints compare against these exact byte counts;
performance-only changes require a greater than three percent median gain.

