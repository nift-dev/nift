# CP37 — fresh worst-performer re-profile

A fresh single-run triage on the current CP36 binary (used for ranking, not publication-quality statistics) confirms that fallback-heavy workloads remain the priority:

| workload | wall time | output |
|---|---:|---|
| BFS large | ~16.3 s | 398 |
| sliding-window large | ~6.0 s | 950005000 |
| sort-search large | ~4.2 s | `1 / 100000 / 50000` |
| frequency-count large | ~3.25 s | `10 / 10000` |

These are not evidence against the AST architecture: CP17 showed ~45–48x on AST-covered loops/arithmetic while fallback-heavy Fibonacci moved only ~1.09x. CP18–CP29 intentionally added structural coverage without claiming complete prepared execution. The remaining leaderboard therefore primarily identifies the order in which DeepSeek should complete prepared execution and re-profile.

Priority: BFS first, then sliding-window, sort/search and frequency-count. For each, record AST/fallback coverage before interpreting its timing, and hunt workload-specific complexity defects in addition to the common legacy evaluator tax.
