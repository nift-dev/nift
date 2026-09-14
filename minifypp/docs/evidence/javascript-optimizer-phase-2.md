# JavaScript optimizer phase 2 evidence

Phase 2 adds scope-aware local binding mangling on top of the Phase 1 semantic
inventory. Structured and aggressive modes now rename eligible parameters,
simple `var`/`let`/`const` declarators, catch identifiers, rest parameters, and
block-bodied arrow scopes. Nested functions are allocated independently;
captured bindings, dynamic `eval`/`with` scopes, observable function names,
destructuring catch patterns, classes, methods, and concise-arrow scopes remain
outside the safe subset.

## Correctness gates

- Test262 revision `72faf8ec`: 48,011 eligible scripts, 39,747 executable
  passes, 8,264 runtime-inapplicable originals, zero transformed failures.
- Twelve benchmark artifacts in three modes: 36 syntax checks and 36 artifact
  runtime validations passed.
- Generated JavaScript: 15,459 programs passed.
- Deterministic cross-format fuzz: 70,000 cases passed normally and under
  ASan/UBSan (`ASAN_OPTIONS=detect_leaks=0`).
- In-repository smoke, Node, module, scope, structured, aggressive differential,
  format, cross-format, and CLI gates passed.
- The TypeScript JSX/TSX oracle passed all 221 cases with structured expression
  mangling enabled. Its scope-aware token projection canonicalizes resolved
  local parameter and variable symbols while retaining exact spelling for
  globals, properties, JSX names, types, labels, and unresolved references.

The optional local PostCSS and repository-local TypeScript checks skipped when
their dependencies were unavailable; the dedicated TypeScript JSX/TSX corpus
was run separately as described above.

## Benchmark change from Phase 1

The following uses the benchmark's aggressive instance at Phase 1 commit
`85e0d07` and this phase on the same machine. Times are deliberately omitted;
the artifact sizes and validators are deterministic, while single-run timing
is not a stable performance comparison.

| Artifact | Phase 1 bytes | Phase 2 bytes | Raw reduction | Phase 1 gzip | Phase 2 gzip |
|---|---:|---:|---:|---:|---:|
| React | 39,653 | 34,469 | 13.1% | 10,937 | 10,174 |
| Moment | 95,094 | 80,170 | 15.7% | 25,226 | 22,831 |
| jQuery | 142,057 | 120,327 | 15.3% | 41,319 | 37,601 |
| Vue | 195,054 | 163,153 | 16.4% | 57,823 | 52,083 |
| Lodash | 140,420 | 113,081 | 19.5% | 36,030 | 32,738 |
| D3 | 389,385 | 368,324 | 5.4% | 105,386 | 103,057 |
| Terser | 632,017 | 594,332 | 6.0% | 146,807 | 142,190 |
| Three | 922,945 | 790,407 | 14.4% | 192,408 | 179,091 |
| Victory | 1,385,716 | 1,076,452 | 22.3% | 221,054 | 198,503 |
| ECharts | 1,724,545 | 1,359,754 | 21.2% | 435,618 | 386,405 |
| Ant Design | 4,315,490 | 3,582,413 | 17.0% | 619,687 | 562,898 |
| TypeScript | 5,635,746 | 4,807,500 | 14.7% | 1,124,387 | 1,022,255 |

Phase 2 closes a meaningful portion of the identifier-mangling gap, but it is
not a complete compressor. Destructuring bindings, concise arrows, safe
top-level policy, live-range name reuse, and the structured syntax/effect passes
remain subsequent work.
