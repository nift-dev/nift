# v4.1 independent certification evidence

Independent review → adversarial testing → repair → re-certification campaign
run against the Nift v4.1 implementation (2026-09-15). The supplied workspace
contained Nift (`cfef034`), the regression suite (`e449f3d`), website source
(`f8a0080`) and generated website (`e180765`).

## Repository heads recorded at campaign start

| repo | head | branch |
|---|---|---|
| nift | cfef034 | main (clean) |
| nift-regression-suite | e449f3d | main (clean) |
| nift-dev.github.io | f8a0080 | stage (clean) |
| jsonic / minify | not git repos (source trees) | — |

No history was rewritten; the checkpoint series is preserved.

## Dependency import verification

- Jsonic++ embedded source is the pinned **v1.0.0** release state (`88e4736`).
  The only difference vs the upstream 1.0.1-dev head is the absence of the
  `json::version` constant (added post-release), so no 1.0.1-dev code leaked.
- Minify++ embedded source is the pinned **v1.1.3** release. The only
  1.1.4-dev change (executable identity `1.1.4`) is absent; `Minify.h` and
  `Minify.cpp` are byte-identical to the 1.1.4-dev head.
- Embedded test walls: `make test-jsonic` PASS, `make test-minify` PASS,
  `make test-json` / `test-json-schema` PASS.

## Defects found and fixed

All covered by `tests/v41_certification_adversarial.sh` (fails on pre-fix
binary) and mirrored in the regression suite.

1. **Mutation-suppression flag leak** — a function that declared a local or
   mutated an outer binding, a fragment with a body declaration, or `inject()`
   nested inside a larger expression silently suppressed the enclosing
   `$[...]` output. `last_expression_mutation_` is now depth-scoped and
   saved/restored around callable bodies and `inject()`.
2. **Conditional `@return`** — `@fn` always took the last `@return(expr)`;
   `@return` is now a directive inside function bodies that unwinds through
   nested `@if`/`@for`/`@input`.
3. **`@for` loop-binding shadowing** — loop variables now shadow an outer
   v4.1 binding of the same name.
4. **Undefined callables** — `$[not_a_function(...)]` now errors with
   "undefined callable" instead of rendering literally.
5. **Structured binding array indexing** — `$[x[1]]` / `$[o.a.b[0]]` now walk
   `.member` and `[index]` segments.
6. **Structured-literal callable/validate arguments** — `parse_parameters`
   preserved quotes only for exactly-quoted parameters; `{…}/[…]` args keep
   their quotes and JSON escapes.
7. **Unbounded callable recursion** — a 64-level call-call depth bound (matching
   the parse-depth guard) fails cleanly and prevents ASan stack overflow.

Historical regressions introduced by the dependency imports and reconciled:

8. **Duplicate-key strictness** — Jsonic++ v1.0.0 defaults to preserving
   duplicate object keys; Nift historically rejected them. All Nift parse
   sites now use `nift_json::parse` (DuplicateKeyPolicy::Reject), restoring
   v4.0.13 behavior for config, tracked state, `@json` data, schemas, inline
   and expression literals.
9. **Minify++ v1.1.3 JS trailing-semicolon elision** — `const q = 1 ;` now
   minifies to `const q = 1` (intended, ASI-safe v1.1.3 behavior); the
   integration tests were reconciled to the certified v1.1.3 output.

Also fixed: a stale v4.1 version-consistency fixture (assertions advanced to
4.1.0 without updating the fixture trees).

## Gates and results

| gate | result |
|---|---|
| Nift `make test` (aggregate CLI wall) | PASS |
| `make test-all` (test + embed + bindings + build boundary) | PASS |
| engine walls (loaders, source-read, pathto, project, reload, pagination snapshot, project-state, project-host, public-header, host-seam, pagination equivalence) | PASS |
| concurrency + TSAN (engine-concurrency, reload, pagination) | PASS |
| scenario 11 / zero-mutation / repair campaign | PASS |
| ownership/concurrency, BH adversarial set (crash-recovery, filesystem-boundary, complexity, parser-value, incremental-state, init-functional-truth) | PASS |
| independent regression suite | 31/31 modules PASS (incl. historical ruthless contract) |
| ASan/UBSan (sanitizer smoke, pagination sanitize, memory-safety checkpoints 0/3/4-watch/4-large/6-run) | PASS |
| Valgrind (watch endurance + full v4.1 adversarial wall) | clean, no leaks |
| parser fuzz gate (checkpoint 9, 1217 cases) + v4.1 feature fuzz (1300 cases, 3 seeds) | PASS |
| website dogfood (75 pages) and benchmark-site (3 pages) | byte-identical vs v4.0.13; no-op = no-op; single-edit rebuild = 1 file |
| performance (v4.0.13 vs v4.1, 7 rounds each) | plain 1.005, nested-scopes 0.736, large-loop 0.975, assign 0.689, rebind 0.781, fragment 0.896, validate 0.620; geometric mean 0.898 |
| website audit | stale v4.0.x statements fixed; all v4.1 doc examples verified against the executable; internal links/assets validated |

## Environment limitations

- `memory-safety-checkpoint-6-sync` cannot run: sibling `jsonic`/`minify`
  checkouts are at development heads (1.0.1-dev / 1.1.4-dev), not the pinned
  release states. The run portion (`memory-safety-checkpoint-6-run`) passes.
- No other gate was unavailable; `strace` and `valgrind` were present.

## Release readiness

Existing v4.0.13 sites behave correctly (byte-identical website dogfood).
The common existing-template path does not materially regress. No memory-safety
finding, no known open correctness defect in the claimed v4.1 surface, and
dependency tracking for `@input`, `inject()`, schemas and computed outputs was
verified. The one documentation nuance is that `immut` is observably a
non-rebindable readonly view (no member-level mutation syntax exists), so the
website's "deeply read-only through that binding" wording is descriptive rather
than a separately enforceable guarantee.