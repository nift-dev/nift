# JavaScript optimizer checkpoints 31–70

This is the execution plan following checkpoints 1–30. “Unrestricted DCE”
means applicability throughout completely analysed code, never removal without
a semantic proof. Execute in order and apply the retention gate to every commit.

Implementation evidence for the bounded checkpoint 31–50 foundation is in
`docs/evidence/javascript-optimizer-checkpoints-31-50.md`. Do not infer full
roadmap completion from the commit labels: complete AST/IR printing,
fixed-point path analysis, interprocedural summaries and exceptional basic-block
CFG construction remain open and must be completed before checkpoints 31–50
can be promoted from foundation status.

## Semantic IR foundation

31. Add stable expression/statement IDs, source spans and binding/reference
    associations, with deterministic IR snapshots.
32. Build a complete expression tree including assignment, sequence,
    conditional, logical, optional-chain and precedence information.
33. Represent properties, calls, `new`, tagged templates, dynamic import,
    spread, receiver evaluation and `this` semantics.
34. Build a complete statement tree including loops, labels, `switch`,
    `try`/`catch`/`finally`, `with`, `debugger` and abrupt completion.
35. Model functions, arrows, async/generators, classes, parameter initialization,
    fields, static blocks, TDZ and initialization order.
36. Model imports, exports, re-exports and top-level await; install explicit
    optimization barriers for eval, `with` and dynamic-scope hazards.
37. Print semantic IR directly with precedence, ASI and lexical-boundary
    correctness, preserving required empty statements such as `while(x);`.

## Path-sensitive effect analysis

38. Define a composable lattice for reads, writes, calls, throws, allocation,
    mutation, iteration, suspension and abrupt termination.
39. Classify literals, binding reads and primitive operators including TDZ,
    Symbol, BigInt and mixed-numeric throws.
40. Model ToPrimitive, ToBoolean, ToNumber, ToNumeric, ToString, property-key
    conversion and comparison coercion.
41. Model getters, proxies, computed keys, private checks, nullish-base throws,
    writes and deletes conservatively.
42. Model call/construction evaluation order; add local function summaries,
    recursive convergence and conservative unknown-call fallback.
43. Model object/array/template construction, spreads, accessors, prototypes,
    iterable access and observable allocation identity.
44. Model destructuring order, iterator acquisition/stepping/closing, defaults
    and their exceptional paths.
45. Model function/class creation effects, computed elements, heritage, static
    initialization, captures and observable name/length data.
46. Maintain path facts for constants, primitive types, truthiness/nullishness,
    equality, TDZ, aliases, escapes and safely known shapes.
47. Expand the effect oracle to ordered observable events and certify getters,
    proxies, coercion hooks, iterators and throwing paths differentially.

## Complete normal and exceptional CFG

48. Construct basic blocks with entry, normal/abrupt exits and normal edges.
49. Add separate paths and edge facts for logical/nullish operators,
    conditionals and optional chains.
50. Model all loop forms, including `for in`, `for of` and `for await of`, with
    initializer, condition, update, break and continue edges.
51. Resolve labelled transfers and model switch test order, fallthrough, scope
    and TDZ across cases.
52. Add exceptional successors to each potentially throwing operation and route
    them to the correct handler.
53. Carry normal, return, throw, break and continue completions through
    `try`/`catch`/`finally`, including replacement by `finally`.
54. Add iterator-close and async cleanup edges for destructuring, spread and
    abrupt loop completion.
55. Represent await/yield suspension, escaped-state invalidation and generator
    `return()`/`throw()` cleanup.
56. Integrate interprocedural summaries with call-graph SCCs and conservative
    fixed-point convergence.
57. Add a deterministic CFG oracle and differential trace/completion tests for
    every normal, exceptional and abrupt edge family.

## Broad semantics-preserving elimination

58. Remove CFG-unreachable statements while preserving hoisting, declarations,
    early errors and binding existence.
59. Remove proven pure/non-throwing unused expressions and sequence operands,
    preserving directives and observable allocations.
60. Eliminate dead conditional, logical and optional paths while retaining
    condition effects.
61. Eliminate never-entered loops while retaining initializer/condition effects
    and preserving possible non-termination.
62. Add CFG-wide backward liveness and general dead-store elimination while
    retaining RHS, base, key, conversion and setter effects.
63. Remove dead variables, parameters, catches and pattern components while
    respecting initializers, `arguments`, arity, eval and debugger barriers.
64. Remove dead functions/classes after accounting for class effects, recursion,
    captures, exports and reflection-sensitive names.
65. Remove dead properties/allocations only when keys, descriptors, spreads,
    accessors, prototypes and construction are proven unobservable.
66. Merge blocks and cost-gate branch, return, jump and sequence compression
    without changing evaluation or exception order.
67. Compress conditional assignments, returns and throws when CFG and effect
    proofs establish exact equivalence.
68. Iterate to a deterministic fixed point using incremental invalidation and
    termination guards instead of whole-program analysis after every rewrite.
69. Select non-overlapping candidates by exact printed-byte cost, including
    parentheses, separators and lexical boundaries.
70. Certify with Test262, JSX, real bundles, effect/CFG oracles, fuzzing,
    sanitizers, Valgrind, benchmarks and portable-platform evidence before
    removing the experimental label.

## Retention gate

At every checkpoint: record baseline throughput and output sizes; add focused
unit, IR/oracle and differential tests; run complete Test262, JSX and real-bundle
validators; and run sanitizers for representation or ownership changes. Reject
semantic regressions. Reject or redesign throughput regressions above 3% unless
they buy a substantial measured size reduction. Commit implementation and
evidence together. Keep conservative and structured output unchanged unless the
checkpoint explicitly targets them. Checkpoint 57 is the critical boundary:
only then may checkpoints 58–69 rely on complete CFG/effect proofs rather than
adding more token heuristics.
