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

Verification against the **actual tagged release revisions** (not the current
sibling working trees), enforced by the corrected release-tag synchronization
check:

- **Vendored Jsonic++ v1.0.0 release-content verification — PASS.** The
  vendored tree (`nift/jsonic/`) is byte-for-byte identical to tag `v1.0.0`
  (`88e4736`), including `include/json.h` and all vendored tests/docs, subject
  only to the documented Nift integration machinery exclusion below. The only
  Nift integration material is `src/Json.h` (a wrapper outside the vendored
  tree) and the sync checker script itself.
- **Vendored Minify++ v1.1.3 release-content verification — PASS.** Every file
  in the sync list (`Minify.h`, `Minify.cpp`, `cli/main.cpp`,
  `ReleaseNotes.md`, tests, scripts) is byte-for-byte identical to tag
  `v1.1.3` (`43288d1`).
- **Release-tag synchronization contract (corrected).** The dependency sync
  check previously compared Nift's vendored payload against the sibling
  working tree / HEAD, which is the wrong reference point for a released
  dependency: a sibling normally moves to development work (Jsonic++ 1.0.1-dev,
  Minify++ 1.1.4-dev) immediately after a release. The check now resolves the
  release tag for the version Nift declares it vendors and verifies the payload
  against that tagged content; sibling HEAD/working-tree state is irrelevant,
  and the check never resets, checks out or modifies either repository.
  `scripts/check-nift-sync.sh` itself is documented Nift integration machinery
  (not released dependency content), so it is excluded from the payload-vs-tag
  comparison and instead verified to match between sibling and vendored trees.
  The latest sibling release tag is additionally reported to say whether an
  update is available.
- **Sync/checkpoint result:** `memory-safety-checkpoint-6-sync` is **PASS**
  with sibling checkouts at development heads, reporting:
  `vendored version v1.0.0 / matching tag v1.0.0 / latest v1.0.0 / payload
  matches yes / update no` (Jsonic++) and `v1.1.3 / v1.1.3 / v1.1.3 / yes / no`
  (Minify++). The checker self-test (`tests/check_nift_sync_test.sh` in each
  sibling, wired into the gate) covers HEAD-at-tag, HEAD-ahead-with-dev,
  dirty working tree, payload-differs-from-tag, declared-tag-missing, a newer
  sibling release than the vendored version, and checker-machinery desync.
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
| performance (v4.0.13 vs v4.1, 7 rounds each, same machine) | see "Performance conclusion" below |
| website audit | stale v4.0.x statements fixed; all v4.1 doc examples verified against the executable; internal links/assets validated |

## Performance conclusion

Raw measurements (v4.0.13 vs v4.1, 7 rounds each, same machine, median wall time ratio v4.1/v4.0.13):

| workload | ratio | interpretation |
|---|---|---|
| plain `@input`/`@content` site | **1.005** | release-relevant common path — effectively unchanged |
| nested `@if` scopes | 0.736 | faster |
| large `@json` + `@for` loop | 0.975 | effectively unchanged |
| declaration/assignment heavy | 0.689 | v4.1 feature — diagnostic only |
| structured rebinding | 0.781 | v4.1 feature — diagnostic only |
| fragment heavy | 0.896 | v4.1 feature — diagnostic only |
| schema validation | 0.620 | v4.1 feature — diagnostic only |
| `inject()` heavy | 1.603 | v4.1 feature — diagnostic only (v4.0.13 lacks `inject`; it only rendered literal text) |
| function heavy | 1.100 | v4.1 feature — diagnostic only (v4.0.13 lacks callables) |

**Release-relevant conclusion: no material common-path performance regression**
(`plain` v4.1/v4.0.13 ≈ 1.005×). The workloads above the separator all involve
v4.1 features that v4.0.13 does not implement with equivalent semantics, so
they are reported only as diagnostics and are **not** aggregated into a
headline. The geometric mean across old/new-feature workloads (≈0.898) must
not be read as "v4.1 is ~10% faster overall".

Standing Nift development rule (unchanged): performance regressions are checked
during development; meaningful common-path regressions are investigated and
normally rejected before a checkpoint is accepted.

## Environment limitations

- No dependency-sync limitation remains: `memory-safety-checkpoint-6-sync` is
  green against sibling development heads because the sync check compares
  against the declared-version release tag, not sibling HEAD (see "Dependency
  import verification"). The run portion (`memory-safety-checkpoint-6-run`)
  passes.
- No other gate was unavailable; `strace` and `valgrind` were present.

## Release readiness

Existing v4.0.13 sites behave correctly (byte-identical website dogfood).
The common existing-template path does not materially regress. No memory-safety
finding, no known open correctness defect in the claimed v4.1 surface, and
dependency tracking for `@input`, `inject()`, schemas and computed outputs was
verified. One wording nuance: `const` prevents rebinding while `immut`
establishes a recursively read-only binding/view contract; because v4.1 does
not yet expose member/container mutation syntax, much of that distinction is
currently **latent rather than cosmetic** — it is a deliberate language
contract for what must be rejected once member/container mutation arrives.
Alias behaviour is verified: an `immut` view does not globally freeze storage
reachable through a separate mutable binding (`immut frozen` stays unchanged
when a mutable alias is rebound; the same holds for nested views).
## Follow-up hardening pass (2026-09-15)

Small final evidence/regression-hardening pass after the campaign was accepted.

- **Duplicate-key regression coverage** — added the independent black-box module
  `v41_duplicate_key_smoke.sh` (regression suite) proving Nift rejects duplicate
  object keys in `.nift/config.json`, `@json`-loaded data, `$[x := {...}]`
  expression literals, inline `@json(name){...}` blocks and schema JSON. This
  makes the Jsonic++ integration regression impossible to reintroduce silently.
- **Parse entry-point audit** — re-searched the complete Nift source for direct
  `json::Document::parse` and equivalent parser entry points (including the
  `ParseDiagnostic` overload and any streaming/named-array parse). The only
  direct `json::Document::parse` in Nift-owned source is the `nift_json::parse`
  policy wrapper itself; all eight product parse sites route through the
  Reject-policy wrapper (`load_json_file`, `read_shared_json`, expression
  literals, inline `@json`, engine/context). No unprotected entry point found;
  vendored Jsonic++/Minify++ were not modified.
- **Dependency-sync evidence** — vendored trees verified byte-identical to the
  actual release tags (`v1.0.0` = `88e4736`; `v1.1.3` = `43288d1`), recorded
  separately from the current sibling-head sync check, which is intentionally
  mismatched because the siblings are on post-release development heads.
- **`immut` wording** — refined from "cosmetic"/"observably a non-rebindable
  readonly view" to: `const` prevents rebinding; `immut` establishes a
  recursively read-only binding/view contract whose distinction is currently
  **latent** (no member/container mutation syntax yet) rather than cosmetic.
  Alias behaviour re-verified: an `immut` view does not globally freeze storage
  reachable through a separate mutable binding (top-level and nested views).
- **Performance wording** — release-relevant conclusion is now stated as "no
  material common-path regression" (`plain` ≈ 1.005×); new-feature workloads
  are reported separately as diagnostics and the geometric mean is not used as
  a headline.

Walls rerun after this pass: full independent regression suite (32/32 modules),
duplicate-key module, v4.1 adversarial module, `make test-jsonic`,
`make test-json`, `make test-json-schema`, `make test-json-schema-integration`,
`make test`, and `git diff --check` (all clean). No executable code changed in
this pass (docs/tests only), so the ASan/UBSan/Valgrind walls were not rerun.

## Release-candidate sync-check correction (2026-09-15)

Final narrow repair before freezing the v4.1.0 release candidate: the sibling
dependency sync check compared Nift's vendored payload against sibling `HEAD`,
which is wrong for a released/versioned dependency. The comparison target is
now the sibling release tag for the version Nift declares it vendors, resolved
through Git tags with semantic ordering and stable-release filtering; the
latest sibling release is reported separately for update availability. The
check never resets/checks out/modifies either repository, never falls back to
sibling `HEAD`, and fails clearly and distinctly for "declared tag unavailable"
vs "payload differs from tag". The checker script itself is a documented Nift
integration exclusion from the payload-vs-tag comparison (verified to match
between sibling and vendored trees instead). Regression coverage (per sibling,
wired into `memory-safety-checkpoint-6-sync`) exercises sibling-HEAD-at-tag,
HEAD-ahead-with-development, dirty working tree, payload-differs-from-tag,
declared-tag-missing, newer-sibling-release-than-vendored, and checker desync.
Markup++ keeps its candidate-based HEAD sync (its documented contract); it is
not a released dependency Nift pins in the same way and currently matches its
v0.1.0 release tag. Walls rerun after this change: `memory-safety-checkpoint-6-sync`
PASS (checks + self-tests), `make test-jsonic` / `test-minify` PASS, `make test`
PASS, independent regression suite 32/32 PASS. The sibling repositories
themselves were not reset or moved to make the check pass.
