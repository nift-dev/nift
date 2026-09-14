# D3 checkpoint 8: indexed name allocation

Date: 2026-09-13

`gprof` attributed 39.8% of ECharts structured execution to
`plan_safe_js_parameter_renaming`, including 3,902,333 interference checks and
4,376,255 short-name generations in one run.

The allocator now caches each binding's live range once per source, caches
scope depth, and indexes prior allocations by generated-name index. It therefore checks a
candidate only against bindings which actually received that spelling instead
of scanning every prior allocation.

Three warm ECharts structured runs measured:

| Revision | Times (seconds) | Median |
| --- | --- | ---: |
| checkpoint 7 | 11.506, 11.713 | 11.610 |
| checkpoint 8 (initial map prototype) | 2.312, 2.275, 2.170 | 2.275 |

The final allocation-free candidate index reduced ECharts further to 0.630 s
and TypeScript structured from 5.761 s to 1.513 s. These improvements of 94.6%
and 73.7% are far above the 3% retention threshold.
Both revisions emitted 1,096,022 bytes with SHA-256
`1a559008c70a5c38ae1870c0c24acf87673f036a9662edf71e0e07302e0fded7`.
