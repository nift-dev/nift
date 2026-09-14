# JavaScript aggressive optimization checkpoint 30

The aggressive layer remains explicitly opt-in. Its Linux release candidate was
tested against Test262 revision `72faf8ec1445c55149615e8b35187830783aba1a`.
An initial complete preflight exposed 36 semantic failures: 35 came from removing
parentheses which were actually call delimiters, and one came from simplifying a
conditional nested beneath `??`. Both families were reduced into permanent tests.
The repaired 36-case subset passed before the final complete run.

The 20-iteration synthetic benchmark measured the aggressive workload at
890,000 input bytes, 429,999 output bytes and 8.8 MiB/s. The same run measured
the structured scope workload at 690,000 input bytes, 339,999 output bytes and
20.9 MiB/s. Aggressive mode currently performs separate structured-renaming,
transformation and conservative-print passes, so this throughput is accepted
only as experimental evidence and not as a production performance target.

The local release gates passed 15,459 generated JavaScript programs, 180
generated JSX programs, 221/221 external JSX cases, 70,000 deterministic fuzz
cases, ASan/UBSan, and an 11-case aggressive execution comparison against source,
Terser and esbuild. PostCSS remained unavailable and was skipped. macOS, Windows,
real-bundle execution, source maps and fixed-host RSS evidence remain open.
