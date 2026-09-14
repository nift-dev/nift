# JavaScript and JSX optimization roadmap

This roadmap separates three contracts. `conservative` remains the default and
may only perform transformations supported by complete language conformance.
`structured` may use syntax and scope information but must still preserve all
observable behaviour. `aggressive` is explicitly opt-in and may trade source
shape, diagnostics and evaluation detail for smaller output, while retaining a
documented semantic contract.

No checkpoint advances while its applicable product tests, sanitizers, complete
Test262 run, complete JSX/TSX token-preservation run, and size/performance evidence
are red. Every discovered defect is first reduced into a permanent product test.

## Current repaired baseline

The default JavaScript scanner removes comments and safe trivia, preserves ASI
boundaries after closing braces, removes semicolons only where its current token
context proves them redundant, shortens booleans only where precedence cannot
change, shortens radix integers, and selects shorter string delimiters.

The default JSX scanner shares trivia removal but preserves every non-trivia TSX
token. The incomplete parameter-renaming planner is retained as inactive research
code and is not part of either public default.

Evidence at Test262 revision `72faf8ec1445c55149615e8b35187830783aba1a`:

- 48,011 eligible JavaScript programs;
- 39,744 runtime-applicable programs passed after transformation;
- zero transformed failures;
- 221 eligible JSX/TSX programs passed exact non-trivia token comparison at
  TypeScript revision `1e4744d68260a7cb91b62b12edc3f6a2187faaf1`.

## Default conservative mode

1. **Public policy types.** Add an options structure and named optimization
   level while keeping current calls source-compatible and conservative by
   default. At this checkpoint, every mode produces the conservative output.
2. **Authoritative token stream.** Replace output-position side tables with one
   lexer representation covering identifiers, punctuators, literals, regexes,
   templates and JSX transitions. It must be non-mutating first.
3. **Template-expression lexing.** Tokenize `${...}` recursively while copying
   raw template segments byte-for-byte. Add nested template/regex/brace cases.
4. **Separator oracle.** Centralize the rules that prevent token merging,
   comment creation, numeric-member ambiguity and punctuator reinterpretation.
5. **Line-terminator model.** Record restricted productions, postfix operators,
   async/yield/await, arrow heads and expression continuations explicitly before
   removing any additional newline.
6. **Statement-boundary model.** Represent empty statements, labels, control
   bodies, do/while, Annex B declarations and expression/declaration endings.
   Expand semicolon elision only from this model.
7. **Literal candidates.** Generate boolean, number and string candidates with
   precedence and following-token checks in one place; choose only a strictly
   shorter proven-equivalent spelling.
8. **Module grammar.** Add complete module extraction and a module-aware oracle
   before claiming import/export optimization coverage.
9. **JSX region contract.** Retain exact TSX non-trivia tokens by default;
   expand only trivia removal across ordinary JS, attribute expressions,
   children expressions and nested JSX roots.
10. **Conservative release gate.** Run Test262 and JSX/TSX on Linux, macOS and
    Windows, compare representative React/Moment/Lodash-style bundles, record
    output deltas and cap throughput/RSS regressions.

## Structured optimisation layer

11. **Structured mode activation.** Expose `structured` as an explicit option,
    initially identical to conservative mode. Never silently activate it for
    existing callers or JSX.
12. **Concrete syntax tree.** Parse statements and expressions while retaining
    enough source structure for directives, comments, ASI and stable printing.
13. **Scope graph.** Model script/module/function/block/class/catch scopes,
    hoisting, Annex B bindings, private names and direct `eval`/`with` hazards.
14. **Reference resolution.** Resolve declarations and reads through nested
    closures, defaults, computed keys, templates, destructuring and shorthand.
    Keep renaming disabled during this checkpoint.
15. **Deterministic printer.** Print the tree without optimization and require
    semantic conformance plus idempotence before transformations are enabled.
16. **Parameter renaming.** Rename only resolved parameters; preserve duplicate
    parameter semantics, function length constraints where contracted, property
    keys, labels and diagnostic-sensitive exclusions.
17. **Local binding renaming.** Extend renaming to `var`, `let`, `const`, function,
    class and catch bindings with deterministic frequency-weighted names.
18. **Shorthand-aware printing.** Expand `{name}` or destructuring spellings when
    required to preserve property names while shortening bindings.
19. **Structured JSX expressions.** Apply structured optimization only inside
    fully parsed JavaScript expression regions, behind an explicit JSX option and
    a stronger compile/runtime oracle; JSX markup and text remain separate.
20. **Structured release gate.** Complete cross-platform conformance, large
    real-bundle differential execution, source-map decision, performance/RSS
    budgets and documented unsupported constructs.

## Optional aggressive compression

21. **Aggressive contract and CLI/API opt-in.** Define observable guarantees and
    exclusions first. Aggressive output must never be selected implicitly.
22. **Pure constant folding.** Fold arithmetic, comparison, boolean and string
    expressions only when coercion, overflow, `-0`, `NaN`, BigInt and exceptions
    are proven equivalent.
23. **Control-flow simplification.** Simplify constant branches and conditional
    expressions while preserving hoisting, lexical declarations and completion
    values.
24. **Dead-code elimination.** Remove unreachable statements after terminating
    control flow without changing declarations, directives or function metadata.
25. **Expression compression.** Introduce sequences, compound assignments and
    equivalent operator forms under explicit precedence/evaluation-order proofs.
26. **Binding and declaration compression.** Join declarations and remove unused
    bindings only after complete side-effect and escape analysis.
27. **Function compression.** Optimize returns, arrows and immediately invoked
    functions while preserving `this`, `arguments`, `new.target`, names and
    constructor behaviour.
28. **Property mangling as a separate sub-option.** Require a reserved-name/API
    boundary configuration; never assume externally visible properties are safe.
29. **Multi-minifier differential corpus.** Compare execution and output against
    unminified sources plus established minifiers across libraries and generated
    adversarial programs. Competitor agreement is evidence, not an oracle.
30. **Aggressive release gate.** Publish separate size/speed/conformance results,
    fuzz each optimization independently, and retain automatic bisection to the
    first transformation that changes behaviour.

The sequence is intentionally incremental. A later checkpoint may be split, but
scope, parser and conformance boundaries must not be combined merely to reduce
the apparent number of steps.

## Optimizer foundation phase 1 — 2026-09-12

The first post-benchmark foundation tranche is complete and recorded in
`docs/evidence/javascript-optimizer-foundation-phase-1.md`. It adds authoritative
identifier roles, scanner-retained brace roles, navigable delimiter structure,
binding identities, reference access/capture facts, dynamic-scope propagation,
transactional rewrites, per-pass controls and a 36-output real-bundle gate.

The tranche deliberately stops short of calling the optimizer foundation
complete. A full expression/statement parser, precedence-aware optimized
printer, complete binding-pattern and module coverage, control-flow graph,
effect/escape analysis and frequency-weighted allocator remain required before
comprehensive structured mangling is activated.

The subsequent performance sequence and bounded Phase 3/4 work are recorded in
`docs/evidence/performance-and-optimizer-phases-3-4.md`. In particular, that
evidence records two executable-bundle failures that Test262 alone did not
expose. Captured-binding renaming and dead-local elimination remain disabled;
they must not be presented as completed roadmap capabilities.

The next fifteen coordinated-mangling foundation checkpoints are recorded in
`docs/evidence/javascript-optimizer-foundation-checkpoints-1-15.md`. They add
syntax inventories, canonical binding/reference/capture identities, a topology
oracle, nested-name barriers and an interference-aware deterministic allocator.
Captured-binding rewriting remains disabled until checkpoint 16.

The bounded checkpoint 16–30 implementation is recorded in
`docs/evidence/javascript-optimizer-checkpoints-16-30.md`. Coordinated captured
binding mangling is now enabled and validated on executable bundles. The effect,
CFG, liveness and compression portions remain intentionally fail-closed subsets;
their broader roadmap definitions must not be inferred from the checkpoint
numbers alone.

## Aggressive checkpoint execution — 2026-09-12

Checkpoints 21–30 activate a separately named, CLI/API opt-in aggressive layer.
Its implemented transforms are intentionally smaller than the roadmap's broad
categories: exact non-negative safe-integer arithmetic in parenthesized binary
expressions; literal-only conditional selection; unreachable `debugger`
statements immediately following a `return`; compound assignments for two
references resolved to the same non-dynamic binding; adjacent uninitialized
`var` joins; and literal-returning, argument-free anonymous IIFEs. Every planner
falls back without modifying source when its local proof is incomplete.

Property mangling requires an explicit allowlist for every eligible property.
There is no inferred-private mode. Aggressive JSX is likewise active only inside
fully parsed expression regions and only when the existing explicit structured
JSX-expression option is selected.

The release gate includes a permanent ten-case source/Minify++ execution corpus.
When installed, Terser and esbuild are also executed over that corpus as
independent comparison points; their agreement is recorded as evidence and is
never treated as the semantic oracle. Complete Test262, JSX, sanitizer, fuzz,
output-size, throughput and repository-identity results are recorded at the
final commit before advancing this section beyond Linux evidence.

## Conservative checkpoint execution — 2026-09-12

The ten controlled commits after the repaired baseline established the public
policy API, a non-mutating source-positioned token inventory, recursive token
coverage inside template substitutions, centralized separator/newline/semicolon
decisions, centralized literal candidate selection, a module execution oracle,
and consistent policy propagation through JSX regions. All three named policy
levels still emit conservative output; no structured or aggressive rewrite is
active.

Linux release evidence:

- Test262 revision `72faf8ec1445c55149615e8b35187830783aba1a`: 48,011
  eligible scripts, 39,747 runtime-applicable original/transformed passes,
  8,264 classified runtime incompatibilities, zero transformed failures;
- TypeScript JSX corpus revision `1e4744d68260a7cb91b62b12edc3f6a2187faaf1`:
  221/221 exact non-trivia token passes and zero transformed failures;
- 15,459 generated JavaScript programs, 180 generated JSX programs, five
  module differential cases, 70,000 deterministic fuzz cases, CLI,
  cross-format and ASan/UBSan gates passed;
- PostCSS semantic testing was skipped because PostCSS is not installed in the
  release environment.

Representative local benchmark output sizes were unchanged from the repaired
baseline: JavaScript 850,000 -> 729,999 bytes, JavaScript-scope 690,000 ->
639,999 bytes, and JSX 1,160,000 -> 1,070,000 bytes. Median throughput in a
40-iteration sample was 70.8, 105.5 and 60.0 MiB/s respectively. The ordinary
JavaScript scanner result is below the earlier 81.2 MiB/s sample, so token
inventory overhead remains a performance item before declaring the conservative
release gate complete.

Checkpoint 8 currently supplies module-aware differential execution, but the
independent Test262 selector still excludes modules; complete module corpus
extraction remains open. Checkpoint 10 is likewise Linux-complete only. macOS
and Windows conformance runs, a fixed-host RSS comparison, PostCSS installation,
and representative real-bundle comparisons remain required before a portable
release claim.

### Token-inventory profile

An instrumented 40-iteration profile found that passing the inventory hook as
`std::function` leaked type-erasure overhead into conservative scans: its
manager path appeared roughly 60.4 million times and accounted for 4.1% of all
sampled time. Checkpoint 11 replaced it with a nullable recorder pointer.
Conservative mode now passes null and performs no inventory allocation; an
explicit structured or aggressive policy supplies the recorder.

The ordered plan for completing the semantic IR, path-sensitive effect
analysis, exceptional CFG and broad dead-code elimination is maintained in
`docs/handover/JAVASCRIPT-OPTIMIZER-CHECKPOINTS-31-70.md`.
