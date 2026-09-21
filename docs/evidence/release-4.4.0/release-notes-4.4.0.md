# Nift v4.4.0

Nift v4.4.0 turns the v4.3 language into a coherent execution environment and
delivers a large execution-engine speedup. Variadics, spread, wildcards,
process execution, the interactive shell, native build/project scripting,
lifecycle hooks, a Git-native package ecosystem (first package `sqlite`) and
the root+path location-reference model arrive together with an AST-based
execution engine that makes loops, arithmetic, function calls, collections and
JSON traversal dramatically faster. The release is hardened through the
independent regression suite, sanitizer/Valgrind/lifetime campaigns and the
full JSONTestSuite conformance corpus.

## Execution and performance

- **AST-based execution engine.** Expressions and statement fragments are
  prepared into an abstract-syntax-tree execution plan with direct dispatch
  for prepared loops, user-function calls, native collection methods and
  structured-literal JSON. The previous string-scanning evaluator hot path is
  collapsed: representative workloads such as BFS, loops, arithmetic,
  function calls, map/set operations and JSON traversal all complete in a
  small fraction of their previous time (for example BFS goes from ~16 s to
  ~310 ms and loops from ~1.5 s to ~110 ms on the certified benchmark corpus).
  The legacy evaluator remains the correctness oracle and fallback, so
  behaviour is unchanged while execution is faster.
- **JSON fast paths.** JSON parse/inject, traversal, mutation and transform
  workloads avoid repeated whole-source scanning through the structured-literal
  fast path and direct JSONIC++ parsing.
- **Performance guards.** Pay-for-use scaling guards keep array push, set add,
  map set and string concatenation linear, and a bounded differential fuzz
  verifies prepared/legacy equivalence.

## Execution shell, scripting and process execution

- **A real cross-platform shell.** `nift sh` starts the persistent native
  shell with history, completion, globbing, quoting/escaping, environment
  assignment, pipelines, redirection and `&&`/`||`/`&` control, matching
  familiar command syntax while keeping every operation a native Nift
  operation.
- **Command-style calls.** In script/shell land, `fn a b` resolves
  command-style; unquoted arguments are literal tokens so `rm *.o` and
  `cp assets/**/* public/assets/` need no quotes, while `$[...]` bridges
  evaluated values back into command arguments. Expression calls stay
  `fn(a, b)`.
- **Process execution.** `run()` and `cmd()` are explicit structured APIs over
  a direct executable + argv process engine (never `system()`/implicit Bash;
  explicit Bash remains available). Standalone scripting follows normal
  OS-user authority by default, with restrictions opt-in.
- **Native command aliases.** `cp`, `mv`, `rm`, `mkdir` are Nift-native
  aliases with defined wildcard semantics shared with external-command glob
  expansion. Structured-data wildcards follow a separate documented contract.
- **Automation and hooks.** Native build/project scripting and lifecycle hooks
  run before/after build steps with strict filesystem-boundary enforcement.

## Packages

- **Git-native package ecosystem.** Packages are plain Nift `.f` code resolved
  from pinned Git checkouts. The first package is `nift-packages/sqlite`,
  wrapping the local `sqlite3` executable with schema, migration and query
  helpers.
- **No native HTTP primitive in core.** v4.4 deliberately keeps Nift core
  free of TLS/HTTP dependencies; packages and scripts use `run()` with an
  installed client (such as `curl`) when they need HTTP, which keeps Nift core
  small and independent.

## Language

- **Variadics and spread.** User functions, lambdas and eligible native
  functions accept a trailing `...args` heterogeneous array; `...` spreads an
  array into arguments. Recursive and variadic calls are depth-guarded with a
  clean `callable recursion depth exceeded` error.
- **Wildcards.** Shell/script globbing supports `*`, `**`, `?` and character
  classes with a defined contract; structured-data wildcards are a separate
  documented surface.

## Root+path location references

- **Live nested references.** `b := a[0]`, `b := a.key`, `b := a` assign a
  <em>location</em>, not a snapshot: the reference names a root binding plus a
  normalized path and always reflects the live location. Inserting, removing,
  replacing or clearing the parent changes what the reference resolves to, and
  root rebinding is observed through retained references. A reference to a
  location that no longer exists fails safely (`reference target no longer
  exists`); if a value later occupies that location again, the reference
  resumes resolving.
- **Location identity through functions.** Arguments, returns, lambdas,
  higher-order functions, closures, nested calls and recursion all preserve
  location identity, so mutating through a parameter or returned reference
  writes back to the caller's aggregate.
- **`same()` identity.** `same(x, y)` on aggregate references reports whether
  they name the same root binding and path (mutation/rebinding/reappearance
  aware), distinct from structural equality (`==`). Structs and collections
  remain identity-bearing handles.
- **`copy()` and `deepcopy()`.** `copy()` produces a recursively independent
  value; `deepcopy()` is a retained compatibility spelling of the same
  recursive copy.

## Reliability and hardening

- Full sanitizer wall: ASan/UBSan/LSan clean across the adversarial,
  corruption-reproducer, differential and fuzz suites; Valgrind reports no
  memory errors. Retained interior-pointer aliasing is eliminated from the
  engine (root+path locations never dangle under parent reallocation).
- The independent regression suite (65 contract modules) and the full internal
  test surface pass (592 checks), including the cross-platform execution/shell
  and packaging gates.
- JSONIC++ conformance passes the complete pinned JSONTestSuite corpus plus
  the project-owned conformance suite (810/810), with standalone value
  semantics preserved — Nift's location machinery stays entirely Nift-side.

## Compatibility

- v4.3 syntax and behaviour are retained; the legacy evaluator remains the
  correctness oracle and fallback for unsupported constructs, so no public
  behaviour regresses. Structs, collections, schemas, taxonomies, contracts,
  pagination, typed content, `@path`/`@input`/`@dep`, `@for`/`@if`, JSON and
  JSON Schema, the frontend algebra and the native scripting hosts are
  unchanged in their public contract.

## Evidence bounds

- Release-candidate evidence (full internal + independent regression,
  performance certification against the certified AST baseline, sanitizer and
  Valgrind/lifetime checks, JSONTestSuite conformance, website idempotence,
  version/guarantee consistency and cross-platform CI) is recorded in the v4.4
  handovers (`V4.4-EXECUTION-SHELL-PACKAGES`, `V4.4-AST-PERFORMANCE-CAMPAIGN`,
  `V4.4-ROOT-PATH-B2-HANDOFF` and companions). Multithreading (v4.5) and
  concurrency (v4.6) are not part of this release.