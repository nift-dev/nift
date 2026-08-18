# Codex handover — Checkpoint 10 cross-platform behavioural equivalence

Date: 2026-08-18
Starting Nift state: post-Checkpoint-9 workspace, Nift `main` at `7338414` before this handover-only commit.
Status: **COMPLETE** at final evidence commit `f1512bf`; retained summary is `docs/evidence/checkpoint-10/cross-platform-equivalence.json`.

## Purpose

Checkpoint 10 is the final planned deliberate-hardening campaign before the hardening plateau. It is not a portability compile check. Its purpose is to establish, with explicit scope, that Nift's portable behavioural contracts produce equivalent observable semantics on Linux, macOS and Windows while treating genuinely platform-specific filesystem behaviour as separate documented contracts.

Do not broaden this checkpoint into another generic fuzz, memory, filesystem-chaos or feature-development campaign. Checkpoints 7–9 already cover those local properties and now act as regression constraints.

## Preferred execution model

Use GitHub Actions as the primary execution environment. Add a matrix covering:

- `ubuntu-latest`
- `macos-latest`
- `windows-latest`

The matrix jobs should build Nift, run the same portable behavioural corpus, and upload one normalized machine-readable result artifact per OS. A final comparison job should download all three artifacts and verify normalized semantic equivalence.

The intended loop for Codex is:

1. create a branch / PR for Checkpoint 10;
2. add the corpus, result schema and Actions matrix;
3. run Actions;
4. inspect failed jobs/artifacts;
5. distinguish product defects from invalid portability assumptions or over-normalization;
6. fix the smallest correct layer;
7. rerun the matrix;
8. retain the final evidence in the repository;
9. reconcile HANDOVER / roadmap / Battle Tested only after the matrix is genuinely green.

## Two-layer evidence model

Prefer two explicit layers instead of hashing the entire repository indiscriminately.

### 1. User-visible semantics

For successful build cases, compare the complete generated output tree:

- relative output paths normalized to `/`;
- SHA-256 of file bytes after only explicitly justified normalization;
- file-presence / file-absence expectations;
- command success/failure class.

Where line endings are intentionally platform-neutral, normalize them before hashing. Do not normalize arbitrary content differences merely to make the matrix pass.

### 2. Internal behavioural evidence

Record selected state that represents behavioural contracts without treating implementation details as portable API:

- normalized tracked entry data where relevant;
- selected `.nift` page metadata fields needed to verify dependency / requirement / minification / tracked-state semantics;
- `status` or rebuild-reason classes;
- expected diagnostic **classes** rather than exact presentation strings;
- command exit semantics;
- recovery state after expected failures.

Do not compare timestamps, absolute paths or other incidental state unless the case explicitly tests them.

## Suggested normalized result schema

A result artifact should be easy to compare mechanically. A structure along these lines is appropriate:

```json
{
  "schema_version": 1,
  "checkpoint": "10-cross-platform",
  "platform": {
    "runner_os": "Linux",
    "compiler": "...",
    "nift_commit": "..."
  },
  "cases": [
    {
      "name": "incremental-content-change",
      "classification": "portable",
      "exit_class": "success",
      "outputs": {
        "index.html": "<sha256>"
      },
      "state": {
        "tracked_count": 3,
        "rebuild_class": "clean"
      },
      "diagnostic_class": null,
      "pass": true
    }
  ]
}
```

The exact schema may evolve, but keep it deterministic, explicit and versioned.

## What may be normalized

Normalize only differences that are presentation/platform noise rather than semantic differences. Likely candidates include:

- `\` vs `/` path separators;
- `.exe` suffixes;
- absolute temporary/workspace paths;
- CRLF vs LF where the tested semantic contract does not promise exact line endings;
- locale-sensitive formatting in diagnostics when a stable diagnostic class can be compared instead;
- filesystem timestamp representation / granularity when timestamps are not the contract;
- runner-specific compiler/version presentation stored as metadata rather than compared for equality.

Every normalization rule should be documented close to the comparer. If a rule is added because a matrix run failed, first establish that the difference is genuinely non-semantic.

## What must not be normalized away

Do not hide differences in:

- generated path structure;
- generated content semantics;
- tracked/untracked lifecycle;
- dependency / requirement invalidation;
- build success vs failure;
- stale-vs-clean status;
- missing-output behaviour;
- contract / JSON / schema interpretation;
- parser success/failure class;
- failure recovery;
- persistent state that controls a later build.

A platform discrepancy in one of those categories is a product finding until proven otherwise.

## Portable behavioural corpus

The corpus should be broad enough to cover Nift's meaningful cross-platform contracts without simply rerunning every historical test. Include representative cases for:

- clean `build-all`;
- no-op `build-updated`;
- content edit invalidation;
- template/shared `@input` invalidation;
- JSON data and JSON Schema invalidation;
- Project Contract source invalidation;
- `@pathto` / requirement semantics;
- tracked entry add / move / remove lifecycle;
- template-less tracked entries;
- minified vs non-minified configured outputs where supported;
- failed render preserving the last-good output / stale state;
- repaired build converging cleanly;
- representative malformed parser input producing a controlled error;
- representative Unicode project/content/output names;
- selected nested-directory paths;
- output and relevant selected `.nift` metadata equivalence.

Use Checkpoint 7's whole-output property as a design influence, but Checkpoint 10 should remain a cross-platform corpus rather than repeating all 720 mutation comparisons on every runner unless runtime is trivial.

## Platform-specific filesystem contracts

Do not force false equivalence for behaviours that operating systems/filesystems genuinely define differently. Classify these separately from the portable corpus.

Examples:

- symlink creation privileges / policy on Windows;
- case sensitivity and case-preserving behaviour;
- permission-bit semantics;
- file locking / replacement semantics;
- executable suffixes;
- path-length constraints;
- filesystem timestamp resolution.

For platform-specific cases, the result artifact should record the expected contract for that platform and whether Nift satisfies it. The final comparison job should require all **portable** cases to be semantically equivalent and all **platform-specific** cases to satisfy their documented per-platform expectation.

Checkpoint 8 deliberately scoped its filesystem evidence to Linux; do not silently generalize those exact permission/symlink assumptions to Windows/macOS.

## Existing regression constraints — do not break

Checkpoint 10 fixes must preserve the established evidence from Checkpoints 7–9.

Run the relevant maintained gates after any change that touches their area:

```sh
make checkpoint-7-incremental-equivalence
make checkpoint-8-filesystem-transaction
make checkpoint-9-parser-fuzz
```

Also rerun focused existing tests when changing parser, filesystem, tracking, contracts, JSON/schema or incremental logic.

Checkpoint 7 established 720/720 incremental-vs-clean complete output-tree comparisons across `modified`, `hash` and `hybrid` modes.

Checkpoint 8 established 13 Linux filesystem/transaction cases and introduced readable-regular-file checks plus same-directory temporary replacement for generated/state writes.

Checkpoint 9 established 1,217 sanitizer-backed n++ parser/resource cases with zero crashes, timeouts or sanitizer findings and an explicit 64-level recursive parse boundary.

Treat these as constraints, not old one-off experiments.

## Scope boundaries

- Do not pull tscc into Checkpoint 10 merely because it is in the workspace. There is no production Nift↔tscc integration to prove.
- Do not re-run generic Jsonic++ or Minify++ standalone campaigns inside this checkpoint. Their Nift-relevant integration boundary was already exercised in Checkpoint 6.
- Do not add package-manager work to this checkpoint.
- Do not refactor unrelated code while chasing matrix parity.
- Do not call the checkpoint complete because all three jobs compile.
- Do not promote Battle Tested wording until the final normalized comparison job passes.

## Evidence retention

When green, retain:

- the Checkpoint 10 corpus;
- normalization/comparison code;
- GitHub Actions workflow;
- exact per-platform JSON artifacts or a reproducible summarized form committed under `docs/evidence/checkpoint-10/`;
- compiler / runner / Nift commit metadata;
- any minimized regressions created from real platform findings;
- a short record of differences that were classified as harmless normalization vs actual bugs.

If GitHub-generated raw artifacts are too runner-specific or bulky for Git, retain a compact canonical evidence summary in the repository and keep the workflow capable of reproducing the detailed artifacts.

## Documentation discipline

Until the entire required matrix and final comparison job are green:

- Checkpoint 10 = **IN PROGRESS**;
- cross-platform behavioural equivalence = **not yet proven**;
- Battle Tested must continue to describe it as the final frontier.

After it passes, update:

- `HANDOVER.md`;
- the workspace roadmap;
- website `HANDOVER.md`;
- website project history;
- Battle Tested;
- retained evidence.

The public wording should state the exact tested runner/OS/compiler matrix and corpus scope. Prefer wording such as:

> For the portable behavioural corpus tested, normalized Linux, macOS and Windows results were equivalent; platform-specific filesystem cases satisfied their documented per-platform expectations.

Do not turn that into “works identically everywhere.”

## After Checkpoint 10

Checkpoint 10 is intended to end the deliberate arbitrary-hardening sequence.

Once the evidence and documentation are reconciled, declare the hardening plateau rather than inventing Checkpoint 11. Move the project into:

1. package-manager / distribution completion;
2. sustained real-world dogfooding;
3. feature-rich example-site dogfooding;
4. AI-DX experiment: give a fresh coding agent only public Nift documentation and ask it to build a substantial site, then report which Nift features it used naturally, which it avoided, and where documentation caused friction;
5. publicity / broader field exposure.

That field exposure is the next kind of evidence Nift cannot manufacture through another synthetic torture category.

## Completion record

The maintained GitHub Actions gate ran the same 18-case portable behavioural corpus on `ubuntu-latest`, `macos-latest` and `windows-latest`, uploaded normalized JSON from each runner and passed a final artifact-consuming comparison with zero portable mismatches. Two platform-specific contracts—Windows executable suffix and read-only generated-file deletion semantics—also satisfied their explicit expectations.

The campaign found one genuine product defect: tracked `mv`/`rm` could leave stale read-only generated output and metadata on Windows because cleanup ignored failed removal. Commit `bd98e27` introduced portable owned-artifact removal and the cross-platform lifecycle cases verified the repair. CI runtime visibility and UTF-8 decoding defects were fixed in the harness rather than normalized as product semantics.

Final passing workflow run: <https://github.com/nift-dev/nift/actions/runs/32118334090> at Nift commit `f1512bf`. Checkpoints 7–9 were rerun and remained green. Checkpoint 10 closes the planned deliberate-hardening campaign; do not create a Checkpoint 11 without a newly justified guarantee or field finding.
