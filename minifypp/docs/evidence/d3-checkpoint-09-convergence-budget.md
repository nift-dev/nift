# D3 checkpoint 9: bounded aggressive convergence

Date: 2026-09-13

Aggressive optimization previously allowed every successful rewrite batch to
start another unrestricted full-source scan. The large-input profile recorded
ten or eleven complete optimizer entries.

Aggressive rewriting is now bounded to seven successful rounds. Binding
renaming retains its existing convergence behavior and does not consume this
budget. The bound is a performance guardrail only: every accepted rewrite is
still individually fail-closed and size-decreasing.

| Fixture | Unbounded time | Bounded time | Change | Output bytes |
| --- | ---: | ---: | ---: | ---: |
| TypeScript aggressive | 14.472 s | 8.309 s | -42.6% | 3,647,652 |
| ECharts aggressive | 3.622 s | 3.292 s | -9.1% | 1,059,417 |

Both bounded outputs are byte-identical to the unbounded checkpoint-8 outputs.
Budgets of five and six rounds were also measured, but were rejected because
they left 24/136 and 4/24 bytes respectively on TypeScript/ECharts.
