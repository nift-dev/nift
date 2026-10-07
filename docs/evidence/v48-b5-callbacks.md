# v4.8 B5: bounded immutable callback code caching

B4 is committed separately as `89f40d9`. Its exact sort-index profile dropped
from 6,404,297,707 to 3,053,416,540 instructions, but numeric reduce and
selector bodies still spent substantial time classifying repeated expression
text. B5 addresses that residual without changing callable semantics.

## Implementation boundary

Two thread-local caches hold only immutable syntax and expression plans. Each
cache has at most 256 entries; source longer than 4096 bytes stays uncached.
Plans contain at most 64 nodes with depth at most 16. Negative entries avoid
repeated preparation of unsupported syntax. Neither cache holds bindings,
closures, identities, module environments, source paths, runtime results or
mutable execution state.

Syntax hits still instantiate a fresh LambdaInstance, snapshot the current
bindings, capture the current module/source environment, assign a fresh identity
and register the instance. In particular, inline sort selectors still create
one closure per element; callback factories are never hoisted.

Prepared expression bodies support numeric bindings/literals, unary plus/minus,
addition, subtraction, multiplication, division and modulo, optionally topped
by one numeric ordering/equality comparison. Binding leaves use the canonical
expression evaluator, including location synchronization and callable
precedence. Arithmetic uses the existing numeric operator unchanged; ordering
and equality use RuntimeValue's canonical arbitrary-decimal numeric helpers.
This preserves signed integer overflow, exact large integer binding values,
division/modulo errors and comparison semantics. It does not use the AST
interpreter's double-only arithmetic.

Each invocation executes with fresh temporary values and resolves live captures.
Nonnumeric operands, calls, member/index expressions, blocks, mutations,
logical/coalescing operators, nested comparisons, excessive depth and unsupported
syntax retain compatibility evaluation. Adjacent `++`/`--` and hexadecimal
literals are explicitly excluded: the two parsers interpret those forms
differently. Unsupported parse errors remain deferred to the original route.
Both ordinary lambda invocation and primary array expression callbacks use this
body helper. Block execution, async dispatch and returned-location handling are
unchanged. Parser/LambdaInstance/RuntimeValue layouts and public ABI are unchanged.

## Compatibility oracle and guards

The callback matrix was frozen against B4 before implementation. It covers
zero/multiple/variadic parameters, expression/block bodies, locals/conditions/
loops/return/fallthrough, mutable scalar/object/collection captures, nested and
escaped closures, identity, callable precedence, recursion/reentrancy, prepared
loops and templates, all selector APIs, collection handles and comparator sort,
errors/throws, references, factories and evaluation counts. It retains direct
block success versus primary map block failure, synchronous async map behavior,
per-element sort factories versus once-per-map factories, empty input and
multiple selector behavior. Identical source in separate private modules keeps
separate captures; async worker cloning is exercised for those modules too.

`v48_callback_numeric_parity.py` compares 470 ordinary-expression/callback
value and error pairs, including int64 edges, decimals, literals, associativity,
mutation tokens, dynamic strings/arrays/null and unsupported forms. Literal and
mutation exclusions were added after parity checks exposed grammar differences.
The escaped closure hammer retains 400 independent environments and exercises
worker futures. Existing callable, async, template and reference corpora remain
part of certification.

The dedicated CLI counters are compiled only with
`NIFT_TEST_LAMBDA_CACHE_STATS`; ordinary CLI/library builds have no counters or
new output. At 100,000 calls, reduce and numeric ordering/equality filters each
report one syntax preparation, one body preparation, one closure and 100,000
prepared evaluations with zero legacy body passes. Numeric sort reports the
same preparation count while retaining 100,000 closure instances. Additional
guards verify typed fallback, the 256-entry limit and oversized-body fallback.
The guard uses structural counts, not a millisecond threshold.

## Measurements and certification

The published benchmark sources are read-only; custom selector probes use the
same 100,000-element construction workload. Five samples rotate B2/B4/B5 order
and compare outputs. Absolute times vary with host load; deterministic
instruction counts provide independent evidence.

| Callgrind workload | B4 instructions | B5 instructions | Reduction |
|---|---:|---:|---:|
| Exact published sort-index | 3,053,416,540 | 2,900,872,658 | 5.00% |
| Numeric reduce 100k | 7,166,815,207 | 1,694,629,121 | 76.35% |
| Numeric ordering filter 100k | 10,940,573,518 | 1,288,188,915 | 88.23% |

Sort-index is 54.70% below the original B2 instruction count. Sorting itself
retains 365,060,249 instructions (12.58% of the new total), while fresh closure
construction accounts for 549,757,753 inclusive (18.95%). Allocator malloc/free
account for roughly 10.2%/10.7% inclusive; these shares overlap construction and
invocation and must not be added as disjoint costs. Per-instance capture/scope
maps, RuntimeValue movement and cleanup dominate the remaining callback cost.
Numeric selector preparation no longer occurs per invocation.

Final local certification: full `make test` PASS; full first-party GCC and
Clang warnings-as-errors PASS; identifier/callable/comparison/object/collection
parity and map/set scaling PASS; nrs 93/93 PASS; prs 12/12 PASS. ASan/UBSan with
leak detection PASS for the complete callback matrix, 470 value/error pairs,
callable/async/template/reference corpora and a 10,000-element fresh-closure /
map/reduce/predicate hammer. The unchanged public C ABI remains 1.3. Four direct
error probes also retain B4's complete diagnostic text and source locations.
A sanitizer compiler rebuild initially exceeded the host's `/tmp` quota; final
compilation and tests used project-backed temporary storage and passed.

No benchmark, shell, lab or package implementation was changed. Two existing
MDX package investigation files, dated October 4/5, were left untouched.
No Release artifacts workflow was run, and CP-E was not started.


Paired five-sample medians in milliseconds:

| Workload | B2 | B4 | B5 |
|---|---:|---:|---:|
| sort-index | 1568.82 | 1098.54 | 1040.74 |
| function-calls | 180.46 | 178.41 | 177.92 |
| loops | 48.45 | 45.06 | 48.73 |
| sliding-window | 176.40 | 179.96 | 182.72 |
| bfs | 463.44 | 457.05 | 439.11 |
| json-transform | 306.89 | 309.64 | 294.55 |
| frequency-count | 301.28 | 294.86 | 282.07 |
| identity-map | 631.95 | 206.82 | 211.62 |
| constant filter | 633.30 | 231.70 | 237.06 |
| reduce | 1557.90 | 755.41 | 203.80 |
| json-traverse | 247.43 | 232.27 | 234.08 |
| numeric-map | 2294.96 | 1927.54 | 209.97 |
| numeric-filter | 1609.83 | 1150.06 | 182.03 |
| equality-filter | 2507.64 | 1995.95 | 192.47 |
| group-by | 1529.57 | 1168.57 | 254.78 |
| min-by | 1754.94 | 1397.86 | 484.82 |

Raw samples are retained in `v48-b5-timings.json`. The prior B4 paired run
recorded sort-index at 1195.0 -> 804.7 ms, identity map 576.0 -> 205.4 ms,
constant filter 617.0 -> 216.9 ms and reduce 1553.6 -> 813.2 ms. Higher sort
absolute times in the final three-way run demonstrate why its 52.32% B4 and
additional 5.00% B5 instruction reductions carry more weight than a single
wall-time comparison. Identity map and constant filter retain B4's gains;
B5's material additional gains are numeric selector bodies.

Custom probes build 100k numbers and apply `x * 2 + 1`, `x > 50000`,
`x % 2 == 0`, `x % 10` group keys, and `x + 1` minimum keys. All measured
outputs agree across the three executables. The published JSON query/traversal
workload is named `json-traverse` in the benchmark repository.

## Stopping decision

B5 is worthwhile: arithmetic and numeric predicate callbacks lose most of their
instruction overhead while all current route differences remain intact.
Among the remeasured published workloads, sort-index and BFS remain the largest;
JSON transform and frequency counting retain their previous wins. Map/set
scaling guards also pass.

No further large, profile-supported bounded evaluator classification win is
established for this v4.8 checkpoint. Identity sort now spends its material
residual in fresh closures, capture/scope storage, allocation, value movement
and sorting. Reusing mutable closure instances or hoisting factories would
change tested behavior. A larger reduction calls for a separate closure
ownership/storage or execution-engine campaign, beyond this bounded checkpoint;
it is not established that VM/bytecode is necessary. Logical, block and richer
callback bodies intentionally retain compatibility execution.

Recommended next: CP-E. Stop v4.8 residual performance implementation here.
