# Mangler/printer checkpoint 14: integration evidence

Date: 2026-09-13

This checkpoint integrates the preceding thirteen controlled mangler and
printer changes.  The repository-wide `make test` target completed
successfully with these locally available gates:

- standalone C++ smoke tests;
- Node script and module semantic differentials;
- 15,459 generated JavaScript programs;
- 12 adversarial scope cases;
- 19 structured-mode cases;
- 11 aggressive differential cases;
- 115 generated non-JavaScript idempotence documents;
- cross-format adversarial tests;
- CLI smoke tests; and
- 70,000 deterministic fuzz cases.

The CLI JSX golden was updated to reflect the newly certified local named
function mangling.  This was a stale expected spelling, not a behavior change
found by the semantic oracles.

The omitted dependencies were installed and every external gate was rerun.
The generated JSX corpus passed 180/180, PostCSS semantic comparison passed
17/17, and the real-bundle syntax and semantic gates each passed 36/36.
Test262 used revision `419d3e0a2273ba01a3bfcbec423f2801425b8e93` and the
39,741 source paths retained as runtime-applicable by the public conformance
runner.  The final repaired 11,007-source segment exercised 21,774 default and
strict scenarios: 20,543 passed and 1,231 were classified failures.  There
were zero unexpected transformed failures.  The classified failures are the
known generic-harness ArrayBuffer-detachment limitation and tests which assert
the original inferred function/class `.name` spelling; the latter require the
alpha-renaming-aware oracle described in the handover before they can certify
mangling.

The rerun found and repaired dynamic-import nested-arrow reference coverage,
an ASI-significant newline after arrow bodies, captured-name allocation through
nested arrow chains, destructuring shorthand, class heritage, for-await,
unresolved-name, top-level-declaration, and rewrite-overlap defects.  The full
repository `make test` gate then passed, including 15,459 generated JavaScript
programs, 70,000 deterministic fuzz cases, all dependency-backed gates, and
36/36 real-bundle validations.

Fresh same-host output sizes from the pinned benchmark fork are:

| Fixture | Conservative | Structured | Aggressive |
| --- | ---: | ---: | ---: |
| lodash | 148,438 | 78,047 | 76,339 |
| moment | 97,951 | 64,714 | 63,942 |
| TypeScript | 5,911,581 | 3,722,060 | 3,646,841 |
| D3 | 400,431 | 336,276 | 332,821 |

Against checkpoint 14 before the conformance repairs, aggressive output is
210 bytes larger for lodash, 33,322 bytes smaller for moment, 2,237,054 bytes
smaller for TypeScript, and 65,539 bytes smaller for D3.  The safety barriers
therefore recover correctness while retaining most of the benchmark gains;
D3 remains the clearest optimizer/mangler coverage gap.
