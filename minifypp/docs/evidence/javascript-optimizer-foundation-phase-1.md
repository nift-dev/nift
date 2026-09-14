# JavaScript optimizer foundation — phase 1

Phase 1 replaced several output-position heuristics with source-positioned,
fail-closed optimizer infrastructure. It does not enable broader mangling or
compression by default.

Implemented foundation:

- scanner-owned block/object brace classifications retained in the token stream;
- a navigable delimiter tree with parents, children and matching-token links;
- explicit identifier roles for bindings, references, object keys, shorthand
  properties, member properties, labels, import/export names and private names;
- lexical scopes separated from object literals;
- stable binding identities and reference-to-binding resolution;
- `var` hoisting, catch bindings and containing-function propagation of direct
  `eval`/`with` hazards;
- read, write, read/write and cross-function capture facts;
- conservative constant, effect and abrupt-completion facts;
- transactional, non-overlapping, size-decreasing rewrite batches;
- independently disableable public JavaScript optimization-pass identities;
- a permanent 12-artifact, three-policy real-bundle syntax gate.

Two real-bundle failures were found and reduced. The first expanded shorthand
syntax inside nested call arguments and made Victory, ECharts, Antd and
TypeScript syntactically invalid. The second confused a ternary value with an
object key and left Victory references unrenamed. A 490,000-case fuzz run also
found malformed-JSX recovery that could reinterpret `/ >` as a regex start on a
second pass. All three families now have product regressions.

Linux evidence at Minify++ commit `5b35e8d` plus the final evidence commit:

- Test262 `72faf8ec1445c55149615e8b35187830783aba1a`: 48,011 eligible,
  39,747 original/transformed passes, 8,264 classified runtime-inapplicable,
  zero transformed failures;
- TypeScript JSX `1e4744d68260a7cb91b62b12edc3f6a2187faaf1`: 221/221 in
  aggressive structured-expression mode;
- 15,459 generated JavaScript programs and 180 generated JSX programs passed;
- all 12 benchmark artifacts passed their own runtime validators in conservative,
  structured and aggressive modes (36 validated outputs);
- 490,000 deterministic cross-format fuzz cases passed;
- ASan/UBSan smoke, 490,000-case fuzz and CLI gates passed with leak detection
  disabled because LeakSanitizer cannot inspect processes under this environment's
  `ptrace`; CI retains leak detection.

This checkpoint is the safe substrate for the remaining optimizer work. It is
not a claim that the expression/statement parser, control-flow graph, effect
analysis, destructuring/import/export binding coverage, deterministic optimized
printer or escape analysis are complete. Those remain ordered prerequisites to
comprehensive structured mangling.
