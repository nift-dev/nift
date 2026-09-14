# D3 checkpoint 10: integration and timeout certification

Date: 2026-09-13

The benchmark-native adapter completed every mode of the four large diagnostic
fixtures under the upstream ten-second per-run limit on this host:

| Fixture | Conservative | Structured | Aggressive |
| --- | ---: | ---: | ---: |
| D3 | 0.030 s / 400,431 B | 0.239 s / 295,969 B | 1.144 s / 292,532 B |
| Terser | 0.044 s / 640,812 B | 0.129 s / 511,252 B | 0.814 s / 507,016 B |
| TypeScript | 0.296 s / 5,912,392 B | 1.363 s / 3,722,871 B | 8.266 s / 3,647,652 B |
| ECharts | 0.103 s / 1,792,146 B | 0.625 s / 1,096,022 B | 3.296 s / 1,059,417 B |

The complete `make test` gate passed: 15,459 generated JavaScript programs,
180 generated JSX programs, scope/module/structured/aggressive semantic suites,
17 PostCSS fixtures, 115 other-format documents, 70,000 fuzz cases, the CLI,
and 36 real-bundle syntax and semantic outputs.

The independent conformance harness then processed all 48,011 selected tests
from Test262 revision `419d3e0a2273ba01a3bfcbec423f2801425b8e93` against
Minify++ commit `72d305f03b14e8b866acc0cb3c734f11758a6978`:

| Classification | Count |
| --- | ---: |
| Pass | 39,744 |
| Runtime-inapplicable | 8,267 |
| Minifier error | 0 |
| Minified timeout | 0 |
| Semantic failure | 0 |

This completes D3 checkpoints 1–10 with zero transformed conformance failures.
