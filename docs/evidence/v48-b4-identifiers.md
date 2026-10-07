# v4.8 B4: early plain-identifier resolution

B2 starts at `2f90471bc9d587acc8606820c2711da33f31ac6a`. Its hosted
Deep Guards, Test Integrity, Performance Guards, Checkpoint 10, Gate 6B and
Init checks passed; local contract suites passed 93/93 and 12/12. Socket-based
package tests require an environment that permits loopback servers.

## Correctness prerequisite

The new location fixture exposed a pre-existing stale receiver read after a
parent array reallocates. Valgrind reports the invalid read on the unchanged B2
binary. The separate `428a303` fix synchronizes the location before module
receiver detection and safely rejects a removed location. Valgrind reports zero
errors after the fix; identifier/callback/location fixtures also pass ASan/UBSan.
This correctness fix is separate from B4.

## Change and precedence

After await and named-callable resolution, a syntactically plain identifier uses
the existing `resolve_direct` helper before the built-in call/operator chain.
The helper preserves variable/receiver/JSON/metadata/literal resolution and
location synchronization. Named callables retain precedence over a same-named
binding when evaluated as a value; callable invocation retains its existing
binding behavior. Unresolved names keep the old fallback and diagnostics.
Compound/member/index expressions and runtime marker syntax do not take the new
plain-identifier path. No new resolver or callable implementation is introduced.

## Frozen behavior

`v48_identifier_parity.sh` passed before implementation. It covers ordinary,
prepared, function, capture, composite, callable, shadowing, builtin-name,
receiver, module, template, location and error behavior. It explicitly retains
block-lambda failure in primary array map, synchronous async-lambda map results,
and per-element sort callback factories versus once-per-map factories.
CP-C callable parity, comparison/object/collection parity, map/set scaling and
the root/path reference corpus passed. Full native tests, GCC/Clang warnings,
core contracts (93/93), package contracts (12/12) and ASan/UBSan are certification
requirements, not replaced by the targeted fixtures.

## Measurements and guard

The exact published sort-index workload was used without modifying its repo.
Seven-sample independent-review profiling recorded 6,404,297,707 instructions;
B4 Callgrind records 3,053,416,540, a 52.32% reduction. This is stronger evidence
than host-dependent wall time. Stable sorting retains approximately 365 million
instructions; numeric comparisons remain secondary. Per-element lambda
construction, capture/scope allocation and RuntimeValue movement remain.

Paired five-sample medians (milliseconds), alternating the unchanged B2 and
B4 executables for each workload:

| Workload | B2 | B4 |
|---|---:|---:|
| sort-index | 1195.0 | 804.7 |
| function-calls | 155.7 | 162.9 |
| loops | 46.6 | 48.7 |
| sliding-window | 161.5 | 158.4 |
| bfs | 416.9 | 427.0 |
| json-transform | 297.1 | 296.6 |
| frequency-count | 284.3 | 301.4 |
| identity-map | 576.0 | 205.4 |
| filter | 617.0 | 216.9 |
| reduce | 1553.6 | 813.2 |
| json-traverse | 312.5 | 314.1 |

The repository calls its JSON query/traversal workload `json-traverse`. These
wall measurements are indicative; compiler/test activity can affect host load.
The instruction counts are deterministic for the recorded local binary.
The relative guard compares identity-map/filter against array construction using
interleaved medians and a ratio of four, without a millisecond threshold. It
fails the original B2 binary (identity/build 8.58) and passes B4 (about 2.9/3.2).

No ABI change (C ABI remains 1.3), benchmark/package changes, release workflow,
shell performance work, or CP-E is included.
