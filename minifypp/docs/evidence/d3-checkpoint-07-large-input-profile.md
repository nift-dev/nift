# D3 checkpoint 7: large-input execution profile

Date: 2026-09-13

The native benchmark driver was timed outside the upstream ten-second timeout.
An instrumentation-only build counted recursive full-source optimizer entries.

| Fixture/mode | Wall time | Output bytes | Full optimizer entries |
| --- | ---: | ---: | ---: |
| Terser structured | 0.203 s | 511,252 | 2 |
| Terser aggressive | 0.921 s | 507,016 | 10 |
| TypeScript structured | 5.761 s | 3,722,871 | 2 |
| TypeScript aggressive | 12.575 s | 3,647,652 | 11 |
| ECharts structured | 11.709 s | 1,096,022 | 2 |
| ECharts aggressive | 15.033 s | 1,059,417 | 10 |

The earlier benchmark interpretation is corrected here: Terser aggressive did
not time out. ECharts timed out in both structured and aggressive modes, while
TypeScript timed out only in aggressive mode.

Aggressive mode performs nine or ten additional complete lex/scope/resolve
cycles for relatively small late-stage reductions. However, ECharts structured
already exceeds the timeout with only two entries, demonstrating a separate
single-pass complexity problem in binding allocation. Checkpoint 8 therefore
targets allocator structure before imposing a convergence budget.

Instrumentation was confined to a disposable build and is not part of the
product source.
