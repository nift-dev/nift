# D3 checkpoint 4: broad barrier costs

Date: 2026-09-13

The current planner's broad barriers were checked against the pinned D3 source:

| Barrier trigger | D3 occurrences | Effective scope |
| --- | ---: | --- |
| `for await` | 0 | Compilation unit |
| Dynamic `import(` / `import.source` | 0 | Compilation unit |
| Direct `eval(` | 0 | Function unit |
| `with (` | 0 | Function unit |
| Arrow functions | 72 | Outer factory rejected after second arrow |
| `class` tokens | 6 | Binding/reference-specific checks |
| `arguments` tokens | 403 | Parameter-only restriction |

An isolated diagnostic build removed only the multi-arrow unit rejection. It
was not retained at this checkpoint because the coordinated allocator repair
and conformance rerun belong to later checkpoints.

| Mode | Baseline bytes | Barrier-disabled bytes | Delta |
| --- | ---: | ---: | ---: |
| Structured | 336,276 | 295,969 | -40,307 (-12.0%) |
| Aggressive | 332,821 | 292,532 | -40,289 (-12.1%) |

This single barrier explains 70.1% of the 57,474-byte structured gap to
UglifyJS no-compress. It is therefore the checkpoint-5 implementation target.
The experimental output passed parsing, but correctness is not inferred from
syntax validity; the barrier stays in production until parent-before-child
allocation and capture-name reservation are repaired and conformance passes.
