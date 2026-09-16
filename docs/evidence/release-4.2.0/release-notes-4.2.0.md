# Nift v4.2.0

Nift 4.2.0 adds the v4.2 template-language campaign: a dedicated `@fn`
function-program grammar, first-class `null`, and a stateful struct model,
while keeping the ordinary Nift workflow (`@input`, `@content`, `@path`) and
v4.1 compatibility intact.

## Function-program grammar

`@fn` bodies now use a real statement grammar instead of template grammar:

- Declarations, assignments, function calls, `if` / `else if` / `else`,
  `for`, `while`, `break`, `continue` and `return`.
- `return expr` and bare `return` are the canonical v4.2 return forms.
  Value-less returns and fallthrough produce `null`. Legacy `@return(...)`
  remains accepted inside `@fn` for v4.1 compatibility and is not the
  recommended new syntax.
- `break` and `continue` target the innermost `@for`/`@while` within the
  current callable boundary and cannot escape a function or fragment call.

## Template additions

- `@while(condition){...}` with normal inherited mutation and per-iteration
  scope.
- Fragments gain a bare value-less `return` (early return without rollback of
  already-rendered content).

## Structs

Structs combine fixed, stably typed state with methods:

- Definitions, construction, stable fields, methods, `this`, private
  fields/methods, reference semantics (assigning or passing an instance
  aliases the same instance), `copy()` (shallow) and `deepcopy()` (recursive,
  graph-sharing preserving).
- Struct member paths compose with the expression evaluator, so `a.v + 1`,
  member-assignment arithmetic and nested member paths work as expressions.

## Numeric literal typing

- Nift source numeric literals are typed by their lexical form: integer
  spellings (`0`, `8`) infer `int`; fractional/exponent spellings (`0.0`,
  `8.0`, `8.5`, `1e3`) infer `double` even when the value is integral. An
  arithmetic expression with any `double` operand is `double`. Inferred
  binding and field types stay stable.
- Parsed JSON numbers retain their established Jsonic++ representation.

## Correctness and hardening

- Incremental builds treat an equal dependency/metadata timestamp as
  potentially stale, so an immediate source edit is never silently skipped.
- Unknown struct field reads fail rather than rendering literally; struct
  reference values never leak into output.
- The v4.2 campaign was independently reviewed and adversarially certified
  (CP1–CP29): performance and memory baselines established, struct member
  expressions and function-program translation correctness fixed, and the
  full hardening wall run (sanitizers, Valgrind, fuzzing, incremental
  equivalence, package consumers, cross-platform guards).

## Compatibility

- v4.1 sites build unchanged. Template grammar remains template grammar inside
  legacy-compatible function bodies; `@return(...)` stays accepted for v4.1
  function programs.
- Common existing-template workloads show no material performance regression:
  the 10,000-page ordinary-project full build measures within noise of the
  v4.1 baseline.
- Deferred (not in this release): general collection mutation
  (`push`/`pop`/`append`/`remove`, element/member assignment), structs stored
  in arrays/objects, struct array-field indexing (`a.v[0]`), nullable typed
  struct fields, inheritance/interfaces/generics, dynamic struct fields, and
  new `copy`/`deepcopy` semantics.
- The embedded engine, its language bindings, the shared corpus and the
  experimental Rust implementation remain in-tree but are not released,
  documented or promoted.