# D3 checkpoint 6: external conformance

Date: 2026-09-13

Tested Minify++ commit: `dbd831ac3f89c367550b26ba67a46caefd48e9b7`

The independent JavaScript conformance runner used Test262 revision
`419d3e0a2273ba01a3bfcbec423f2801425b8e93`. Eight deterministic shards
covered all 48,011 selected source tests.

| Classification | Count |
| --- | ---: |
| Pass | 39,744 |
| Runtime-inapplicable | 8,267 |
| Minifier error | 0 |
| Minified timeout | 0 |
| Semantic failure | 0 |

The three-case change from the retained 39,741/8,270 split is within the
documented timing-sensitive runtime partition. The transformed-failure gate
remains exactly zero.

The complete repository `make test` target also passed with all optional
dependencies present: 15,459 generated JavaScript programs, 180 generated JSX
programs, 17 PostCSS semantic fixtures, 115 generated non-JavaScript documents,
70,000 fuzz cases, and 36 real-bundle syntax plus 36 benchmark validations.

This certifies retention of checkpoint 5's parent-first allocation and D3 size
recovery on the tested corpus and runtime.
