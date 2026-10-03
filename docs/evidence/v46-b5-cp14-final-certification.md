# v4.6 Batch 5 CP14 — final Batch 5 aggregate / release-readiness certification

Status: complete. Deliberately boring final certification: composes the
CP10–CP13 gates, audits for stale flat-lock assumptions, and certifies the
complete Batch 5 package graph surface. No new package-manager features.

## Final Batch 5 contract inventory (frozen)

- **Graph identity:** one node per package name; canonical source + exact
  commit/local.
- **Edge identity:** declaring parent, declared source spelling, requested ref.
- **Lock:** v2 complete reachable graph; deterministic serialization; strict
  validation; v1 remains readable and migrates on a successful mutating op.
- **Resolver:** deterministic (lexicographic roots/edges/paths); owner-relative
  local sources; floating-ref memoization; deterministic conflicts/cycles.
- **Commands:** add/install/update/update&lt;root&gt;/remove are graph-aware
  with one atomic `PackageTransaction` publication.
- **Provenance:** node identity (name + canonical source + exact commit/local),
  never an arbitrary `requested` projection.
- **Recovery:** v1 + v2 journal payloads; journal version remains 1.
- **Inspection:** `nift packages [name] [--json]`; manifest + lock only.
- **Local qualification:** live path identity, not content reproducibility.
- **Self-cycle:** always invalid, no exception.

## Mechanical schema/contract audit

Every remaining legacy-looking use was classified:

| Use | Classification |
| --- | --- |
| `LockEntry.requested` in `LockView` (transaction coverage) | intentional v1 compatibility (ownership equality uses source+commit) |
| `LockEntry.requested` in query output | intentional (edge requirement display) |
| `validate_lock` (v1 size-equality) in CLI add/remove/install | intentional v1 compatibility (validate a v1 lock before migration) |
| `load_lock` (v1 loader) | not used in production paths |
| arbitrary first-parent `requested` in provenance | none (provenance uses node identity; the field no longer exists) |
| installed-package reads to reconstruct graph truth | none (query/explanation use manifest + lock only) |

No stale production defect was found.

## Full Batch 5 aggregate

`tests/v46_b5_cp14_final_certification.sh` (`make test-v46-b5-cp14`) composes
the CP10–CP13 gates plus the package metadata/refs/hardening/callable/module
export walls, relative-import ownership, worker package ownership, import/
module projection, Batch 4 package/import recoverability, package language,
transaction + v2 recovery smokes, the adversarial graph fixtures, the CP13
reproducibility repeat, and the recovery-epoch resource wall.

**Final aggregate: 1933 `PASS|passed` lines, 8 explicit sqlite SKIP lines
(34 case-insensitive skipped markers), full gate exit 0.** The sqlite skips
are the documented dirty-worktree exclusions (unchanged; the sqlite repo is
not modified).

## Sanitizer

CP14 found no production defect; the production package/runtime delta was
already ASan/UBSan certified in CP12 (`nift-sanitize` running the CP12
graph-command E2E plus the import/provenance/worker-ownership walls with
leak detection + `halt_on_error`). CP13's production delta is query-side and
green; no sanitizer rerun was required.

## Final adversarial smoke + reproducibility repeat

The CP12 graph-command E2E, CP13 determinism suite, and CP13a certification
suite (compatible diamond, different-commit/different-source/targeted-update
conflicts, self/multi-node cycles, missing transitive local source, malformed
transitive manifest, name mismatch, locked install with upstream moved, orphan
cleanup, store-less query) all pass, and the CP13 reproducibility fixture is
re-run in the aggregate with byte-stable lock/human/JSON output.

## Cross-platform static audit

No Batch 5 regression in Windows drive/root handling, separators, local
absolute path identity, symlink/reparse containment, deterministic JSON
newline/order, or Git source canonicalization. CP13 introduced no new
platform-specific normalization; Batch 3/CP10/CP13 evidence remains
authoritative.

## Documentation / evidence consistency

`docs/packages/README.md`, the Batch 5 design/handover docs, the CP10–CP13
evidence, the live website, and `nift commands` agree on the v2 lock, exact
Git commits, local `"local"` identity, transitive graph, self-cycle rejection,
targeted-update semantics, v1 migration, `nift packages` query, optional
`--json`, and the offline/locked qualification. No contradictions or stale
claims were found; the website is already published and clean
(stage `33a7779`, public `95e57e0`).

## Version / public API scope

CP10–CP13 changed no public ABI headers (`include/` is byte-identical across
the Batch 5 range since the `06ed9bb` design boundary) and the exported
`nift_*` C ABI symbol surface still matches the frozen baseline. The only
public additions are the intentional package CLI surface (`nift packages`).

## Performance sanity

Ordinary non-package site build 0.05s, script startup 0.00s, package query on
a representative graph 0.00s — no material regression observed.

## Independent final review

The complete Batch 5 diff (`06ed9bb` design boundary → CP14 candidate) was
reviewed for: mutation before complete graph resolution (none; one atomic
commit), non-atomic publication (none), stale v1-only parsing (none in
mutating/runtime paths), arbitrary requested projection (none), graph edge
loss (none), name/source identity confusion (none), nondeterministic ordering
(none; lexicographic), local source CWD leakage (none; owner-relative), orphan
miscalculation (none), shared child installed/removed incorrectly (none),
self-cycle exception (none; always invalid), recovery divergence (none;
v1+v2 journals, version 1), query dependence on installed state (none),
package failures becoming catchable runtime Errors (none; command failures),
and unrelated CLI/language regressions (none).