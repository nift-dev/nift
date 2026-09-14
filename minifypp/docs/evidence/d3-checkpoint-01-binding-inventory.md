# D3 checkpoint 1: binding and rejection inventory

Date: 2026-09-13

Baseline Minify++ commit: `beb0a66`

Fixture: `d3@6.3.1/dist/d3.js`, 555,767 source bytes, from the pinned
`minification-benchmarks` workspace.

The maintained `js-mangle-inventory` observer reported:

| Metric | Bindings | Recoverable bytes |
| --- | ---: | ---: |
| Total bindings | 8,150 | - |
| Eligible | 2,702 | 55,377 |
| Already short | 2,831 | - |
| Unsupported binding kind | 287 | 8,516 |
| Duplicate-name ambiguity | 16 | 30 |
| `arguments` alias barrier | 691 | 6,106 |
| Opaque class/heritage barrier | 1,554 | 48,107 |
| Concise-arrow barrier | 69 | 1,030 |
| Dynamic lookup | 0 | 0 |

The resolver recorded 28,724 references and 1,670 unresolved identifiers.

The class/heritage rejection category is the dominant measured opportunity:
it accounts for 48,107 potential identifier bytes, 75.4% of all bytes reported
behind explicit rejection barriers. The next checkpoint must split this broad
category by lexical scope rather than assuming all class-contained references
are equally unsafe.

This checkpoint is diagnostic only and does not change minifier behavior.
