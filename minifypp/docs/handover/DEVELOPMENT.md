# Minify++ development workflow

## Baseline

Before substantial work, record branch/commit/status, preserve unknown changes,
run `make` and `make test`, and identify the exact format/transformation being
changed. If performance is in scope, capture a comparable baseline first.

## Transformation workflow

1. State the transformation and why it is safe.
2. Create minimal positive cases.
3. Create neighboring counterexamples where removal/rewriting is unsafe.
4. Choose the strongest available oracle: exact output, structural JSON,
   parse/validation, runtime equivalence, or combination.
5. Confirm the new test catches the missing/incorrect behavior.
6. Implement the smallest coherent scanner/formatter change.
7. Run format-specific tests, then the complete suite.
8. Attack sibling token/context cases instead of stopping at the first fix.
9. Run native safety validation appropriate to string/index/lifetime changes.
10. Benchmark only semantically green candidates.
11. Synchronize canonical implementation/header into Nift where applicable and
    run embedded Minify++ plus Nift integration/contract tests.
12. Reconcile README, ReleaseNotes, website, handovers, and roadmap.

Keep test orchestration portable across process supervisors. Where Node supplies
a parser/runtime oracle and a native driver supplies transformation output, prefer
shell-owned native execution followed by Node reading completed files. This avoids
nested-process stalls without weakening the oracle or adding environment skips.

Avoid combining a compression tweak with unrelated scanner architecture changes.
Avoid regex or token shortcuts that do not model the relevant context. Do not add
general compilation behavior because JSX/TSX syntax appears in the corpus.

## Cross-project synchronization

Standalone source is the intended canonical development origin. At the current
checkpoint `src/Minify.cpp` and `include/minify/Minify.h` match Nift byte-for-byte.
After changes, compare exact files, identify intentionally different CLI/build or
test wrappers, and never let Nift acquire an undocumented fork.

Recommended validation flow:

```text
standalone full suite
→ copy/synchronize intended files
→ exact diff check
→ Nift build
→ embedded Minify++ tests
→ Nift minification integration
→ Nift external contract where relevant
```

## Checkpoint review

Ask whether the candidate achieved the bounded semantic objective, preserved all
old format contracts, added the relevant hostile family, passed real/corpus and
safety evidence, maintained performance, synchronized Nift, and left docs/site
truthful. The newest code remains working state until that evidence is complete.

Use these living statuses where useful: **PLANNED**, **AUDITING**,
**IMPLEMENTING**, **VALIDATING**, **BLOCKED**, **COMPLETE**, and **SUPERSEDED**.
They distinguish work not yet attempted from an audit that intentionally found no
production change necessary; they are descriptive, not process bureaucracy.

## Living knowledge

If a bug reveals a general scanner/context lesson, update TESTING or DECISIONS.
An ordinary isolated mistake normally needs a regression and code fix, not a
historical essay. Reassess production priorities whenever new syntax interactions
or corpus failures appear.

## Reference checkpoint: fuzz evidence changed the roadmap

The 2026-08 production audit began as sanitizer/fuzz tooling work, then paused
when successful first-pass minification produced output rejected on a second
pass. Each finding was minimized, expanded into its behavioral family, fixed at
the relevant language boundary, and retained before the interrupted campaign
resumed. This exposed CSS comment-delimiter synthesis, a JSX-significant opener,
and HTML/XML comment/CDATA opener synthesis. The resumed campaign ultimately
completed 70,000 deterministic mutations across seven formats under normal and
sanitized builds.

Preserve the method, not merely the specimens:

1. Let evidence revise checkpoint priority.
2. Minimize the failure and identify the behavioral family.
3. Retain an exact regression and a broader property where justified.
4. Fix the semantic/token boundary rather than special-casing one input.
5. Resume the interrupted validation campaign.
6. Make readiness claims only after all relevant gates are green.
7. Feed the result back into the roadmap and living handovers.

The later website-driven reopening reinforced the same method. A real stylesheet
showed that idempotence alone can preserve the same wrong output, so CSS now has an
independent semantic-tree comparison. A 7,000,000-case continuation then exposed
JavaScript delimiter synthesis and JSX comment/root confusion. Production status
is therefore a revocable evidence judgment, not a milestone that suppresses new
contradictory evidence.

The permanent low-risk property is second-pass acceptance after every successful
first pass. Strict byte idempotence is a separate possible contract and must not
be inferred without a deliberate seven-format audit.
