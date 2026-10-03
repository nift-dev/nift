# v4.6 Batch 5 CP12 — real provider + command/transaction integration

Status: complete. The resolver becomes the package manager: `nift add`,
`install`, `update`, `update <name>`, and `remove` now resolve the full
transitive closure, stage every required store change, emit a v2 graph lock,
and commit atomically through the existing journaled `PackageTransaction`.
Runtime package provenance migrates to v2 node identity. Contract sources:
`docs/handover/V4.6-BATCH-5-DEPENDENCY-GRAPH-DESIGN.md` and
`docs/handover/V4.6-BATCH-5-GRAPH-CONTRACTS.md`.

## Real provider + atomic acquisition

`src/PackageGraphCommands.h` (new) provides the shared git/local acquisition
primitives (`git_run`, `git_checkout`, `stage_local_package`,
`detached_git_head`) and the real `ResolutionProvider` (stages git packages
into the transaction staging on first resolution, memoized per
(canonical, requested) by the resolver; re-clones when the requested ref
differs so a two-ref conflict is never missed), `existing_lock_graph`,
`prune_locked_graph`, and `acquire_graph`.

`acquire_graph` resolves the complete graph, computes the deterministic
operation plan (reuse / replace / remove-orphan) by comparing the old graph,
the desired graph, and the actual store slot identity (canonical source +
exact commit/local), stages every required store change into the transaction
staging root, and produces the desired manifest + v2 graph lock documents. It
never publishes anything; `PackageTransaction::commit` is the single atomic
publication. A graph is one transaction: if any descendant fails
(resolution/fetch/manifest/name/source/commit compatibility/cycle/conflict/
staging), nothing is published.

## Commands (src/CLI.cpp)

- `nift add SOURCE [--ref=REF]`: stages the new package to discover its name,
  adds the root requirement, resolves the complete closure, commits one v2
  lock. Adding a root whose transitive closure conflicts with an existing root
  fails with both dependency paths and leaves the project byte-identical.
- `nift install`: with a valid v2 lock, LockedInstall mode is authoritative
  and offline (no floating re-resolution); missing slots are re-acquired at
  their exact commits. With a v1 lock it migrates, preserving the locked
  direct commits (`root_locked_commits`) while resolving the transitive
  closure. With no lock it resolves from the manifest.
- `nift update` (full): re-resolves all roots + closures (memoized), reconciles
  shared nodes globally, removes unreachable nodes.
- `nift update <root>`: restricted to root/direct deps; re-resolves the target
  root + its reachable closure, preserves unrelated roots at their locked
  identities, reconciles shared nodes globally, fails atomically on conflict.
  A transitive-only name is rejected.
- `nift remove`: recomputes reachability from the remaining manifest; retained
  nodes keep their locked identities (v2 prune) or re-resolve preserving the
  v1 direct commits; orphaned transitive nodes are removed deterministically.
  Orphans are determined from the old graph vs the newly reachable graph, never
  from filesystem contents.

## v1 -> v2 migration

- Read-only/runtime operations accept a valid v1 lock without rewriting it.
- A successful graph-aware mutating operation (`add`/`install`/`update`/
  `remove`) atomically emits a v2 graph lock. A failed operation does not
  migrate. `install` against a v1 lock preserves the locked direct commits
  (`root_locked_commits`), then discovers the transitive closure.
- `PackageTransaction::commit`/`apply_journal` allow a metadata-only
  (empty-operation) publication for no-op installs; a journal whose operations
  were stripped is still rejected by the coverage check (no mutation).

## Runtime package provenance migration

`PackageProvenance` now carries node identity `{project_root, package_root,
name, canonical source, exact commit/local}` (the v1 `requested` field is
removed; ownership equality never uses an edge's requested ref).
`package_graph::lock_node_identity` reads a v1 or v2 lock and returns the node
identity for a name; both the package import (`ParserScript.cpp`) and the
stale-ownership check (`Parser.cpp` `acquire_package_read`) use it. For v1
locks the identity is derived from `source + commit/local`, ignoring
`requested`. Remote generation changes remain a controlled stale-provenance
failure; same local identity continues following live local contents.

## Self-referential dependency note

A package declaring a dependency on itself at the same canonical source is
treated as a self-provided no-op (the `sqlite` package's legacy
self-referential manifest keeps working) rather than a cycle. A genuine
multi-node cycle (`a -> b -> a`) and a different-source self-edge remain
rejected. This is a narrow, documented exception discovered during real
integration.

## Wall

```sh
make test-v46-b5-cp12
```

runs the Batch 4 + CP10 + CP11 aggregate then the CP12 wall: CP10/CP11 lock +
resolver units, the v2 recovery smoke, `tests/v46_b5_cp12_graph_commands.py`
(real local + git `file://` transitive fixtures covering add/install/update/
targeted-update/orphan/v1->v2/offline/conflict rollback/runtime import), and
the existing package transaction/metadata/refs/hardening/callable/recoverability
walls.

Regression gates for the full CP12 review: package metadata/refs/hardening/
callable/provenance, Batch 3 relative-import/provenance, Batch 4 package/import
recoverability, worker package ownership, package transaction/recovery smoke,
v2 recovery smoke, recovery-epoch resource wall, and the aggregate
CP1..CP11 gates.