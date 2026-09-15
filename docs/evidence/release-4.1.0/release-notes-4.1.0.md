# Nift v4.1.0

Nift 4.1.0 adds the v4.1 template-language surface as optional advanced
templating while keeping the ordinary Nift model (`@input`, `@content`,
`@path`) unchanged. It also embeds the released Jsonic++ v1.0.0 and Minify++
v1.1.3, restores Nift's historical duplicate-key strictness, and ships the
independently certified result of the v4.1 template-language campaign.

## v4.1 template language (optional advanced templating)

The normal Nift workflow remains `@input` / `@content` / `@path`. Templates
that need build-time state can opt in to the new v4.1 surface:

- `$[x := value]` declares a mutable binding in the current lexical scope;
  `$[x = value]` assigns the nearest visible existing mutable binding and never
  declares. `:=` and `=` are right-associative, support chaining, and
  top-level declaration/assignment interpolations change state but render no
  text.
- Types are inferred at declaration and remain stable for the lifetime of the
  binding; incompatible reassignment is an error. Integer-valued and
  double-valued numeric literals are distinguished.
- `$[const x := value]` declares a binding that cannot be rebound. `$[immut x
  := value]` establishes a recursively read-only binding/view contract; an
  `immut` view does not globally freeze storage reachable through a separate
  mutable binding.
- Every nested construct (`@if`, `@for`, `@input`, `inject()`, `@fn` and
  `@fragment` calls) runs in a child scope: it can read and mutate visible
  outer mutable bindings, while declarations made inside the construct
  disappear when it exits. There is no `global` escape.
- `@:=(name){ expression }` is the multiline spelling of `$[name :=
  expression]`; the outer braces are directive framing, not part of the value.
- `inject(path)` parses and evaluates another Nift expression source at the
  call position with its own child scope and an automatically tracked
  dependency. Paths are project-contained, cycle-checked, and never interpreted
  by file extension.
- `validate(schema, value)` validates a value against a Jsonic++ JSON Schema
  and returns the value unchanged on success, failing the build otherwise.
- `@fn(name(args)){ ... @return(expr) ... }` defines a value callable and
  `@fragment(name(args)){ ... }` defines a rendered-template callable. Both are
  invoked only as `$[name(...)]`, which leaves foreign at-rules such as CSS
  `@media` / `@supports` / `@font-face` / `@keyframes` easy to pass through.
  Function bodies honour conditional `@return`, and recursive callables fail
  cleanly at a documented depth bound.
- Structured bindings support `.member` and `[index]` access. Callables and
  `validate()` accept object/array literals as arguments.

## Dependency integration

- The embedded JSON implementation is the released **Jsonic++ v1.0.0**
  (release tag `v1.0.0`, revision `88e4736`).
- The embedded minifier is the released **Minify++ v1.1.3** (release tag
  `v1.1.3`, revision `43288d1`).
- Nift historically rejected duplicate object keys in every JSON document it
  parsed. Jsonic++ v1.0.0 defaults to RFC-preserving duplicate keys, so Nift
  restores its historical strictness through the `nift_json::parse` policy
  wrapper for config, tracked state, `@json` data, schemas, inline and
  expression literals.
- The dependency synchronization check compares Nift's vendored payload against
  the sibling release tag for the declared vendored version (Jsonic++ v1.0.0,
  Minify++ v1.1.3), independently of sibling development heads, and reports
  whether an update is available.

## Independent certification

The v4.1 template-language implementation was independently reviewed and
adversarially tested before release: review first, adversarial testing second,
repair third, then a full re-certification from clean worktrees. Seven genuine
language defects and two historical JSON/minifier integration regressions found
by that campaign were fixed and are protected by dedicated regression coverage
(see `docs/handover/V4.1-TEMPLATE-LANGUAGE.md` CP25 and
`docs/evidence/v41-independent-certification.md`).

## Compatibility

- Legacy `@json` forms remain fully supported while `$[j := {...}]`,
  `@:=(j){ {...} }`, `$[j := inject(path)]` and `$[j := validate(schema,
  inject(path))]` provide the composable v4.1 equivalents.
- Existing v4.0.13 sites build unchanged; the Nift website and benchmark-site
  dogfood builds are byte-identical between v4.0.13 and v4.1.0 where no
  intended content change exists.
- Common existing-template workloads show no material performance regression
  versus v4.0.13.
- The embedded engine, its language bindings, the shared corpus and the
  experimental Rust implementation remain in-tree but are not released,
  documented or promoted.