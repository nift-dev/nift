# Mangler checkpoint 12: no-compress comparison

The current Minify++ aggressive output was measured against Terser and
UglifyJS with compression disabled. This isolates parsing, printing and name
mangling from compressor transforms. Sizes are bytes and are deterministic;
all Minify++ fixture outputs passed the repository's real-bundle semantic gate.

| Fixture | Input | Minify++ aggressive | Terser no-compress | UglifyJS no-compress | Gap to smaller peer |
|---|---:|---:|---:|---:|---:|
| react | 72,132 | 28,758 | 25,063 | 25,030 | 14.9% |
| moment | 173,902 | 76,771 | 63,011 | 62,495 | 22.8% |
| jquery | 287,628 | 100,790 | 94,258 | 94,082 | 7.1% |
| vue | 342,146 | 146,216 | 126,386 | 126,137 | 15.9% |
| lodash | 544,089 | 94,507 | 75,035 | 74,607 | 26.7% |
| d3 | 555,767 | 323,576 | 276,125 | 275,347 | 17.5% |
| terser | 1,009,635 | 527,886 | 472,579 | 472,162 | 11.8% |
| three | 1,247,235 | 727,095 | 675,428 | 674,490 | 7.8% |
| victory | 2,132,722 | 807,230 | 756,623 | 756,530 | 6.7% |
| echarts | 3,196,329 | 1,190,110 | 1,069,227 | 1,068,386 | 11.4% |
| antd | 6,672,611 | 2,494,975 | 2,420,814 | 2,420,587 | 3.1% |
| typescript | 10,945,727 | 4,379,206 | 3,525,593 | 3,538,024 | 24.2% |

Compared with the retained checkpoint-7 measurements, the barrier refinements
reduced D3 from 343,342 to 323,576 bytes (5.8%), Terser from 556,313 to 527,886
(5.1%), Victory from 812,108 to 807,230 (0.6%), and TypeScript from 4,403,762
to 4,379,206 (0.6%). The remaining no-compress gap is therefore concentrated
in incomplete identifier coverage/general printing rather than compressor-only
passes. Lodash, Moment, TypeScript and D3 remain the highest-value fixtures.

Environment: the checked-out benchmark dependencies and artifact versions in
`minification-benchmarks`, GCC C++17 `-O2`, and Minify++ built from this commit's
source. Competitor adapters are the benchmark repository's `compress: false`
Terser and UglifyJS configurations.

Final acceptance evidence: all 48,011 selected Test262 scripts completed with
39,747 original/transformed passes, 8,264 classified runtime incompatibilities,
and zero transformed failures. The TypeScript JSX corpus passed 221/221. The
full product suite also passed 15,459 generated JavaScript programs, 36
real-bundle outputs, 70,000 deterministic fuzz cases, and the module, scope,
aggressive-differential, CLI, cross-format and format-idempotence gates.
PostCSS remained unavailable, so its optional differential gate was skipped.
