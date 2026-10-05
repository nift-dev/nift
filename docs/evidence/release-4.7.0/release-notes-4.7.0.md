# Nift v4.7.0

Nift v4.7.0 is a runtime-performance and robustness release. It expands prepared
execution so ordinary script loops stop re-running the legacy string evaluator
on every iteration, hardens the expression parser against very large generated
inputs, and fixes a prepared-loop `return` regression. Language semantics are
unchanged and the C ABI remains 1.3.

### Performance

- **Faster scripting hot paths.** Prepared execution now covers plain
  user-function call statements and collection/map method operations executed
  inside script loops, removing repeated legacy-evaluator and string-processing
  work per iteration. On Nift's tracked scripting microbenchmarks the final
  v4.7.0 build compares to v4.5.0 as follows (callgrind instruction counts;
  lower is better):

  | workload | vs v4.5.0 |
  |---|---|
  | `fn_empty` (empty function calls) | ~ -95.9% |
  | `fn_args` (one-argument calls) | ~ -94.1% |
  | `lambda` (lambda calls) | ~ -37.4% |
  | `map_set_new` (map insertion) | ~ -93.1% |
  | `map_iterate` (map iteration) | ~ -91.1% |
  | `numeric_loop` | faster than v4.5.0 |
  | `array_push_index` | faster than v4.5.0 |
  | `map_get` | faster than v4.5.0 |
  | `map_contains` | faster than v4.5.0 |

  These are the specific tracked scripting microbenchmarks, not a claim that
  every Nift workload is ~90% faster. Website build performance is unchanged.

### Expression parser robustness

- **Very large flat expressions no longer crash.** Flat binary and logical
  chains (`1 + 1 + ...`, `true && ...`, `... || ...`) and deeply parenthesized
  expressions are now handled without C++ stack growth proportional to their
  length. Expressions with **64,000+** terms parse and evaluate; previously,
  sufficiently long chains (roughly 1,000-2,000 operators) could exhaust the
  parser stack and crash.
- **Controlled diagnostics for pathological nesting.** Genuinely recursive
  syntactic nesting (unary, call, index/member) is bounded by an intentional
  resource limit and reports `expression nesting exceeds parser limit`, with
  nesting up to 64 supported and covered by the external contract suite.
  Pathological input never produces a segmentation fault or abort.
- **Callable recursion** remains bounded at **64**, and sanitized builds now
  enforce that documented limit with a controlled diagnostic instead of
  exhausting the instrumented stack first.

### Bug fixes

- **`return` inside a prepared `@for`.** A v4.6 regression could make a
  `return` from within a prepared array `@for` body continue iterating and yield
  the wrong element instead of propagating the return. v4.7.0 restores the
  documented (v4.5) behavior.

### Certification and hardening

- Added a dedicated **deep-capable** ASan/UBSan sanitizer profile that can
  exercise Nift's real recursion and nesting limits, and a separate **lifetime**
  ASan/UBSan profile that retains stack-use-after-scope detection; a small
  canary proves the two profiles genuinely differ.

### Compatibility

- **Language semantics are unchanged.**
- **C ABI remains 1.3.**
