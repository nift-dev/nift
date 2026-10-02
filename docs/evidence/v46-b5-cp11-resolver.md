# v4.6 Batch 5 CP11 — deterministic transitive package graph resolver

Status: complete. Compute-only resolver algorithm. No command wiring, no
store/manifest/lock/journal mutation, no runtime provenance change, no new
recoverability codes. The full algorithm is exercised with an in-memory fake
`ResolutionProvider`, so conflict/cycle/floating-ref determinism is tested
without git/network timing.

## Resolver architecture

`src/PackageGraphResolver.h` (new):

- `Requirement{name, source spelling, requested ref, owner_dir}` — one
  package requirement edge.
- `ResolutionProvider` (read-only acquisition abstraction):
  - `resolve_ref(requirement, commit, error)` — resolve a ref to an exact
    commit (never called in LockedInstall mode; tests assert zero calls).
  - `load_package_manifest(requirement, canonical_source, commit, dir,
    manifest, error)` — read-only manifest load returning the absolute owner
    directory for that package's own relative local dependencies.
- `resolve_graph(root_manifest, root_dir, mode, existing_lock, target_root,
  provider, outcome, error)` producing a `ResolveOutcome{GraphLock graph,
  used_locked_identity}`.

The graph algorithm (DFS over sorted requirements) is entirely separate from
git/filesystem/transaction mutation. Unit tests drive it with a fake provider.

## Identity contract (exact)

- One node per package name. A requirement targeting an already-resolved name
  is compatible iff canonical source AND exact commit agree; otherwise a
  deterministic conflict error. Different requested refs resolving to the same
  source+commit share one node; both edges are recorded independently
  (`a -> b latest`, `c -> b v1.2.3`). Nodes are never merged by source:
  `foo@S` and `bar@S` remain two nodes.

## Deterministic traversal

Roots are processed lexicographically by name; each package's child
requirements are processed lexicographically by name (std::map order). No
dependency on unordered containers, filesystem enumeration, git output, or
discovery timing. Equivalent inputs/providers produce byte-identical
serialized graph locks (tested).

## Floating-ref memoization

Within one resolution, the memoization key is `(canonical source, requested
ref)` plus the local-vs-Git distinction implicit in the canonical form
(absolute path for local, `git_source` URL for Git). The provider is asked to
resolve `S/latest` at most once even when two parents require it (tested);
a provider whose answer would change on a second call never observes a second
call. Different requested refs are separate memoization entries.

## Local dependency ownership

Relative local sources resolve against the declaring manifest directory
(`owner_dir`), never the process CWD. The fixture
`/repo/root` (a = ./packages/a) and `/repo/root/packages/a` (b = ../b) yields
a -> `/repo/root/packages/a`, b -> `/repo/root/packages/b`. `../sibling` is
permitted. Changing the process CWD during the test produces an identical
graph (tested). Runtime module/package confinement is not applied to
acquisition-source traversal.

## Manifest identity validation

Every resolved package manifest must agree with the edge target name; a
mismatch is a deterministic fatal resolver failure. Existing strict package
manifest validation is retained (the provider returns already-validated
manifests; malformed transitive manifests fail deterministically).

## Cycle detection

Cycles fail deterministically during resolution with a stable path
(`dependency cycle: a -> b -> a`). A shared DAG node reached through two
parents is not a cycle (only the active recursion stack triggers the check).

## Conflict diagnostics

Deterministic diagnostics carry both paths and identities:
`dependency conflict for package: b; existing path: ...; incoming path: ...;
existing source/commit; incoming source/commit`. This supports the CP13 graph
explanation/query work.

## Modes

- **LockedInstall** (offline): with a complete valid v2 lock, `validate_graph_lock`
  runs against the root manifest and the locked identities are authoritative.
  No `resolve_ref` and no manifest loading occurs (test asserts
  `resolve_calls_total == 0`). Missing locked nodes and lock/manifest edge
  mismatches are rejected. Physical install availability remains CP12.
- **ResolveUpdate**: requirement refs are resolved through the provider with
  per-transaction memoization.
- **TargetedUpdate**: `target_root` names a root/direct dependency to
  re-resolve; unrelated root nodes and their reachable closure are preserved
  at their locked identities (never re-resolved); shared nodes are reconciled
  globally and a conflict fails atomically. Both a compatible-shared-node and
  a conflicting-shared-node fixture are proven, and the unrelated root is
  preserved (only the target's ref is resolved).

## Resolver result / acquisition plan

The essential output is the complete deterministic `GraphLock`. Comparing it
to the physical store to produce acquire/replace/reuse/orphan-candidate
slots is deliberately deferred to CP12 (needs the existing lock + store state
through a read-only provider); CP11 keeps graph truth separate from physical
store state.

## Error boundary

Resolver errors are package-manager/configuration failures (malformed
manifest, invalid requirement, ref-resolution failure, name mismatch,
conflict, cycle, missing locked node, invalid locked identity). None become
Batch 4 catchable language Errors; no recoverable Diagnostic codes were added.

## Wall

```sh
make test-v46-b5-cp11
```

runs the Batch 4 aggregate + CP10 gate then the CP11 wall: the resolver unit
test, the CP10 lock/recovery unit tests, and a check that ordinary commands
still emit v1 locks (no command wiring).

Resolver unit matrix (fake provider): zero/one/multi-level/branching/diamond
graphs, shared-different-refs compatible, different-commit conflict,
different-source conflict, same-source two-names, self-cycle, multi-node
cycle with deterministic path, manifest name mismatch, malformed transitive
manifest, owner-relative local + ../sibling + CWD independence, Git shorthand
canonicalization reuse, floating ref resolved once, changing-provider second
answer never observed, separate refs separate entries, insertion-order
independence with byte-identical serialization, LockedInstall zero-resolve
offline contract, missing locked node, lock edge mismatch, targeted-update
compatible/conflicting shared node, and unrelated-root preservation.