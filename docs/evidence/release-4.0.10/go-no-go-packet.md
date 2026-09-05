# Nift 4.0.10 release candidate — go/no-go packet (finalized)

This packet records the local and hosted release-candidate evidence for Nift
4.0.10. The non-publishing release rehearsal, the Snap rehearsal, hosted CI at
the release-preparation head and the pre-tag verifications have all run and are
recorded below. The tag itself has **not** been created: the final go/no-go
decision, tag authorization and the tag-triggered publication run are still
**pending**.

## 1. Candidate heads and tag target

| Repository | Head | Role |
|---|---|---|
| canonical `nift` (implementation candidate) | `a520921346a853115a5dfe1f3709b069ba1f9431` | contains the full v4.0.10 implementation; pushed `main` |
| canonical `nift` (initial release-preparation content commit) | recorded externally after creation | release notes + this packet + handover/evidence reconciliation |
| canonical `nift` (packet-finalization commit) | recorded externally after creation | this finalized packet (documentation/evidence finalization) |
| canonical `nift` (proposed `v4.0.10` tag target) | recorded externally immediately before tag authorization | the final reviewed commit containing this finalized packet |
| standalone Markup++ (canonical source) | `69a4ab3b004866a4ba3218395c5a2c03b0672a6e` | embedded-sync source of truth; evidence-bearing commit `20cbefe` |
| website source (`nift-dev.github.io` `stage`) | `0c79e5e` | pushed; documents the `@json` schema disambiguation |
| website generated (`nift-dev.github.io` `public/main`) | `8e4fd00` | pushed |
| Markup++ website source (`markup-website` `stage`) | `69cb46f` | pushed; records verified compatibility evidence |
| Markup++ website generated (`markup-website` `public/main`) | `dcb31e9` | pushed |
| independent contract suite (`nift-regression-suite`) | `1107bdd04cb5d6c9b0151bccdaaf677854947777` | 25/25 modules PASS against the candidate binary |

Version: **Nift 4.0.10** (`src/CLI.cpp:34`, `snap/snapcraft.yaml`, release notes).
Post-release dev identity: **4.0.11**.

## 2. Commit classification (v4.0.9 … final head)

Every commit in the range `v4.0.9..HEAD` appears in the release record.

| Commit | Class |
|---|---|
| `f3db1e7` Post-release: advance development identity to v4.0.10 and record the v4.0.9 release | post-release version advancement + documentation/evidence |
| `6a44a88` Embed Markup++ and revise JSON directives | included user-facing change (embedded converter, `@markup`, name-first `@json`) |
| `c28a48d` Fix Markup++ embedding and binding builds | correctness/build fix (PIC rules, Node/Python binding sources, engine tests) |
| `a26148d` Harden @markup and @json: schema disambiguation, cycles and brace handling | correctness/hardening (schema-name vs path, include cycles, code-span braces) |
| `d7a9cdc` Sync embedded Markup++ and update Nift documentation | embedded-source sync + documentation |
| `cfe9733` Fix make clean to remove all binding and packaging build products | build-system fix |
| `5565039` Correct embedded Markup++ provenance in the handover | documentation/evidence correction |
| `ae692c3` Migrate checkpoint fixtures to the name-first @json contract | CI-evidence migration (checkpoint 7/8/9/10 fixtures) |
| `a520921` Fix Python packaging to embed Markup++ and the vendored cmark engine | CI-driven packaging fix (sdist/wheel source set, manifest) |
| `<this release-preparation commit>` | initial release-preparation: release notes + this packet |
| `<this packet-finalization commit>` | documentation/evidence finalization carrying the finalized packet |

## 3. v4.0.10 correctness work (this candidate)

- **Embedded Markup++ converter.** The approved Markup++ library and vendored
  cmark engine are embedded under `markuppp/` and synchronized byte-for-byte
  with the standalone repository (41 canonical files). Nift owns filesystem
  policy, dependency recording and template evaluation; Markup++ remains IO-free
  and knows nothing about Nift's project model.
- **`@markup` directive.** Inline and file-backed Markdown/AsciiDoc/RST
  conversion with template-before-conversion ordering, no re-parse of generated
  HTML, project-root containment and automatic source/include dependency
  tracking. Include resolution participates in input-loop tracking so nested
  `@markup` cycles are reported as cycles.
- **Name-first `@json`.** Six forms (file-backed and inline, each with optional
  schema-path or named-schema). Inline bodies are templated before parsing;
  bare schema identifiers must be existing bindings (never a same-named file);
  quoted arguments are schema paths. Path-first forms are retired and fail with
  a clear diagnostic.
- **Cycle, diagnostic and brace handling.** AsciiDoc/RST include cycles,
  cross-detector `@markup` cycles, schema-name disambiguation, and inline-block
  brace counting that ignores quoted strings, comments and backtick code
  spans/fences.
- **Binding and packaging support.** The C ABI library, Go, C#, Node and Python
  bindings compile and link the complete embedded converter source set. Python
  sdist/wheel packaging stages the full Markup++ and cmark source set, and
  `make clean` removes all binding, packaging and Python-cache products.

## 4. Compatibility claims (bounded)

- **CommonMark 0.31.2.** 652/652 official examples; the pinned cmark 0.31.1
  engine implements spec 0.31.2.
- **Asciidoctor-core 2.0.26 (AC0–AC9).** Bounded profile validated by a
  normalized-output gate with hash-pinned differences. Not "Asciidoctor
  compatible" with every extension; never drop-in compatible.
- **Docutils-core 0.23 (RST0–RST14).** Bounded profile validated by a
  normalized-output gate with hash-pinned differences. Not Docutils/Sphinx
  compatible beyond the documented profile.

Cross-platform evidence for the strengthened gates: Markup++ commit `20cbefe`
in Actions run `33964331360` passed Linux (GCC), macOS (Clang) and Windows
(MSVC) including the pinned-output gates and the difference-gate adversarial
self-test, plus the bounded libFuzzer job. Evidence-only Markup++ commit
`69a4ab3` (run `33964887282`) records that result.

## 5. Local and hosted validation evidence

- **Hosted CI at Nift release-preparation head `2c9dd96`** (all push-triggered
  workflows, dispatched at that exact head):
  - Test integrity guards: `33966355566` — SUCCESS.
  - packaging matrix (build-only, non-publishing): `33966357347` — SUCCESS (all
    six jobs incl. language packages).
  - Checkpoint 10 cross-platform equivalence: `33966359564` — SUCCESS.
  - Init targets: `33966361371` — SUCCESS.
- **Independent regression suite.** `run-contract.sh` → 25/25 modules PASS at
  Nift `a520921`; hosted workflow `33964012981` (main) and `33964185936`
  (explicit full SHA `a520921346a853115a5dfe1f3709b069ba1f9431`) both SUCCESS.
  The release-preparation commit changes documentation only; the regression
  suite result carries to the release-preparation head unchanged.
- **Local Linux (g++ 15.2.0).** `make test` exit 0; `make test-all` exit 0
  (embed, bindings: Go `-race`, C# 27/27, Node 25/25, Python 22/22); sanitizer
  and TSAN targets pass; `make test-markuppp-sync` → 41 files PASS;
  `packaging/test-version-derivation.sh` PASS; standalone Markup++
  `make test-release-local` + `test-sanitize` PASS including AC9 (8 normalized
  matches + 1 pinned difference), RST14 (17 normalized + 7 pinned differences)
  and the 12-check difference-gate self-test; the Python wheel builds from the
  staged native tree and the clean-consumer render passes.
- **Websites.** Nift `nift status` → 75/75; Markup++ website → 6/6.

## 5a. Non-publishing rehearsals (run at the release-preparation head)

- **`Release artifacts` rehearsal** — run `33966850169` at `2c9dd96`
  (`version=4.0.10`), conclusion **SUCCESS**:
  - `unix (linux-x86_64)`, `unix (macos-arm64)`, `unix (macos-x86_64)`,
    `windows` — build + extracted-archive native smoke + correct `Nift
    v4.0.10` version each;
  - `installer-preflight` — `sh -n packaging/install.sh` + `make
    test-installer` PASS;
  - `public-installer-preflight` — `https://nift.dev/install` byte-identical
    to `packaging/install.sh` PASS;
  - `rehearse` — exact four-asset set + `SHA256SUMS` manifest generated;
    publishing jobs (`publish`, `chocolatey`, `homebrew`,
    `installer-public-smoke`, `snap`) skipped by the dispatch trigger, so no
    GitHub release or package publication occurred.
- **`Snap` rehearsal** — run `33967077103` at `2c9dd96`, conclusion **SUCCESS**:
  - `toolchain-preflight` — pinned Snapcraft 9.0.1, snap revision 18514
    verified;
  - `build (amd64)` and `build (arm64)` — validation builds PASS;
  - `release-coordination` skipped (tag-trigger only), so no Store channel
    mutation occurred.

## 5b. Pre-tag verifications (at finalization time)

- `src/CLI.cpp:34` reports `Nift v4.0.10`; `snap/snapcraft.yaml` reports
  `4.0.10`; `packaging/test-version-derivation.sh` PASS (metadata derives the
  synchronized version).
- `v4.0.10` does not exist locally, on the remote, or as a GitHub release.
- `https://nift.dev/install` is byte-identical to `packaging/install.sh`
  (SHA-256 `a98bdf72…`).
- Every repository head matches its remote; every working tree is clean of
  uncommitted changes.

## 6. Pending items before tag authorization

- [x] Initial release-preparation commit `2c9dd96` created on `nift` `main`
      (notes + this packet) and pushed.
- [x] Hosted CI at the release-preparation head `2c9dd96` (init targets,
      checkpoint-10, test-integrity, packaging) — all SUCCESS.
- [x] Non-publishing `Release artifacts` rehearsal (`version=4.0.10`) —
      SUCCESS (`33966850169`).
- [x] Non-publishing `Snap` dispatch — SUCCESS (`33967077103`), no Store
      mutation.
- [x] Pre-tag verifications — all pass (version, snap version, tag absence,
      installer byte-equality, heads match remotes, clean trees).
- [ ] Final documentation commit carrying this finalized packet. **PENDING:
      review and push.**
- [ ] Hosted CI at the exact final head after the packet-finalization commit.
- [ ] The final reviewed commit containing this finalized packet is resolved
      and recorded externally, and explicit tag authorization names it.

## 7. Repository cleanliness

- At the committed candidate head, the canonical `nift` tree contains no
  uncommitted files or generated/debug residue.
- Website `stage` and generated `public/main` trees are clean.
- `nift-regression-suite`: clean at `1107bdd`.
- Standalone Markup++ and its website trees are clean.

## 8. Proposed tag and release notes

- Tag: `v4.0.10` (annotated) at the final reviewed commit containing this
  finalized packet, resolved and recorded externally immediately before tag
  authorization (**PENDING**).
- Release body = the reviewed notes file
  `docs/evidence/release-4.0.10/release-notes-4.0.10.md`, published verbatim by
  `release.yml` (`--notes-file`).
- No Markup++ tag or GitHub release is created as part of the Nift release;
  Nift vendors the approved source and does not require an independently
  published Markup++ binary release.

## 9. Known limitations

Binaries unsigned; release is CLI-only; embedded engine/bindings/corpus/Rust
remain in-tree but unpublished and unpromoted. Snap and package-channel
publication is exercised by the real runs, not claimed from offline evidence.
Checkpoint-8 claim scope remains limited to the tested Linux failure classes
(no direct ENOSPC; no Windows/macOS permission/locking semantics). Markup++
compatibility is bounded to the documented profiles and never implies full
Asciidoctor, Docutils or Sphinx compatibility.