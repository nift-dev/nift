# v4.6 Batch 5 CP13 — determinism, reproducibility and graph-query certification

Status: complete. Certifies the v2 graph model and adds the minimal graph
inspection surface. No new resolver semantics, no cache/registry/SemVer work.

## Graph inspection model

The authoritative data source for any graph explanation is the **root
manifest + the v2 lock graph**; installed package manifests are never opened.
`nift packages [name] [--json]` answers "why is this installed / which paths
require it / what did each parent request / which node identity satisfied it"
and works even when `.nift/packages/` is absent, provided the manifest and a
valid v2 lock exist.

## Query surface

`src/PackageGraphQuery.h` (new) + `src/CLI.cpp`:

- `nift packages` — whole-graph summary: every package (sorted) with its
  resolved source + commit and its required-by paths.
- `nift packages <name>` — per-package explanation: resolved source, commit,
  and every root-to-package path with the **source spelling + requested ref**
  each parent declared. Distinct parent requirements are never collapsed even
  when they resolve to the same commit.
- `nift packages --json` — semantically structured output: the lock graph
  plus per-package `paths` arrays (`nodes`, `source`, `requested`). Not
  a dump of internal structures. Human-readable output remains the default.
- A **valid v1 lock is rejected by the query** with a clear "legacy/direct-only;
  run `nift install` to migrate" message (the query does not pretend a
  direct-only lock has a transitive graph, and never rewrites it).
- The query validates the graph with the existing CP10 machinery
  (`validate_graph_lock` via `enumerate_graph_paths`): malformed lock, missing
  target, orphan, cycle, and invalid source/commit coupling are rejected. No
  graph-validation logic is duplicated in CLI code.

## Deterministic path enumeration

- Roots processed lexicographically; child edges lexicographically (std::map
  order); paths sorted lexicographically by their node-name sequence — the
  same order contract as CP11.
- Same manifest+lock bytes always produce byte-identical human and JSON query
  output (unit-tested, including a diamond `root -> {a -> b, c -> b}` with both
  paths in stable order and both distinct edge requirements).

## Reproducibility certification

Two independent project directories from equivalent inputs (a shared Git graph
`root -> {a -> b, c -> b}` with b pinned at an exact commit) produce
**byte-identical v2 lock, human query, and JSON query output**, with exact Git
node identities, even when:

- the root dependency insertion order differs (a then c vs c then a),
- the checkout/install temp location differs,
- the process CWD differs for the query.

## Local-package qualification

Local dependencies keep `commit == "local"`, deterministic canonical
path/graph identity, and **live contents** — explicitly not
content-reproducible across machines. The query output shows the resolved
local path as the node source, making the local nature obvious.

## Source-spelling / canonical-source fidelity

The query distinguishes the **requested edge source** (the parent's declared
spelling, e.g. `github:org/b`) from the **resolved node source** (the canonical
`https://github.com/org/b.git`), so canonicalization history is not lost.

## Conflict and cycle determinism

Equivalent conflicting graphs in different declaration orders yield stable
path diagnostics (`existing path`, `incoming path`, existing/incoming
source+commit) — certified, not redesigned. Self-cycles (`a -> a`) and
multi-node cycles remain deterministic failures with no exception.

## Offline / locked install

Locked install uses the pinned exact commits as the identity authority and
does not re-resolve `latest`/`latest-tag`/branch refs; a lock whose `b` is
pinned at commit B1 stays at B1 even after the source repository advances to
B2 (the test asserts the installed `b` is NOT the advanced commit). Physical
acquisition of an absent package may still require its exact source contents.

## No-op stability

`install` with a complete valid graph and a valid installed store is a
semantic no-op: the lock bytes are unchanged.

## Failure immutability

A conflicting `add` (a new root requiring `b@latest -> B2` while an existing
root pins `b@B1`) fails with both paths and leaves the manifest and lock
byte-identical (CP12 fixtures composed rather than duplicated).

## Add/remove/update determinism

The CP12 E2E already asserts deterministic operation/message ordering and
byte-identical lock output for add/install/update/targeted-update/remove,
including a diamond/shared child and orphan cleanup; CP13 composes those gates.

## Cross-platform audit

Local absolute path identity uses `std::filesystem` normalization (Windows
drive/root-name handling already covered by the existing package/path
hardening walls). No contradiction with already-certified containment behavior
was found; no new resolver semantics were introduced.

## Documentation

`docs/packages/README.md` updated to describe the transitive dependency graph,
the v2 lock schema at a user-facing level, exact Git commits, the local
package qualification, update semantics, the `nift packages` query surface,
and v1 compatibility/migration. No v4.6 release claim.

## Wall

```sh
make test-v46-b5-cp13
```

runs the Batch 4 + CP10/CP11/CP12 aggregate then the CP13 wall: the graph-query
unit test, the determinism/reproducibility/no-op/failure-immutability python
suite, the CP10/CP11/CP12 units + recovery + graph-command suites, and a query
sanity check that the command works without the installed store.

Gate: **961 `PASS|passed` lines**, 4 explicit sqlite SKIPs (17 case-insensitive
"skipped" markers), full gate **exit 0**.