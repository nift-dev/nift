# JavaScript optimization checkpoint investigation and repair

The original checkpoint-10 conclusion was invalid because it relied on retained
product suites without rerunning the repository's on-demand Test262 corpus. A
fresh complete run at Minify++ commit `f414d58` found:

| Classification | Count |
| --- | ---: |
| Eligible Test262 programs | 48,011 |
| Runtime-applicable originals | 39,744 |
| Passed after transformation | 39,601 |
| Semantic failures | 137 |
| Transformed-only timeouts | 6 |
| Runtime-inapplicable | 8,267 |

Isolation at the first five lexical checkpoints still produced 29 semantic
failures. Isolation at the newline-only checkpoint produced three. Confirmed
families included closing-brace ASI, meaningful empty `else` statements, boolean
member/call precedence, `new` operands, and incomplete parameter reference
rewriting (including template expressions).

The repaired conservative baseline therefore:

- disables incomplete scope rewriting in the default path;
- preserves line terminators after every closing brace;
- preserves semicolons that form empty `else` statements;
- refuses boolean shortening where a following operation binds more tightly or
  the boolean is a `new` operand;
- retains the conformance-clean radix integer and string quote candidates;
- restricts JSX to trivia-only changes so every non-trivia TSX token is stable.

## Repaired evidence

At Test262 revision `72faf8ec1445c55149615e8b35187830783aba1a`, the repaired
default processed all 48,011 eligible programs. All 39,744 programs accepted by
the selected Node runtime passed after transformation, with zero minifier errors,
semantic failures or transformed-only timeouts.

At TypeScript revision `1e4744d68260a7cb91b62b12edc3f6a2187faaf1`, extraction
selected 221 JSX/TSX programs and excluded 56 source-parser-rejected inputs. All
221 eligible programs passed exact non-trivia token and JSX-text comparison.

The retained local gates also pass: standalone smoke, Node differential,
15,459 generated JavaScript programs, the 12-case scope adversarial gate,
70,000 deterministic fuzz cases, 115 non-JavaScript documents, cross-format
adversarial tests and CLI smoke. Sanitizer evidence is rerun at final handoff.

The complete corpora are acquired with each conformance repository's `make sync`
and `make extract` targets and intentionally remain outside version control.
