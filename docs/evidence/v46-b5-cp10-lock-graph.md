# v4.6 Batch 5 CP10 — deterministic v2 package graph lock representation

Status: complete. Representation / parser / structural validation /
deterministic serialization for the v2 graph lock ONLY. No resolver, no
acquisition traversal, no command wiring. Current v1 lock behavior and runtime
package provenance are preserved exactly. Contract source:
`docs/handover/V4.6-BATCH-5-GRAPH-CONTRACTS.md`.

## What was added

- `src/PackageGraphLock.h` (new): `GraphRequirement {source, requested}`,
  `GraphNode {source, commit, requirements}`, `GraphLock {packages}`,
  `detect_lock_format`, `parse_graph_lock`, `validate_graph_lock`,
  `graph_lock_document`/`graph_lock_text`, `local_source_absolute`.
- `src/PackageMetadata.h`: moved the two source helpers from CLI.cpp into
  `package_metadata::git_source` / `package_metadata::source_is_local_path`
  (single canonical Git normalizer, reused — no second normalizer).
- `src/CLI.cpp`: package commands now call the shared helpers; behavior
  unchanged (they still read/write v1 locks).
- `tests/package_graph_lock_unit.cpp` (new standalone unit test).
- `tests/v46_b5_cp10_lock_graph.sh` + `make test-v46-b5-cp10`.

## Exact v2 schema (frozen)

```json
{
  "lockfileVersion": 2,
  "packages": {
    "<package-name>": {
      "source": "<canonical/resolved source identity>",
      "commit": "<40/64-hex exact git commit> | \"local\"",
      "requirements": {
        "<child-package-name>": {
          "source": "<requirement source spelling>",
          "requested": "<requirement ref>"
        }
      }
    }
  }
}
```

- Strict exact-field validation (unknown fields fail). `lockfileVersion` must
  be the number `2`; `packages` is an object; every key is a valid package
  name; node `source` is a non-empty safe string; node `commit` is a valid
  exact Git commit or the literal `local`; `requirements` is an object whose
  keys are valid package names and whose edges have exactly `source` +
  `requested`. No semver ranges or new ref grammar.

## Format detection

`detect_lock_format`: top-level package map -> v1; top-level
`lockfileVersion` + `packages` -> v2; anything else (unknown top-level field,
v1 map plus `lockfileVersion`/`packages`, v2 plus stray flat entries,
non-object) -> invalid/mixed, rejected.

## Structural graph validation (self-contained)

`validate_graph_lock` never opens installed packages. Requires:
every root manifest dependency names a node; the closure is closed under
edges (no missing edge targets); every node is reachable from a root
dependency (no orphans); no self or multi-node cycle. Cycle diagnostics carry
the deterministic path (`a -> b -> a`). Root `ref -> commit` equivalence is
NOT proven structurally (that is CP11/CP12 resolver work); no network activity
in validation.

## Deterministic serialization

Stable top-level field order (`lockfileVersion`, then `packages`); packages
sorted lexicographically by name; requirements sorted by dependency name;
stable node/edge field order; compact JSON via `dump(2)` + newline. Insertion
order is irrelevant: the unit test builds equivalent graphs in two orders and
requires byte-identical output, plus a parse -> serialize -> parse round-trip.

## Edge fidelity and identity

- A node's `commit`/canonical `source` are independent of its parents' edge
  `{source, requested}`. `a -> b (latest)` and `c -> b (v1.2.3)` both
  round-trip as distinct edges on the same `b` node.
- Two differently named nodes sharing the same source + commit remain two
  distinct nodes (package name is graph identity, not the source tuple).

## Local source ownership

`local_source_absolute(owner_dir, source)` resolves a relative local source
against the declaring manifest directory (must be absolute; process CWD has no
effect). `./b` and `../sibling` are supported; `../sibling` is permitted
acquisition (distinct from runtime package-root confinement). Unit-tested with
a CWD change.

## v1 compatibility / dormant migration

- Existing v1 locks parse and validate unchanged (`parse_lock` /
  `validate_lock`, size-equality contract kept for v1).
- The shell wall proves `nift add` still emits a v1 lock (no
  `lockfileVersion`), a read-only import does not rewrite it, and the v1
  package transaction/recovery wall remains green.
- Migration stays dormant: no command writes v2 yet. The first successful
  graph-aware mutating operation (CP12) emits v2 atomically.

## PackageTransaction recovery compatibility

The journal version and the lock-format version are separate concerns; the
journal version is unchanged. In CP10 all journals carry v1 payloads (commands
emit v1), so recovery is untouched and the existing v1 recovery walls pass.
CP12 must extend `apply_journal`'s `parse_lock`/`validate_lock` to understand
a v2 graph payload once commands begin writing v2 (the journal stays version
1). Documented integration point; no half-v2 state is created.

## Runtime provenance audit (CP12 integration notes)

Every consumer of `LockEntry.requested` was audited:

- `src/CLI.cpp` (add/install/update/remove): direct-only v1 command
  read/write; stays v1 through CP10; flips to v2 at CP12.
- `src/PackageTransaction.cpp` `lock_equal` (line ~379): compares
  `{source, requested, commit}` for journal coverage; v1 until CP12.
- `src/Parser.cpp` (~529-532): runtime package provenance validation compares
  `found->second.requested != provenance->requested` against the frozen
  provenance. **This is the key integration point**: with v2 there is no
  single node-level `requested`.
- `src/ParserScript.cpp` (~451-457): package import builds `PackageProvenance`
  from `locked->second.source/requested/commit`. Same integration point.

**CP12 required change (not done in CP10):** runtime provenance must switch
from a single ambiguous `requested` to node identity `{package name, canonical
source, exact commit/local}` when v2 locks become command-written. CP10
deliberately does NOT pick "the first parent's requested ref" as a shortcut;
behavior is preserved unchanged because commands still emit v1.

## Wall

```sh
make test-v46-b5-cp10
```

runs the Batch 4 aggregate then the CP10 wall: the graph-lock unit test and the
v1-compatibility checks (add still emits v1, read-only no-rewrite, v1
transaction recovery green).

Existing package regression walls re-run for CP10: package metadata, package
refs, package hardening, package transaction/recovery, package
callable/provenance, Batch 3 package provenance/import ownership, Batch 4
package/import recoverability — all green.