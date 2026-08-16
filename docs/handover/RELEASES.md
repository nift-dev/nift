# Nift release and publication handover

Package-manager recipes, GitHub release workflows, store credentials, artifact
names, and the existing Flathub update path are documented in `PACKAGING.md`.
This document owns release readiness; `PACKAGING.md` owns how an approved release
is packaged and published.

## Authority and current state

The executable currently reports `Nift v4.0.0`. The retained rewrite checkpoint
identity remains documented in project history and release notes, but it is not
part of the public product version. Exact tag, artifact, and public release
conventions must follow `PACKAGING.md` and actual Git/release evidence.

A website content checkpoint, regression-suite checkpoint, and executable version
are distinct identities. Do not synchronize version numbers mechanically.

## Checkpoint versus release

```text
validated checkpoint
    coherent development baseline, not public by implication

release candidate
    validated checkpoint undergoing packaging/publication checks

release
    deliberately published artifact after approval
```

Historical ZIP checkpoints were a file-transfer mechanism. With direct Git
access, prefer commit/status provenance and repeatable scripts. Do not recreate
ZIP-heavy ritual unless an actual release artifact requires it.

## Release-candidate validation

Proportionately include:

1. Clean source build with the intended release compiler/options.
2. Full implementation-local test set.
3. Full external contract against the candidate executable.
4. Relevant ASan/UBSan and platform checks.
5. Current scaling, performance, and memory guards.
6. Exact embedded Minify++ synchronization and its relevant standalone gates.
7. Build the Nift website with the exact candidate binary.
8. Validate representative templates/downloadable examples where relevant.
9. Reconcile README, docs, website, AI context, release notes, decisions, and
   production roadmap.
10. Build the actual package/archive, extract it freshly, build/use it, run the
    external suite against it, and verify version/help/license/expected files.
11. Inspect repository state for generated/debug residue.

Repository tests passing does not prove a release archive is usable.

## Website publication

The Nift website source is a separate repository on its authoritative `stage`
branch in the current checkout. Its nested `public/` is a separate generated Git
checkout used for built-site state. Exact deployment commands must be documented
in that website's `HANDOVER.md` after verification. Never hand-edit generated
output as the source of truth.

Building locally is authorized as validation. Pushing the generated branch or
deploying publicly requires approval.

## Version and notes

User-visible behavioral changes, correctness fixes, and language capabilities may
justify version/release-note changes. Tests or prose alone do not automatically
require a binary version bump. Exact versioning policy remains partly unresolved;
follow established Git/release evidence and ask before assigning a public version.

## Release report

Record exact source/suite/site identities, commands, outcomes, environment where
material, package contents, known limitations, and publication status. Separate
facts from interpretation and avoid universal performance claims from one host.

For packaged releases, also record artifact checksums, the installed package
version tested from each store, the store/channel publication state, and the
external Flathub manifest commit where applicable. A successful workflow upload
is not evidence that a store has published or served the package.
