# NIFT v4.9 performance campaign

The user accepted CP49-0/1/2 and authorized the bounded campaign, accepted
checkpoint commits, normal pushes and non-release certification on 2026-10-08.
Starting accepted source: e291575f004a601a7d9cbb645ec7810e09bdc325; original
runtime binary SHA256 f8da5147504226c0353559de11b9366820ba6064d07aced5e8ca366b49e7679e.
Development version 4.9.0; ABI 1.3. Frozen official series 20261008-v480 remains
an immutable reference; all probes here are independent local diagnostics.

## CP49-3A: live numeric identity binding

A pure numeric binding in an expression lambda previously re-entered the whole
compatibility expression evaluator. The shared callback-body helper now reads
its canonical live variable scope slot after synchronization. Named function
precedence, nonnumeric/unresolved bindings and excessive expression depth retain
compatibility dispatch. No values, scopes, references or origins are cached.
The result remains a value copy; return-location propagation is unchanged.

At N=2,000, exact paired Callgrind totals (whole process) changed:

| Probe | Before | After | Delta |
|---|---:|---:|---:|
| identity map | 30,313,041 | 28,157,102 | -7.11% |
| identity sort | 64,116,878 | 62,042,922 | -3.23% |
| scalar call control | 33,194,651 | 33,194,672 | +0.0001% |
| BFS control | 111,872,138 | 111,870,131 | -0.0018% |
| loop control | 12,967,714 | 12,966,044 | -0.0129% |
| arithmetic sort control | 67,481,342 | 67,487,367 | +0.0089% |
| indexed sort control | 143,381,666 | 143,397,709 | +0.0112% |

Ordinary identity calls at N=16,000: 1,094,046,890 → 1,076,894,914
instructions (-1.57%). This is a shared general abstraction improvement.
Seven interleaved recorded samples plus warm-up at N=2,000 give identity-map
CPU 13.26 → 11.74 ms (-11.5%); sort 28.35 → 28.18 ms (effectively neutral).
An earlier independent run gave map 9.49 → 7.62 ms and sort 27.74 → 24.73 ms.
Host/compiler load is variable: do not interpret tiny CPU changes or claim a
portable sort wall-time speedup. Deterministic instruction reductions reproduce.
The ordinary-call timing medians 226.35 → 217.71 ms are likewise noisy.

Memcheck: allocation counts unchanged (map 44,550; sort 84,567; scalar call
32,489; BFS 80,752), zero errors and zero live heap blocks. Cumulative allocated
bytes differ by only three CLI-path bytes. This patch eliminates dispatch work,
not frame allocations. N=16,000 peak RSS samples overlap except map medians
16,676 → 16,852 KiB (+176 KiB, about 1%); no data representation or allocation
change explains a systematic increase. Raw paired data are in [3a](3a/).

Correctness: 27 explicit selector/capture/location/error contracts preserve exact
baseline status/stdout/stderr; existing callback compatibility matrix and 470
numeric/fallback parity pairs pass. Deterministic counter guard proves identity
map/sort execute 100,000 prepared bodies with zero legacy calls, while nonnumeric
values still fall back; existing cache bounds and arithmetic guards pass.
Safety certification and acceptance are recorded as each checkpoint completes.

CP49-3A decision: **KEEP**. Focused ASan/UBSan/LSan selector (27 cases),
callback compatibility matrix and location receiver synchronization checks pass.
The first lifetime build contains the identity branch before its final depth
eligibility narrowing; final optimized parity/counters cover that narrowing.
Final campaign sanitizer certification will rebuild the exact final source.
Memcheck zero-error/all-freed evidence independently covers the final optimized
candidate. No concurrency machinery was changed.
