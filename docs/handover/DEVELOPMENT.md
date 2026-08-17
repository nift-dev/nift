# Nift development and checkpoint workflow

## Before changing code

1. Inspect branch, `HEAD`, and `git status`. Preserve all unknown/user work.
2. Define a bounded objective and identify participating repositories.
3. Build and run relevant baseline tests before editing when practical.
4. Record baseline failures separately from candidate failures.
5. Trace implementation and construct a reproducer/contract before fixing bugs
   or adding user-visible semantics.

Do not perform broad cleanup merely because a focused task exposes nearby code.
Small necessary extractions are appropriate; unrelated parser or architecture
rewrites need their own evidence and scope.

## Normal implementation loop

```text
understand call path
→ specify behavior externally
→ confirm intended failing test
→ implement smallest coherent change
→ focused tests
→ high-risk interactions
→ full local suite
→ external contract
→ risk-specific validation
→ docs/site/handover reconciliation
→ checkpoint report
```

For bugs, reduce and preserve the reproducer. Look for siblings in the same
failure family. Prefer root-cause lifecycle/parser/state fixes over one-off
special cases.

Before calling a user-visible behavior checkpoint complete, perform explicit
coverage accounting across both repositories:

```text
changed behavior or invariant
  -> implementation-local test and Makefile target in nift
  -> black-box contract module registered by nift-regression-suite/run-contract.sh
```

Add both layers when they apply and run the external suite against the candidate
executable. Do not assume that adding a shell test under `nift/tests` updates the
independent contract repository. If one layer genuinely does not apply, record
the reason in the checkpoint report; an unexamined omission is not completion.

## Reference checkpoint: textual parameter interpolation

The 2026-08-16 `$[...]` textual-parameter checkpoint is a representative example
of this workflow working in practice. It began with an independent intentionally
red contract, added a narrow one-pass value interpolation seam without making
parameters recursive Nift templates, and then validated focused semantics,
historical behavior, high-risk dependency/requirement transitions, native safety,
the real Nift website, performance, and memory before reconciling release notes,
decisions, roadmaps, and handovers.

Use it as an example of checkpoint-quality development, not as fixed ceremony.
Choose evidence in proportion to the change: filesystem work needs stronger
containment and lifecycle validation, parser restructuring may justify fuzzing,
and documentation-only work has a different risk profile. Revise or replace this
example when later checkpoints establish a better process.

## Checkpoint vocabulary

- **WORKING**: implementation exists; material validation remains.
- **CHECKPOINT CANDIDATE**: intended work appears complete and major validation
  has passed; ready for reconciliation/review.
- **VALIDATED CHECKPOINT**: evidence is sufficient to become the next trusted
  development baseline.
- **RELEASE CANDIDATE**: validated checkpoint undergoing release preparation.
- **RELEASED**: public action completed deliberately.

These are not mandatory Git objects. Do not create commits, tags, or branches
solely because the vocabulary exists.

## Proportional validation

Parser/language changes normally require focused parser/value tests, external
contract cases, malformed and adjacency cases, lexical scope, web-language
transparency, full regression, ASan/UBSan where practical, and a performance
smoke check.

Filesystem/state changes require lifecycle, failure/recovery, traversal,
transactionality, and persistence tests.

Performance changes require the same fixture/configuration before and after,
repeat measurements where noise matters, correctness suites, and peak-memory
consideration.

Documentation changes require example verification and website build/inspection;
they do not automatically need native sanitizer or benchmark runs.

## Website synchronization

When public behavior, syntax, terminology, commands, configuration, benchmark
evidence, or testing claims change, review the Nift website source globally.
Build it using the exact candidate Nift binary. Do not edit its generated `public`
checkout as canonical source. User-visible behavior should be settled and tested
before final website copy is completed.

## Parallel work

If parallel agents/worktrees are explicitly used, give each the same trusted base
and a bounded scope. Their findings are evidence, not automatic merge candidates.
Compare semantic differences, integrate deliberately, and rerun validation on
the integrated candidate.

## Candidate diff review

Before checkpoint promotion, inspect status and complete diffs for debug output,
binaries, generated residue, accidental version changes, unrelated edits, and
fixture debris. Investigative files must be deliberately retained, moved into a
test/benchmark location, documented, or removed safely.

## Checkpoint report

For substantial work include, as relevant:

```text
Scope and objective
Baseline branch/commit/status and known failures
Implementation and rationale
Tests added/changed
Local/external coverage map, including any justified one-layer exceptions
Focused validation
Full local and external validation
Sanitizers/safety
Performance/memory
Website/docs
Handover/decision/roadmap impact
Repository state
Known limitations/deferred work
Status and publication state
```

Evidence should be exact without being exaggerated. “Validated against X” is
better than “proven bug-free.”

## Living documentation and roadmap

Every substantial checkpoint must ask whether architecture, tests, website,
handover knowledge, production confidence, or the next priority changed. Update
durable principles rather than appending a diary entry. Roadmaps may grow,
shrink, reorder, or change scope as evidence changes.
