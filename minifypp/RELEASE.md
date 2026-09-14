# Minify++ release and publication handover

## Authority and current state

- Latest public release: **v1.1.1** at released commit
  `80043581db61e61a417c0d10c2a0ac50a7a95de5`.
- Current development version: **1.1.2** (`minify --version` reports
  `Minify++ 1.1.2`; the public API format version is `1` in
  `include/minify/Minify.h`).
- The repository remote is `minify-cx/minify`. Exact tag, artifact, and public
  release conventions follow `PACKAGING.md`-style evidence in this document and
  the actual Git/release state.

A website content checkpoint, regression checkpoint, and executable version are
distinct identities. Do not synchronize version numbers mechanically.

## Checkpoint versus release

```text
validated checkpoint
    coherent development baseline, not public by implication

release candidate
    validated checkpoint undergoing packaging/publication checks

release
    deliberately published artifact after approval
```

## Release-candidate validation

Proportionately include:

1. Clean source build with the intended release compiler/options
   (`g++ -std=c++17 -O2`, and clang if convenient).
2. Full implementation-local test set (`make test`: smoke, Node differential,
   15,459-program generated JS semantic corpus, 180-program JSX/TSX corpus,
   PostCSS CSS semantic differential, 111-program non-JS idempotence corpus,
   cross-format adversarial, CLI, deterministic fuzz).
3. `make test-sanitize` (ASan/UBSan) and the retained memory-safety gates.
4. Current benchmark/regression evidence where relevant.
5. Exact synchronization with the Nift embedded subtree
   (`make check-nift-sync NIFT_MINIFYPP_DIR=/path/to/nift/minifypp`) and with
   the standalone Jsonic++ JSON parser source.
6. Review the website (`minify-cx/minify-cx.github.io`): complete any release-targeted items,
   build the website with the candidate binary where relevant, and verify the
   generated site.
7. Reconcile README, docs, website, AI context, release notes, decisions, and
   the production roadmap.
8. Build the actual package/archive layouts the release workflow will publish,
   extract them freshly, and run the executable (`minify --version`) plus a
   representative minification pass.
9. Inspect repository state for generated/debug residue.

Repository tests passing does not prove a release archive is usable. After the
release is public, `installer-public-smoke` in `release.yml` installs the exact
tagged release through the live website installer on Linux/macOS and verifies
the installed binary version.

## Website publication

The Minify++ website source is a separate repository
(`minify-cx/minify-cx.github.io`)
on its authoritative `stage` branch. Its nested `public/` is a separate
generated Git checkout on `main`. For publication checkpoints, commit the
rebuilt/generated `public/` checkout on `main` first, then the corresponding
authoritative source changes on `stage`, and verify both trees are clean.

The canonical `packaging/install.sh`, `download.sh`, `update.sh` and
`uninstall.sh` files are served byte-for-byte as the corresponding `.sh`
endpoints below `https://minify.cx/`. When any script
changes, copy all four to the website root and generated `public/` checkout.
Commit generated output first, then website source, deploy them, and verify the
live bytes before release rehearsal.

## Version and notes

User-visible behavioral changes, correctness fixes, and language capabilities
may justify version/release-note changes. Follow established Git/release
evidence and ask before assigning a public release version.

Immediately after a public release, advance the executable identity to the next
development version before further development. Update `cli/main.cpp`
(`--version` text), `include/minify/Minify.h` (`format_version` only when the
format semantics change), and release notes as part of the same post-release
checkpoint.

## Release report

Record exact source/suite/site identities, commands, outcomes, environment
where material, package contents, known limitations, and publication status.
Separate facts from interpretation. For packaged releases, also record artifact
checksums and store/channel publication state where applicable.

## Step-by-step release guide

Use this order for a normal `X.Y.Z` production release. Stop at any failed gate,
fix the problem before tagging where possible, and retain exact evidence.

### 1. Prepare the release candidate

1. Start from the intended clean `main` commit and review unrelated working-tree
   changes before touching release files.
2. Choose `X.Y.Z` with approval. Update the public version reported by
   `cli/main.cpp` and release notes.
3. Verify `minify --version` and `minify --help` are release-ready.
4. Complete the release-candidate validation above, including the full test
   set, sanitizers, the Nift sync gate, and residue inspection.
5. Build the same archive layouts the workflow will publish; extract and test
   the contained executable.
6. Commit and push all approved release-preparation changes. Recheck that
   `main` and the intended release commit are exactly the state validated.

### 2. Rehearse without publishing

1. Dispatch `.github/workflows/release.yml` manually with `version=X.Y.Z` at the
   exact pushed candidate SHA (`origin/main`). The `workflow_dispatch` path runs
   the same platform archive, packaging, public-script and sanitizer validation
   jobs, then the `rehearse` job validates the exact four-archive asset set and
   builds `SHA256SUMS` without creating or modifying any GitHub release.
2. Confirm the run's `head` equals the candidate SHA, every job succeeds, and
   the rehearsal bundle contains exactly
   `minify-$version-{linux-x86_64,macos-arm64,macos-x86_64}.tar.gz`,
   `minify-$version-windows-x86_64.zip` and `SHA256SUMS`.
3. Download the rehearsal artifact and review the checksums against a locally
   built archive set for the same version.

### 3. Create the GitHub release

1. Obtain explicit approval for the public release action.
2. Create the approved annotated `vX.Y.Z` tag at the validated commit and push
   it to `minify-cx/minify`.
3. Watch `.github/workflows/release.yml`. All Linux, macOS and Windows artifact
   jobs plus the packaging and public-script gates must succeed before the GitHub release is
   created. After publication, require `installer-public-smoke` to pass on
   Linux/macOS; this proves the live website installer matches the tag, verifies
   the release checksum, and installs the tagged release.
4. Confirm the release contains exactly the expected four platform archives and
   `SHA256SUMS`, and that each archive name and embedded executable version
   match `X.Y.Z`.
5. Download at least the checksums and representative archives from the public
   release URL, verify them independently, and record the release URL, tag
   commit, workflow run and final checksums.
6. From this point, treat every published asset as immutable. Never replace an
   archive at the same URL. A workflow rerun should leave an existing release
   untouched.

### 4. Close the release

1. Record the exact tag/commit, GitHub release and workflow URLs, final
   checksums, installation tests and known limitations in this handover.
2. Update the website install/download instructions only with availability that
   has been confirmed from the public release.
