# JavaScript optimizer checkpoints 16–30

This tranche activates coordinated closure mangling and adds bounded,
fail-closed analysis and compression work corresponding to checkpoints 16–30.
It does not claim that the eventual full parser, CFG or effect system is complete.

## Mangling

- Captures propagate through every intermediate closure, not only the function
  containing the final read.
- Descendant functions reserve the allocated names of captured ancestors.
- Captured parameter, `var`, lexical and catch bindings can therefore be renamed
  without the Moment/jQuery collision found by the earlier prototype.
- Simple object and array destructuring parameters use the same coordinated
  binding identity and allocator; object keys remain unchanged.
- Disjoint sibling scopes retain independent short-name reuse.
- `var` live-range reuse is enabled only for straight-line functions with no
  branches, loops, exceptions, nested functions, suspension, dynamic lookup or
  captured bindings. A broader token-interval prototype broke Victory and was
  rejected before commit.

Function/class name mangling, complete nested binding patterns and import/export
binding rewriting remain excluded where source spelling or incomplete grammar
coverage can be observable.

## Analysis

- Composable read/write/call/throw effect summaries and primitive value/
  truthiness facts are retained.
- Property and call sites are explicitly classified as potentially invoking
  user code rather than assumed pure.
- Local bindings retain conservative escape classifications.
- `javascript_effect_signature` provides a deterministic evaluation-event
  oracle alongside `javascript_binding_signature`.
- A conservative token control-flow relation, explicit binding read/write/
  capture counts and dynamic-scope barriers feed aggressive decisions.

These are conservative foundations. Complete exceptional CFG edges,
path-sensitive values, iterator effects, escape propagation and general
expression effects remain future expansions.

## Compression

- Literal expression statements proven unreachable immediately after
  `return`/`throw` are removed.
- Unused, uninitialized, standalone function-local `var` declarations are
  removed only when no resolved or same-spelling unresolved reference exists
  and no dynamic lookup is possible.
- Adjacent overwritten stores to the same resolved non-captured `var` binding
  are removed when both right-hand sides are primitive literals.
- Exact integer division is folded only when the divisor is non-zero, the result
  is integral and the cost model proves the replacement shorter.

General dead initializers, path-sensitive stores, sequence formation and broad
control-flow rewriting remain disabled until their proofs can use richer CFG and
effect information.

## Performance profile

`gprof` showed that the coordinated allocator called adaptive `stable_sort`
roughly eleven thousand times in the small-function benchmark. Direct ordering
for two-element binding sets improved structured scope throughput from a short
12.2 MiB/s sample to 13.1 MiB/s, clearing the 3% retention threshold.

The unused-binding prototype also performed a binding-by-unresolved-reference
cross product. Indexing unresolved spellings once per analysis pass improved a
short aggressive sample from 3.5 to 3.9 MiB/s. The remaining aggressive cost is
primarily repeated full analysis after each transactional rewrite and remains a
future batching/profile target.

## Bundle size

Compared with the final pre-checkpoint-16 workspace on the same benchmark fork:

| Artifact | Before bytes | After bytes | Reduction |
|---|---:|---:|---:|
| React | 34,086 | 29,659 | 13.0% |
| Moment | 80,086 | 76,927 | 3.9% |
| jQuery | 119,964 | 102,276 | 14.7% |
| Vue | 160,880 | 146,970 | 8.6% |
| Lodash | 112,437 | 95,929 | 14.7% |
| D3 | 366,972 | 347,308 | 5.4% |
| Terser | 592,094 | 577,857 | 2.4% |
| Three.js | 780,446 | 729,267 | 6.6% |
| Victory | 1,060,664 | 812,279 | 23.4% |
| ECharts | 1,332,509 | 1,192,909 | 10.5% |
| Ant Design | 3,531,749 | 2,511,941 | 28.9% |
| TypeScript | 4,762,648 | 4,446,368 | 6.6% |

Every listed output passed its artifact's executable validator in conservative,
structured and aggressive modes.

## Final validation

- Test262: 39,747 transformed passes, 8,264 runtime-inapplicable cases and zero
  transformed failures.
- JSX conformance: 221/221 cases passed.
- All 36 real-bundle syntax/runtime validators passed across conservative,
  structured and aggressive modes.
- The generated JavaScript corpus, scope, structured, aggressive differential,
  format, cross-format, CLI and 70,000-case fuzz suites passed.
- AddressSanitizer/UndefinedBehaviorSanitizer smoke, CLI and 70,000-case fuzz
  runs passed with leak detection disabled for the test harness.

The TypeScript-generated JSX and PostCSS integration checks were skipped because
their optional dependencies were not installed in this workspace.
