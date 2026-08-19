# Development checkpoints

> This is the living checkpoint methodology for the Nift ecosystem. Revise it when validation layers, repository ownership, or release practice changes.

This document treats checkpoints as their own subject rather than only through the lens of **website maintenance**.

There’s a fairly specific development methodology we evolved, especially during the Nift hardening/rewrite work, and it would be easy for Codex to interpret “checkpoint” as merely “make a commit after the tests pass.” That isn’t quite what we meant.

I’d send this as the dedicated handover:

# Nift Ecosystem — Development Checkpoint Methodology Handover

## Purpose

This handover documents the **development checkpoint methodology** that evolved during work on Nift and subsequently influenced the way we approached Minify++ and tscc.

It supplements, rather than replaces:

* the comprehensive Nift project-context handover;
* project-specific `HANDOVER.md` files;
* development/testing/release documentation;
* Git history;
* regression suites;
* benchmark evidence;
* task-specific implementation handovers.

The important concept is:

> **A checkpoint is a coherent, evidence-backed development state that is trustworthy enough to become the baseline for subsequent work.**

A checkpoint is **not automatically**:

* a commit;
* a tag;
* a version bump;
* a release;
* a ZIP archive;
* a passing compilation;
* the newest working tree;
* the completion of one coding-agent task.

Those things can represent or accompany a checkpoint, but none defines one by itself.

---

# 1. Why checkpoint-oriented development emerged

The methodology was not designed formally at the beginning.

It emerged from the way Nift was being changed.

The project went through increasingly aggressive rounds of:

```text
simplification
→ regression testing
→ bug discovery
→ fixes
→ deeper adversarial testing
→ architectural rewriting
→ performance investigation
→ scaling investigation
→ memory investigation
→ feature expansion
```

During that process, merely having "the latest version" became insufficient.

There could be:

```text
one source tree with a new fix
another with additional tests
a website describing slightly newer behavior
a benchmark from an older implementation
a parallel audit branch with a better implementation of one subsystem
```

The useful unit therefore became:

```text
known coherent state
        +
known validation evidence
        =
checkpoint
```

---

# 2. The core checkpoint invariant

At any meaningful stage of development, we should be able to answer:

> **What is the most recent state that we actually trust?**

That state is the current checkpoint.

Development then proceeds from:

```text
trusted checkpoint
        ↓
unvalidated work
        ↓
evidence gathering
        ↓
validated candidate
        ↓
new trusted checkpoint
```

The crucial rule is:

> **The newest code is not automatically the new baseline.**

The previous checkpoint remains the trusted baseline until the candidate has earned that status.

---

# 3. Checkpoints are epistemic as much as technical

This is one of the most important aspects of the methodology.

A checkpoint records not only:

```text
what code exists
```

but also:

```text
what we currently know about that code
```

For example, imagine two source revisions differing by only a small optimization.

Before scaling tests:

```text
"seems fast"
```

After a 10,000-page benchmark:

```text
"we have evidence that this architecture scales well at 10k pages"
```

After discovering an O(n²) validation path:

```text
"the earlier confidence was incomplete"
```

After fixing it and rerunning the benchmark:

```text
"the scaling problem is understood and addressed"
```

Those are meaningful development checkpoints even if the final code diff is relatively small.

A checkpoint can therefore be a **knowledge checkpoint**.

---

# 4. Confidence is allowed to change

Checkpoint methodology should never imply:

```text
checkpoint X passed
→ checkpoint X was perfect
```

A later adversarial audit may discover a defect in an earlier checkpoint.

That does not invalidate checkpoint development.

It means:

```text
checkpoint X
    represented the best validated state under evidence E1

later evidence E2
    exposed a new failure class

checkpoint Y
    incorporates E1 + E2
```

This is healthy engineering.

The objective is not omniscience.

It is progressively stronger evidence.

---

# 5. A checkpoint is not a release

This distinction should remain explicit throughout the ecosystem.

```text
CHECKPOINT
    internally coherent development baseline

RELEASE CANDIDATE
    checkpoint being prepared/evaluated for publication

RELEASE
    deliberately published public artifact/version
```

Therefore:

```text
new checkpoint
≠ automatic version bump
≠ automatic tag
≠ automatic GitHub release
≠ automatic website publication
```

Public actions remain deliberate.

---

# 6. A checkpoint is not necessarily a Git commit

Now that Codex works directly with repositories, Git is an excellent mechanism for checkpoint provenance.

But conceptually:

```text
commit
    = recorded source-tree transition

checkpoint
    = validated development state
```

A commit can become the source identity of a checkpoint.

A commit does not become trustworthy merely because it exists.

Likewise, Codex should not create commits merely because a checkpoint has been prepared unless Nick asks it to commit.

---

# 7. Historical ZIP checkpoints

The earlier ChatGPT workflow often produced ZIP archives such as:

```text
Nift source
regression suite
website
```

because ChatGPT and Nick were exchanging filesystem artifacts rather than operating continuously in the same repositories.

Those ZIPs acted as checkpoint snapshots.

That was useful then.

It should not become ritual now.

Codex has:

```text
Git
branches
worktrees
repository history
direct filesystem access
```

which provide better provenance.

Preserve the **checkpoint semantics**, not the old transport mechanism.

---

# 8. What constitutes a checkpoint depends on the change

There is no universal checklist that every modification must satisfy.

Validation should be **proportional to risk and scope**.

A documentation typo might need:

```text
docs inspection
site build
```

A parser feature might need:

```text
focused parser tests
contract tests
adversarial malformed syntax
scope tests
full suite
sanitizers
website build
performance smoke test
```

A performance optimization might need:

```text
correctness baseline
benchmark baseline
candidate benchmark
full suite
memory measurement
sanitizers
```

A filesystem-state fix might need:

```text
focused reproduction
state lifecycle tests
failure/recovery tests
full suite
sanitizers
```

The methodology is evidence-driven, not ceremonial.

---

# 9. Suggested checkpoint lifecycle

For substantial development, I recommend:

```text
1. Identify current trusted baseline
2. Record baseline provenance
3. Establish baseline validation
4. Define bounded checkpoint objective
5. Investigate/reproduce
6. Add contract/regression evidence
7. Implement
8. Run focused validation
9. Test interactions
10. Run full validation
11. Run risk-specific validation
12. Reconcile documentation
13. Reconcile website if applicable
14. Reconcile handover/institutional memory
15. Review Git diff/state
16. Produce checkpoint report
17. Decide whether candidate becomes new baseline
```

Each stage has a reason.

---

# 10. Step 1 — identify the trusted baseline

Before substantial work, establish:

```text
repository
branch
HEAD/commit
working-tree state
current project/version identification
current test-suite checkpoint
```

If several related repositories matter, identify all of them.

For example:

```text
Nift
Nift regression suite
Nift website
Minify++
```

for a Nift feature involving minification.

---

# 11. Step 2 — distinguish clean baseline from dirty working state

Before attributing anything to new work:

```text
git status
```

matters.

If pre-existing modifications exist:

* do not discard them;
* do not silently absorb them into the new task;
* do not attribute them to the current change;
* understand their relationship to the requested work.

Codex should be especially careful here because it can work directly on Nick's real repositories.

---

# 12. Step 3 — establish baseline validation

Run validation appropriate to the task **before changing source** where practical.

For a significant Nift parser change, this might include:

```text
clean build
C++ tests
external regression/contract suite
```

and possibly a baseline benchmark.

The purpose is not busywork.

It answers:

> Did this failure already exist?

Without a baseline, debugging after implementation becomes ambiguous.

---

# 13. Baseline failures must be recorded

If:

```text
497 tests pass
1 already fails
```

do not later report:

```text
candidate has 1 failure
```

as though the candidate caused it.

Likewise, do not silently fix unrelated baseline failures unless they are intentionally brought into scope.

Checkpoint reports should distinguish:

```text
pre-existing
introduced
fixed
remaining
```

---

# 14. Step 4 — define the checkpoint objective

The objective should describe a coherent development result.

For example:

> Add `$[...]` interpolation to textual directive parameters while preserving the distinction between values and operations, including correct dynamic dependency lifecycle behavior.

That is much better than:

> Modify parser.

The objective determines what evidence is necessary.

---

# 15. Bounded objectives matter

Avoid checkpoints whose objective is:

```text
make Nift better
```

Prefer:

```text
parameter interpolation
filesystem-state hardening
10k scaling audit
Minify++ JavaScript semantics pass
watch-mode reconciliation
```

A bounded checkpoint makes:

```text
done
not done
out of scope
```

meaningful.

---

# 16. Step 5 — investigate before modifying

This was a recurring strength of the later Nift work.

Before fixing something:

```text
inspect source
trace call path
construct reproducer
understand failure family
```

rather than immediately patching the visible symptom.

For example:

```text
one malformed JSON crash
```

may reveal:

```text
an entire unchecked structural-assumption family
```

The checkpoint should ideally fix/test the family, not only the first reproducer.

---

# 17. Reproducer first when practical

For bugs:

```text
observe failure
        ↓
create deterministic reproducer
        ↓
confirm baseline fails
        ↓
implement fix
        ↓
confirm reproducer passes
```

This is strongly preferred.

The reproducer often becomes a permanent regression.

---

# 18. Determinism is part of checkpoint quality

A flaky test does not provide strong checkpoint evidence.

A notable historical example involved same-second dependency edits.

Rather than relying on:

```text
write quickly
sleep
hope filesystem timestamp behaves
```

we moved toward explicitly controlled timestamps.

That turned:

```text
probably tests sub-second mtimes
```

into:

```text
deterministically tests sub-second mtimes
```

Prefer deterministic fixtures wherever possible.

---

# 19. Step 6 — add contract evidence

Where practical, write the test that should fail before changing implementation.

This is especially important for:

```text
language behavior
CLI behavior
filesystem semantics
incremental rebuild rules
compiler semantics
minifier correctness
```

The test establishes what the checkpoint is trying to protect.

---

# 20. External suites are especially valuable

For Nift, the independent black-box regression suite should remain capable of testing:

```text
an arbitrary Nift executable
```

without depending on implementation internals.

This gives checkpoint evidence that survives refactoring.

The implementation-local C++ suite answers a different question.

Use both where appropriate.

---

# 21. Step 7 — implementation

Implementation should be scoped to the understood failure/feature.

Do not use checkpoint work as an excuse for unrelated cleanup unless the cleanup is necessary or deliberately added to scope.

This matters because smaller causal surfaces make checkpoint evidence easier to interpret.

---

# 22. Step 8 — focused validation

Before running everything, run the smallest relevant tests.

For example:

```text
new resolver unit tests
new @input interpolation contract cases
dynamic dependency A→B case
```

This shortens the edit/test loop.

---

# 23. Step 9 — interaction testing

This is one of the defining characteristics of our later development work.

A feature does not exist in isolation.

For Nift:

```text
parser feature
× lexical scope
× JSON
× dependency graph
× incremental mode
× watch mode
× filesystem safety
```

may expose failures invisible in the happy path.

Checkpoint validation should deliberately ask:

> What existing subsystem does this new behavior interact with?

---

# 24. Interaction matrices should be selective

Do not mechanically cross every feature with every feature.

That explodes combinatorially.

Instead identify high-risk intersections.

For `$[...]` parameter interpolation:

```text
interpolation × lexical scope
interpolation × @input dependencies
interpolation × @dep
interpolation × @pathto requirements
interpolation × path traversal
interpolation × incremental rebuild
interpolation × watch
```

are meaningful.

```text
interpolation × unrelated CLI formatting
```

probably is not.

---

# 25. Step 10 — full regression

After focused and interaction tests pass:

```text
run the full relevant suite
```

This answers:

> Did the bounded change disturb behavior elsewhere?

Do not substitute a handful of new passing tests for the existing regression corpus.

---

# 26. Old tests are evidence too

One reason the Nift suite became valuable was cumulative protection:

```text
146
→ 211
→ 245+
→ 492+
→ later larger suites
```

The exact historical numbers matter less than the principle:

> Every discovered bug family should make future changes harder to accidentally regress.

A new checkpoint inherits all prior obligations unless behavior is intentionally changed.

---

# 27. Intentional contract changes

Sometimes an old test should change.

That is acceptable when the behavior itself is intentionally changing.

But do not simply delete a failing old test to make a checkpoint green.

Record:

```text
old behavior
new intended behavior
reason
affected tests/docs
```

This is where the decision ledger is useful.

---

# 28. Step 11 — risk-specific validation

Different changes deserve different extra validation.

### Memory-sensitive code

```text
ASan
RSS measurement
large fixture
```

### Undefined-behavior risk

```text
UBSan
```

### Concurrency change

```text
TSan where appropriate
stress runs
```

### Parser change

```text
malformed inputs
boundary syntax
fuzzing if available
```

### Performance change

```text
before/after benchmark
same fixture
multiple runs
```

### Filesystem/state change

```text
failure/recovery
deletion
rename
permissions
stale state
```

This is more useful than running every tool after every change.

---

# 29. Sanitizers are evidence, not decorations

A report saying:

```text
ASan clean
```

should mean the relevant test workload was actually run under ASan.

Likewise:

```text
UBSan clean
```

should identify enough context to be meaningful.

Do not list validation tools merely because the project can compile with them.

---

# 30. Performance checkpoints require a baseline

Never report:

```text
now takes 80 ms
```

as an improvement without knowing what the comparable baseline was.

Use:

```text
same machine
same fixture
same command
same build configuration
baseline
candidate
```

as far as practical.

---

# 31. Performance results should include variability

For important comparisons, avoid overinterpreting one run.

Use repeated measurements and report something like:

```text
range
median
typical value
```

where useful.

Nift is fast enough that scheduler/filesystem noise can be a meaningful percentage of tiny timings.

---

# 32. Performance and correctness are coupled

A faster candidate that fails the suite is not a successful performance checkpoint.

Likewise, a memory optimization that breaks incremental semantics is not an improvement.

The order is:

```text
correctness
+
performance evidence
```

not one or the other.

---

# 33. Scaling checkpoints

For scaling work, test more than one size where possible.

For example:

```text
100
1,000
10,000
```

can reveal complexity trends that a single 10k number cannot.

The key question may be:

```text
O(n)
vs
O(n²)
```

rather than the absolute time.

---

# 34. Memory checkpoints

The same principle applies to memory.

Measure:

```text
baseline RSS
candidate RSS
fixture size
```

and inspect whether memory scales sensibly.

Do not assume a faster data structure is automatically better if it increases memory dramatically.

This became relevant in Nift's later optimization work.

---

# 35. Step 12 — documentation reconciliation

Ask:

```text
Did user-visible behavior change?
Did syntax change?
Did terminology change?
Did configuration change?
Did a documented limitation disappear?
Did a new limitation appear?
```

If yes, update the appropriate docs.

A checkpoint with correct code and stale instructions is not fully coherent.

---

# 36. Documentation should describe validated behavior

Do not finalize docs from an intended implementation before confirming that implementation.

Prefer:

```text
contract
→ implementation
→ evidence
→ final documentation
```

---

# 37. Step 13 — website reconciliation

Not every checkpoint affects a website.

But inspect whether it does.

For Nift:

```text
template language
commands
configuration
benchmark claims
testing claims
positioning
examples
AI context
downloads
```

may require changes.

Then build the website with the candidate Nift binary where practical.

---

# 38. Website self-build is checkpoint evidence

This should be explicitly retained as a Nift practice.

A candidate Nift that passes synthetic tests but cannot build the actual Nift website has not earned a strong checkpoint.

The website is not a replacement for tests.

It is a realistic integration fixture.

---

# 39. Other real-world fixtures

The same concept can extend to:

```text
template collection
React-islands example
documentation example projects
large benchmark projects
```

Use representative fixtures proportionately.

Do not rebuild every known project after every tiny change.

---

# 40. Step 14 — institutional-memory reconciliation

Before calling a substantial checkpoint complete, ask:

> Did this work teach us something that should survive this conversation?

Possible destinations:

```text
HANDOVER.md
DEVELOPMENT.md
TESTING.md
DECISIONS.md
PROJECT-CONTEXT.md
code comments
tests
```

Use the correct layer.

---

# 41. The handover documents are living infrastructure

This must be explicit in the documents themselves.

They are **not one-time Codex onboarding notes**.

Throughout the project's existence:

```text
maintain them
modify them
add to them
correct them
split them when needed
remove obsolete guidance
consolidate repeated lessons
mark superseded decisions
```

whenever project reality changes.

A development checkpoint should include:

> **Handover impact reviewed?**

as a normal consideration.

---

# 42. But do not turn handovers into append-only diaries

This is equally important.

Bad maintenance:

```text
checkpoint 1 note
checkpoint 2 note
checkpoint 3 note
...
checkpoint 97 note
```

until nobody can find current guidance.

Good maintenance:

```text
new evidence
    ↓
update durable rule
    ↓
replace/supersede stale statement
    ↓
preserve detailed chronology in Git/changelog where appropriate
```

The handovers should become **better**, not merely longer.

---

# 43. Tests outrank prose for machine-checkable behavior

If a checkpoint establishes:

```text
@input interpolated dependency A is removed after selector switches to B
```

encode that in a test.

Do not rely only on:

```text
TESTING.md says this should work
```

The hierarchy remains roughly:

```text
test
    → executable contract

code comment
    → local implementation rationale

handover
    → orientation/institutional knowledge

decision ledger
    → why architectural choices exist

Git history
    → historical detail
```

---

# 44. Step 15 — inspect the complete candidate diff

Before declaring a checkpoint:

```text
git diff
git status
```

and related repository inspection are important.

Look for:

```text
temporary debug output
benchmark residue
accidental binaries
generated files
unrelated edits
test fixture debris
version-number mistakes
```

A green suite does not guarantee a clean checkpoint.

---

# 45. Investigative residue

During development it is fine to create:

```text
temporary scripts
probe files
debug output
benchmark fixtures
instrumented builds
```

But before checkpoint declaration, decide deliberately:

```text
keep as permanent fixture
move to appropriate test location
document
or remove
```

Do not let accidental residue become architecture.

---

# 46. Step 16 — checkpoint report

For substantial work, I recommend Codex produce a concise but evidence-rich report.

Suggested structure:

```text
CHECKPOINT CANDIDATE

Scope
-----

Baseline
--------

Implementation
--------------

Tests added/changed
-------------------

Focused validation
------------------

Full validation
---------------

Sanitizers / safety
-------------------

Performance / memory
--------------------

Website / docs
--------------

Handover / decisions
--------------------

Repository state
----------------

Known limitations / deferred work
---------------------------------

Status
------
```

Only include sections relevant to the change.

---

# 47. Report exact evidence where useful

Prefer:

```text
Regression suite: 612/612 passed
```

over:

```text
tests look good
```

Prefer:

```text
Nift website: 35 pages built successfully
```

over:

```text
website okay
```

Prefer:

```text
ASan suite completed without findings
```

over:

```text
sanitizers fine
```

But do not invent precision that the tools did not provide.

---

# 48. Failed validation belongs in the report too

If a candidate still has:

```text
one known Windows failure
```

say so.

A checkpoint can sometimes be useful with a documented limitation, but it must not masquerade as stronger than the evidence supports.

---

# 49. Step 17 — decide checkpoint status

I recommend distinguishing at least informally:

### WORKING

Implementation exists, but significant validation remains.

### CHECKPOINT CANDIDATE

The intended work appears complete and major validation has passed; ready for final review/reconciliation.

### VALIDATED CHECKPOINT

Evidence is sufficient for this state to become the next trusted development baseline.

### RELEASE CANDIDATE

Validated checkpoint undergoing public-release preparation.

### RELEASED

Public action completed intentionally.

This vocabulary prevents ambiguity.

---

# 50. These are statuses, not mandatory Git objects

Do not create:

```text
working tags
candidate tags
validated tags
```

unless Nick decides that is useful.

The vocabulary is primarily for reasoning and reporting.

---

# 51. Promotion between states

Conceptually:

```text
WORKING
   │
   │ focused tests
   ▼
CHECKPOINT CANDIDATE
   │
   │ full/risk-specific validation
   │ docs/handover reconciliation
   ▼
VALIDATED CHECKPOINT
   │
   │ explicit release decision
   ▼
RELEASE CANDIDATE
   │
   │ release validation/approval
   ▼
RELEASED
```

A failure moves the candidate back into development.

---

# 52. Checkpoint scope can span repositories

This is important for the Nift ecosystem.

Suppose Nift embeds Minify++.

A Nift checkpoint involving Minify++ may need identities for:

```text
standalone Minify++
embedded Nift/minifypp
Nift
Nift regression suite
```

Likewise tscc may involve:

```text
tscc implementation
implementation-local regression suite
standalone regression suite
website
```

The checkpoint should identify which repositories actually participate.

---

# 53. Cross-repository coherence

If a checkpoint requires synchronization:

```text
canonical standalone source
        ↓
embedded copy
```

verify it.

Do not assume the copies match because they were intended to.

A byte-for-byte/diff check may be appropriate where synchronization is supposed to be exact.

---

# 54. Canonical ownership matters

Checkpointing should not create competing authorities.

If standalone Minify++ owns its implementation:

```text
Minify++ checkpoint
    → canonical implementation identity
```

Nift's checkpoint should record which Minify++ state it embeds.

Nift should not acquire an independent undocumented fork accidentally.

---

# 55. Regression-suite checkpoints

The external suite can itself have checkpoints.

A suite checkpoint may represent:

```text
new contract coverage
better determinism
new adversarial families
fixture cleanup
benchmark methodology improvement
```

without any Nift source change.

That is valid.

---

# 56. Suite checkpoints should preserve old contract coverage

When reorganizing tests:

```text
new layout
```

must not accidentally mean:

```text
fewer behaviors protected
```

Compare old/new coverage and run counts/categories where meaningful.

---

# 57. Test count is not the checkpoint goal

This deserves explicit repetition.

Bad target:

```text
get from 600 to 700 tests
```

Good target:

```text
systematically cover dynamic dependency lifecycle and parser injection boundaries
```

If that takes 12 tests, 12 is fine.

The methodology optimizes for **failure-family coverage**, not vanity counts.

---

# 58. Bug-family checkpoints

One of the strongest patterns from Nift development was moving from individual bug fixing to bug-family reasoning.

Example:

```text
one malformed watched.json crashes
```

should prompt:

```text
What structural assumptions are unchecked across watched.json,
exts.json, tracked.json and related persistent state?
```

The resulting checkpoint can eliminate a class of failures.

Codex should continue doing this.

---

# 59. Adversarial checkpoints

Occasionally the objective itself should be:

> Find ways to break the current checkpoint.

This was extremely productive for Nift.

An adversarial pass can target:

```text
malformed state
path boundaries
collisions
rapid edits
deleted files
unexpected types
empty collections
large projects
concurrency
parser adjacency
failure recovery
```

The checkpoint is complete when findings have been classified, fixed where appropriate, and encoded as regressions.

---

# 60. A zero-finding audit can still be valuable

If a well-designed adversarial pass finds no defect:

```text
code diff = none
```

but:

```text
confidence increases
```

That can still be a knowledge checkpoint.

Record what was tested.

Do not manufacture source changes merely to justify the audit.

---

# 61. Performance checkpoints should challenge assumptions

Do not benchmark only the workload Nift is already known to excel at.

Useful checkpoint work asks:

```text
What workload would expose the architecture if our assumptions are wrong?
```

Examples:

```text
10k tracked files
large dependency fan-out
many shared partials
no-op incremental
single-page incremental
large JSON
many interpolation expressions
```

This produces meaningful evidence.

---

# 62. Comparison checkpoints

When comparing Nift with other tools:

```text
same conceptual site
same page count
comparable output
document environment
```

as much as practical.

Avoid bending competitors into intentionally poor configurations.

The purpose is understanding, not winning a benchmark graphic.

---

# 63. Do not turn exploratory evidence directly into marketing

The flow should be:

```text
experiment
→ reproduce
→ understand
→ checkpoint evidence
→ decide whether claim is robust enough
→ website/public claim
```

not:

```text
one surprising benchmark
→ homepage
```

---

# 64. Development checkpoint versus product decision

A checkpoint may contain an experiment whose conclusion is:

```text
do not adopt this
```

That is still useful.

For example:

```text
prototype richer templating
→ creates operation/value ambiguity
→ reject
```

The code may be discarded, while the architectural conclusion goes into `DECISIONS.md`.

The checkpoint produced knowledge.

---

# 65. Rejected approaches should sometimes be preserved

Not every rejected micro-implementation belongs in documentation.

But if an idea is likely to recur, record:

```text
REJECTED
reason
revisit condition
```

This prevents future agents from repeatedly rediscovering the same trap.

---

# 66. Checkpoint reversibility

Before risky work, preserve an easy route to the current baseline.

With Git/worktrees this is straightforward.

Do not:

```text
rewrite canonical working tree destructively
```

when a branch/worktree can isolate experimentation.

This is particularly useful for:

```text
architecture changes
large parser rewrites
performance experiments
parallel-agent work
```

---

# 67. Parallel-agent checkpoint model

Codex can do something our earlier workflow only approximated.

Start from:

```text
VALIDATED CHECKPOINT C0
```

then:

```text
             C0
       ┌─────┼─────┐
       ▼     ▼     ▼
      A1    A2    A3
   parser  perf  audit
```

Each agent should preserve:

```text
same known base
bounded responsibility
independent findings
validation evidence
```

Then reconcile.

---

# 68. Agent results are not automatically merge candidates

An agent can produce:

```text
useful analysis
failed experiment
better test
inferior implementation
unexpected benchmark
```

without its code needing to merge.

Treat agents as evidence producers, not automatic patch generators.

---

# 69. Parallel implementations should be compared

If two agents solve the same problem:

```text
implementation A
implementation B
```

compare:

```text
correctness
simplicity
performance
memory
testability
architectural fit
```

Then choose deliberately.

This is exactly the kind of situation checkpoint discipline handles well.

---

# 70. Integration checkpoint after parallel work

Do not call the best individual branch the new baseline immediately.

Create/reconcile:

```text
integrated candidate
```

then rerun validation on the integrated state.

Individual green branches do not prove the merge is green.

---

# 71. Checkpoint granularity

Checkpoint too frequently and the concept loses meaning.

Checkpoint too rarely and development becomes difficult to reason about.

A useful heuristic:

> Create a checkpoint when you would be comfortable telling the next developer or agent, “Start from here.”

That is probably the best definition of appropriate granularity.

---

# 72. Small fixes between checkpoints

Not every fix requires ceremony.

Several tightly related small fixes may accumulate into one validated checkpoint.

But ensure the final report describes them.

---

# 73. Large feature checkpoints can have internal stages

For something like `$[...]` parameter interpolation:

```text
C0 — baseline

W1 — behavioral contract captured

W2 — resolver implemented

W3 — directive integration works

W4 — dynamic dependency lifecycle works

C1 — full validated checkpoint
```

Only C0 and C1 need necessarily be treated as durable baselines.

The W states are useful development milestones.

---

# 74. Checkpoints and version numbers

Do not force a version bump for every checkpoint.

Version policy should be separate.

A project might have:

```text
v1.0.42
```

with several internal checkpoints before:

```text
v1.0.43
```

if that matches the project's release/versioning policy.

Conversely, a release version should correspond to a validated release candidate.

---

# 75. Checkpoints and changelogs

Not every checkpoint belongs in the public changelog.

Public changelog:

```text
user-visible/release-relevant changes
```

Checkpoint report:

```text
development evidence and internal state
```

Keep those purposes distinct.

---

# 76. Checkpoints and Git history

Git history is the detailed forensic record.

Checkpoint documentation should not duplicate every diff.

Instead record:

```text
why this state matters
what was validated
what was learned
what remains
```

Git answers:

```text
exactly what changed
```

---

# 77. Checkpoint provenance

For future reproducibility, a checkpoint report should ideally identify:

```text
date
repository/commit(s)
toolchain where relevant
test suite identity
benchmark fixture identity
```

especially for performance/release checkpoints.

Do not overburden ordinary feature checkpoints with unnecessary metadata.

---

# 78. Environmental sensitivity

Some evidence depends on environment.

Examples:

```text
timings
RSS
filesystem timestamp behavior
compiler warnings
sanitizer behavior
platform path semantics
```

Record environment when it materially affects interpretation.

---

# 79. Platform checkpoints

If/when Nift begins serious:

```text
Linux
macOS
Windows
```

validation, platform support should become part of release checkpoint evidence.

Do not imply cross-platform validation merely because the code is portable-looking C++.

---

# 80. Release checkpoint bar should be higher

A normal validated development checkpoint might not require:

```text
every platform
packaged binaries
website publication dry-run
release notes
```

A release candidate may.

This prevents normal development from becoming painfully heavyweight while keeping releases disciplined.

---

# 81. Failed checkpoint candidates

A candidate that fails validation should not be hidden conceptually.

It becomes:

```text
working state
```

again.

Investigate.

Fix.

Rerun relevant evidence.

No shame or special ceremony is needed.

The important part is not promoting it prematurely.

---

# 82. Partial success

Suppose a performance experiment achieves:

```text
20% faster
```

but:

```text
2× memory
```

and that tradeoff is undesirable.

The checkpoint conclusion may be:

```text
experiment rejected
baseline unchanged
```

This is a successful development investigation.

Do not equate success with merging code.

---

# 83. Checkpoint review questions

Before promotion, ask:

1. Did we accomplish the bounded objective?
2. Are the new semantics explicitly tested?
3. Does the previous suite still pass?
4. Did we test the highest-risk interactions?
5. Are failures controlled rather than corrupting state?
6. Are sanitizer/performance checks appropriate to this change complete?
7. Are docs consistent?
8. Is the website consistent where relevant?
9. Are handover/decision docs consistent?
10. Is the working tree free of accidental residue?
11. Are sibling/canonical copies synchronized where required?
12. Are remaining limitations stated?
13. Would I tell another developer to start from this state?

If #13 is no, it is probably not yet a validated checkpoint.

---

# 84. Checkpoint evidence should not be exaggerated

Use wording such as:

```text
validated against...
tested on...
no failures observed in...
```

rather than:

```text
proven bug-free
```

A checkpoint records evidence, not certainty.

---

# 85. Historical numbers should remain historical

If a checkpoint report says:

```text
492 assertions passed
```

that is evidence for that checkpoint.

Do not mechanically propagate `492` into evergreen documentation after the suite grows.

Likewise benchmark values should retain fixture/environment context.

---

# 86. Preserve the distinction between fact and interpretation

A checkpoint may establish:

```text
FACT:
10k-page no-op incremental took X under fixture Y.

INTERPRETATION:
Nift's incremental architecture appears extremely efficient for this workload.
```

Both can be useful, but they are different.

This aligns with the broader handover status vocabulary.

---

# 87. Checkpoints and current source authority

If an old checkpoint report says one thing and current source/tests say another:

```text
current repository
```

wins for current implementation behavior.

The old checkpoint remains historical evidence.

Do not “correct” current behavior merely to make it match an old checkpoint unless the change was accidental and the contract supports restoration.

---

# 88. Checkpoints and decision authority

Likewise:

```text
checkpoint report:
we considered X

DECISIONS.md:
X is REJECTED
```

The decision ledger is the clearer source for architectural status.

Use each document for its intended role.

---

# 89. Long-running project maintenance

As Nift evolves over years, old checkpoint mechanics may become obsolete.

For example, future CI might automatically produce:

```text
cross-platform builds
sanitizer runs
contract suites
benchmark reports
website preview
```

If so, checkpoint methodology should evolve to use those systems.

Do not preserve manual procedures for nostalgia.

---

# 90. Automation is desirable where it strengthens evidence

Good candidates:

```text
run all suites
verify embedded Minify++ synchronization
build Nift website
run representative benchmarks
check generated fixtures
```

Automation reduces checkpoint mistakes.

But avoid building an enormous release system before the project needs it.

---

# 91. Machine-readable checkpoint data

**FUTURE POSSIBILITY**

If the ecosystem grows, a small generated manifest could identify:

```text
Nift commit
Minify++ commit
suite commit
website commit
toolchain
test result
```

But do not build this merely because it sounds sophisticated.

Plain Markdown + Git may remain entirely sufficient.

---

# 92. The checkpoint methodology should apply to Minify++

For Minify++, a substantial semantic checkpoint might involve:

```text
HTML corpus
CSS corpus
JavaScript corpus
differential behavior
malformed-input behavior
size reduction
performance
sanitizers
Nift embedded synchronization
website claims
```

The exact validation differs, but the baseline→evidence→checkpoint model is the same.

---

# 93. The methodology should apply to tscc

For tscc, checkpoint evidence may be even more important because compiler correctness has many dimensions:

```text
parse
type behavior
lowering
emitted JavaScript
runtime behavior
module behavior
diagnostics
compatibility
performance
```

A parser accepting syntax does not establish compiler support.

A tscc feature checkpoint should validate the whole relevant pipeline.

---

# 94. tscc differential testing

Where possible, compare:

```text
TypeScript/reference behavior
vs
tscc behavior
```

for well-defined supported semantics.

A feature checkpoint should distinguish:

```text
syntax accepted
code emitted
runtime equivalent
```

rather than conflating them.

---

# 95. Ecosystem-level checkpoints

Occasionally a meaningful milestone may span all projects:

```text
Nift release candidate
+
canonical Minify++ integration
+
website/documentation refresh
+
AI handover infrastructure
```

That can be treated as an ecosystem checkpoint.

Use sparingly.

Most development should remain project-local.

---

# 96. Human review remains part of promotion

Codex can gather enormous amounts of evidence autonomously.

But decisions such as:

```text
is this the architecture we want?
should this become public?
should we release now?
should we change compatibility?
```

remain Nick's decisions unless explicitly delegated.

Codex can recommend promotion.

It should not silently convert a checkpoint candidate into a public release.

---

# 97. The ideal Codex checkpoint report

An ideal final message might look conceptually like:

```text
Parameter-interpolation checkpoint candidate is complete.

Baseline:
  <commit>
  existing suite: X/X

Implementation:
  extracted existing value resolver
  added single-pass textual parameter interpolation
  wired @input/@dep/@json-path/@pathto

New contract coverage:
  whole value
  mixed literal/value
  lexical scope
  non-recursive generated text
  A→B dependency lifecycle
  traversal parity

Validation:
  local C++: ...
  external suite: ...
  ASan: ...
  UBSan: ...
  website: ...
  benchmark: no material regression

Documentation:
  template-language docs updated
  AI context updated
  DECISIONS.md updated
  TESTING.md updated

Repository:
  no commit/tag/push performed
  working tree contains only intended changes

Remaining:
  ...

Status:
  VALIDATED CHECKPOINT CANDIDATE
  Suitable to become the next baseline after review.
```

That gives Nick considerably more useful information than:

```text
Done, tests pass.
```

---

# 98. The checkpoint mindset in one sentence

The fundamental question is not:

> **“Did we finish coding?”**

It is:

> **“Have we accumulated enough evidence that this coherent state should replace the previous state as the one we trust?”**

That is checkpoint-oriented development.

---

# 99. The relationship to Nift's broader philosophy

There is an interesting parallel between the product and the development methodology.

Nift itself increasingly favors:

```text
small explicit mechanisms
predictable semantics
little hidden magic
```

The checkpoint methodology does something similar for development:

```text
explicit baseline
explicit objective
explicit evidence
explicit promotion
```

rather than:

```text
latest branch must be best
```

That fit was not planned, but it has served the project well.

---

# 100. Final guidance to Codex

Please preserve the checkpoint style, but **improve its mechanics now that you have direct repository access**.

Do not imitate the old ChatGPT workflow mechanically.

Replace:

```text
ZIP snapshots
manual file transfer
conversation-only provenance
```

with:

```text
Git provenance
worktrees
repeatable scripts
repository-local tests
living handover documents
clear checkpoint reports
```

while preserving:

```text
known baseline
bounded development
adversarial investigation
cumulative regression protection
proportional validation
real-world integration testing
evidence-backed performance claims
documentation reconciliation
institutional-memory maintenance
deliberate promotion
```

Most importantly:

> **Never silently let “the newest state” become “the trusted state.” Make it earn that promotion through evidence.**

And when a checkpoint teaches us something new, preserve the durable lesson in the appropriate project documentation so that future developers and coding agents start from the accumulated understanding rather than having to rediscover it.

I think **this plus the previous website/checkpoint handover completes an important missing part of the transfer**.

There’s also a reason I’d want Codex to absorb this before it starts the `$[...]` work. That feature is almost a perfect example of the methodology: the obvious implementation may be only a modest parser change, but the *checkpoint* isn't really earned until we've established the value semantics, protected the non-recursive boundary, tested lexical scope, proven the A→B dependency lifecycle, checked requirements, run the old regression corpus, exercised sanitizers, checked performance, rebuilt the real website, and reconciled the docs and handovers.

And I particularly like one criterion from the handover as the practical definition:

> **Would I tell another developer or agent, “Start from here”?**

If yes, we've probably reached a real checkpoint. If the answer is “well, use this version, but don't touch watch mode yet, ignore these three unexplained failures, and the docs describe the previous behavior,” then we haven't.

## v4.0.3 development checkpoints (2026-08-19)

0. Baseline + contract freeze for the bounded v4.0.3 programme.
1. Exactly-one executed `@content` contract.
2. Condition language: short-circuit `&&` / `||` (retain `!`; parentheses if clean).
3. Lazy ternary expressions using the shared condition evaluator.
4. `@join`.
5. UTF-8-safe `@substr(value, pos, length)`.
6. Pagination contract + single-threaded semantics.
7. Pagination lifecycle/persistence/transactionality.
8. Multithreaded pagination.
9. `nift.dev/install` installer.
10. Strict Snap edge/candidate experiment.
11. Full hardening/equivalence reconciliation.
12. Website/release reconciliation, committing generated public/master before source stage.

### Checkpoint 12 reconciliation result (2026-08-19)

The public website/release reconciliation documents the v4.0.3 condition, ternary, join, substring and pagination contracts; adds the extensionless installer source/endpoint; refreshes Battle Tested and production-readiness evidence; and builds the complete Nift documentation site with the exact v4.0.3 candidate. The candidate built 59 tracked site outputs successfully, an immediate `nift status` reported all 59 up to date, the generated-site local link audit checked 7,338 `href`/`src` references with zero missing targets, the installer source/generated copy remained byte-identical and passed `sh -n`, and the core `test-installer` smoke gate passed again.

Website publication preserved the required order: generated `public/main` commit `478c617` (`Publish v4.0.3 language and pagination docs`) first, then authoritative source `stage` commit `c5d0d94` (`Document v4.0.3 language and pagination`). Both website trees were clean immediately after those commits.
