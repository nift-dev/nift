# Website work at development checkpoints

> This is the living operational guide for coordinating website changes with product checkpoints. Keep it aligned with current branch topology, build commands, and publication policy.

Website coordination requires operational detail beyond the general checkpoint philosophy.

I have already told it quite a lot about the philosophy of checkpoints, regression preservation, packaging, website self-builds, and not treating every test change as a release. But I have **not yet given it a dedicated handover explaining how we actually worked through checkpoints**, how website changes followed development checkpoints, how I treated source/site/suite artifacts together, how I validated them, and how the checkpoint model differed from ordinary “make a few commits and move on” development.

That is worth preserving because our checkpoint workflow became a pretty important part of how Nift evolved safely.

I’d send Codex something like this:

# Nift Checkpoint Development & Website Maintenance Handover

This document supplements the existing Nift project-context, behavioral, and implementation handovers.

Its purpose is to explain a development practice that became important during the recent Nift/Minify++/tscc work:

> **checkpoint-oriented development**

and, specifically, how the websites, test suites, benchmarks, documentation, and packaged artifacts were updated around those checkpoints.

This is partly historical context and partly a recommended working style.

The current Git repositories remain authoritative for present branches, commands, versions, and exact workflows.

---

# 1. What “checkpoint” meant in our development workflow

During much of the previous ChatGPT development work, we were not operating through a polished GitHub release pipeline.

Instead, the practical unit of progress became a **validated checkpoint**.

A checkpoint was not simply:

```text
some code compiles
```

or:

```text
some commits exist
```

It was closer to:

```text
source state
    +
matching tests
    +
matching documentation
    +
matching website where relevant
    +
benchmark/sanitizer evidence where relevant
    +
packaged artifacts
    =
named checkpoint
```

A checkpoint represented:

> **a coherent state of the project that we could safely use as the baseline for the next round of development.**

That distinction matters.

---

# 2. Checkpoint versus release

**Status: IMPORTANT HISTORICAL DISTINCTION**

A checkpoint was not necessarily a public release.

For example, development often produced things conceptually like:

```text
Nift source checkpoint
Nift regression-suite checkpoint
Nift website checkpoint
```

that all represented the same development era.

We might then continue hardening from that state without publicly publishing it.

So:

```text
checkpoint
    = internally validated development baseline

release
    = intentionally published public version
```

Do not automatically publish every checkpoint.

Do not automatically bump a public version because a new internal checkpoint exists.

---

# 3. Why checkpoints were useful

The checkpoint style gave us several important properties.

## 3.1 Known-good recovery point

After an aggressive audit or rewrite:

```text
if next experiment goes badly
    ↓
return to known validated checkpoint
```

rather than trying to remember which intermediate state was healthy.

## 3.2 Matched artifacts

A major benefit was avoiding situations like:

```text
source from Monday
+
tests from Wednesday
+
website from Friday
```

being treated as one coherent package.

A checkpoint tried to keep related artifacts aligned.

## 3.3 Easier reasoning

We could say:

```text
"Start from v1.0.15 merged-final"
```

rather than:

```text
"Start from whichever combination of the last four partially overlapping
branches happened to contain all the fixes."
```

## 3.4 Controlled experimentation

We could branch conceptually from a checkpoint:

```text
checkpoint
    ↓
experiment
    ↓
validate
    ↓
new checkpoint
```

without redefining the baseline prematurely.

---

# 4. The general checkpoint progression

The workflow often looked approximately like:

```text
known-good checkpoint
        ↓
choose bounded audit / feature / refactor
        ↓
establish baseline tests
        ↓
inspect implementation
        ↓
add hostile tests / reproduce issue
        ↓
modify source
        ↓
focused validation
        ↓
full suite
        ↓
sanitizers / benchmarks where relevant
        ↓
update docs
        ↓
update website where claims/behavior changed
        ↓
rebuild website with candidate binary
        ↓
package source/suite/site
        ↓
sanity-check packages
        ↓
declare new checkpoint
```

That was the basic development rhythm.

---

# 5. A checkpoint should be internally coherent

Before calling something a checkpoint, I tried to answer:

```text
Does the source match the tests?

Do the docs match the source?

Does the website match the docs?

Do benchmarks describe this implementation?

Does the website build with this implementation?

Are package filenames/version statements coherent?

Have old temporary branches/artifacts been reconciled?
```

If the answer was no, it was still an intermediate working state.

---

# 6. The checkpoint did not need to be perfect

A checkpoint could still have:

```text
known future work
deferred ideas
unimplemented features
areas needing more testing
```

The important criterion was not:

> “nothing remains to do”

It was:

> “we know what this state is, what has been validated, and what comes next.”

---

# 7. How I treated website work during Nift development

The website was not maintained as an unrelated marketing project.

It served several roles:

```text
documentation
product positioning
real-world Nift project
integration fixture
release evidence
AI onboarding material
```

That meant website changes were often tied directly to development checkpoints.

---

# 8. Website changes followed product truth

The core rule was:

> **Change the website when the product truth changed.**

Examples:

```text
language feature removed
→ remove/rewrite documentation

syntax changed
→ update examples

output directory changed
→ update scaffold/docs/examples

new JSON/loop functionality
→ document it

Minify++ integration changed
→ update minification docs

testing methodology improved
→ update Battle Tested page

performance measurements changed
→ update benchmark claims

positioning changed
→ update homepage/Why Nift/AI pages
```

I tried not to let the website become a stale snapshot of an older Nift architecture.

---

# 9. The major website rewrite followed the stripped architecture

When Nift lost much of the older:

```text
LuaJIT
ExprTk
system scripting
hooks
```

machinery, simply patching a few documentation lines would have been inadequate.

The product story itself had changed.

So the website was reoriented around:

```text
small website generator
templates
dependencies
incremental builds
ordinary web technologies
composability
speed
AI-assisted development
```

rather than presenting Nift as a broad programmable environment.

This was a **product identity checkpoint**, not merely a docs edit.

---

# 10. Website copy was treated as part of architecture communication

Several phrases were deliberately added because they explained the new architecture:

> Keep your HTML. Keep your tools. Stop repeating yourself.

> Nift provides the glue without trying to become the universe.

Those statements came from development conclusions.

If the architecture changes enough that these statements become untrue, they should change too.

---

# 11. The website was also used to test terminology

We refined terms through actual website copy.

A notable example is:

```text
static site generator
```

versus:

```text
website generator
```

The latter is now preferred because Nift-generated sites can contain runtime applications and frontends.

Website copy therefore sometimes acted as a forcing function:

> Can we explain what Nift actually is clearly?

That is useful product-development feedback.

---

# 12. Website design changes were checkpointed separately from product behavior

There were also many purely visual iterations:

```text
dark mode
green gradient
theme toggle
banner redesign
mobile menu fixes
card spacing
demo height
removing grid lines
template screenshots
responsive fixes
```

These did not require Nift executable version changes.

They could still produce a **website checkpoint**.

So:

```text
Nift source version
```

and:

```text
Nift website checkpoint/version
```

were not automatically the same number or concept.

Codex already noticed this.

Preserve that distinction.

---

# 13. Website checkpoint naming

Historically, packaged website artifacts sometimes had names like:

```text
nift-website-...
```

with a descriptive or numbered checkpoint suffix.

These were practical working artifacts.

Do not assume the exact old filename convention is a formal future release policy.

The useful principle is:

```text
checkpoint artifact name
    should clearly identify
which project state it represents
```

---

# 14. Website modification workflow

When making a website change tied to product development, my ideal flow was:

```text
1. Inspect current website source.
2. Identify every page affected by the product change.
3. Search globally for old terminology/syntax.
4. Change canonical source files.
5. Rebuild website.
6. Inspect generated output.
7. Check links/assets.
8. Test responsive/design behavior if relevant.
9. Run Lighthouse/performance checks where relevant.
10. Rebuild again using the exact candidate Nift binary.
11. Package or preserve the validated source/site checkpoint.
```

Do not edit one obvious page and assume the old behavior is gone everywhere.

---

# 15. Global searches were important

Whenever terminology or syntax changed, I frequently searched across the whole website for stale forms.

Examples:

```text
output/
LuaJIT
ExprTk
@pathtopage
old @pathto examples
backtick quoting
old Sift naming
static site generator
old version numbers
```

This is especially important after renames or language simplifications.

A website can look correct while one obscure docs page retains stale instructions.

---

# 16. Website source should remain canonical

Where the website has source and generated/deployment state:

```text
source
    → edit here

generated output
    → rebuild
```

Do not hand-edit generated output and forget to change the source.

Codex has already identified that Nift's website currently has a more unusual branch/deployment arrangement.

Verify the exact current branch workflow, but preserve the principle:

> canonical source owns content; generated site is a build artifact.

---

# 17. Website build as integration test

This became an important habit.

After a candidate Nift build passed the regression suite, I would also build the actual Nift website with it.

Conceptually:

```text
candidate Nift binary
        ↓
Nift website source
        ↓
successful generated website
```

This provides a realistic integration fixture containing:

```text
real templates
real pages
real assets
real paths
real docs
real project structure
```

It complements adversarial tests.

---

# 18. Why website self-build matters

A synthetic regression fixture can prove a parser boundary.

The website can prove:

```text
the product still builds a substantial real Nift project
```

Neither replaces the other.

Use:

```text
regression suite
+
real website
```

as complementary evidence.

---

# 19. Website generation time was sometimes recorded

At some checkpoints, website builds were reported with actual Nift timing output.

For example, a development report might include:

```text
35 pages built successfully
time taken: ...
```

This was useful as a lightweight smoke benchmark.

Do not turn site build time into a strict benchmark threshold.

Its primary purpose was:

```text
candidate binary successfully built real website
```

---

# 20. Website claims were updated after evidence changed

Examples include:

```text
10k benchmark results
incremental-build behavior
fan-noise observations
AI development claims
Battle Tested categories
```

The rule was:

> do not make the website claim stronger than the evidence.

If a benchmark is replaced, update the claim.

If the fixture changes, update the context.

If the old claim can no longer be reproduced, remove or qualify it.

---

# 21. The Battle Tested page evolved with testing

As the regression work became more adversarial, the website's testing story changed from:

```text
we have tests
```

toward:

```text
we attack parser boundaries
state corruption
filesystem safety
incremental invalidation
scaling
etc.
```

This is important.

Do not update the page merely by incrementing:

```text
245 tests
→ 492 tests
→ 1000 tests
```

The durable story is **what kind of failure families are tested**.

---

# 22. Raw test counts should be checkpoint evidence, not eternal identity

A test count is valid for a particular checkpoint.

For example:

```text
checkpoint X
    → N assertions
```

Later:

```text
checkpoint Y
    → M assertions
```

A website sentence hardcoding `N` can become stale immediately.

Where possible:

```text
historical release notes
    → exact count okay

current evergreen marketing
    → emphasize methodology
```

---

# 23. Website examples should be tested examples

When adding Nift syntax to the website, ideally the example should be:

```text
valid current syntax
```

rather than prose invented from memory.

This became especially important after mistakes involving:

```text
@pathto
tracked names
concrete output paths
```

If practical, use examples taken from real fixtures/projects.

---

# 24. `@pathto` documentation was repeatedly corrected

One recurring misunderstanding was using generated paths where Nift expected a tracked name.

This taught us:

> documentation must reflect semantic categories, not merely produce plausible-looking strings.

Codex should be alert to this when updating examples for `$[...]` interpolation.

For example, do not create a dynamic `@pathto` example unless it is semantically valid for the current argument type.

---

# 25. Documentation examples became regression candidates

When an example represented an important feature, it was often valuable to turn its behavior into a test.

The ideal relationship is:

```text
docs example
    ↓
describes contract

test
    ↓
enforces contract
```

This reduces doc drift.

Not every prose example needs a test, but major syntax examples should preferably correspond to tested behavior.

---

# 26. AI-oriented docs were maintained alongside language changes

The site gained an AI-assisted development page and downloadable/project context material.

This meant language changes had an extra update surface:

```text
human docs
+
AI context/examples
```

When `$[...]` parameter interpolation lands, update both if both currently teach directive parameters.

Future agents should not learn old limitations from an AI context file.

---

# 27. Template collection work was also checkpoint-oriented

The Nift site eventually included a larger template collection.

Templates were treated as real projects:

```text
build from current barebones structure
verify with Nift
capture screenshots
package downloads
update website
```

When Nift behavior changes materially, representative templates should still build.

They can serve as additional integration fixtures.

---

# 28. Barebones project mattered

The barebones project/archive was used heavily as:

```text
starting fixture
AI onboarding artifact
test base
template base
```

So changes to:

```text
default scaffold
config
public/
template structure
```

should be reflected in the barebones project too.

A checkpoint is inconsistent if:

```text
docs say new scaffold
but downloadable barebones still contains old scaffold
```

---

# 29. Website visual validation

When changing site design, we iterated by inspecting actual output rather than assuming CSS was correct.

Issues fixed included things like:

```text
mobile menu
overflow
double scrollbars
demo height
spacing
banner graphics
template images
theme controls
```

Codex now has direct machine/browser access, so it should use actual rendered inspection where available.

Do not rely only on source diff for visual changes.

---

# 30. Lighthouse as checkpoint evidence

Lighthouse was used as a practical web-quality check.

Reported milestones included very strong desktop results and later mobile improvements.

Treat Lighthouse like benchmark evidence:

```text
recorded at checkpoint
```

not:

```text
permanent truth
```

If website architecture changes significantly, rerun it.

---

# 31. Website checkpoint should not overtake product truth

Sometimes website work can become attractive enough that there is a temptation to:

```text
change product behavior
because the website would be easier to explain
```

Avoid that.

Product architecture comes first.

Website explains the product.

It does not dictate behavior merely for a cleaner feature table.

---

# Part II — Development Checkpoint Style

# 32. Checkpoints usually followed meaningful engineering milestones

Examples of checkpoint-worthy milestones included:

```text
major regression-suite expansion
bug-family hardening pass
architecture merge
new JSON/control-flow capability
collision/scaling fix
memory optimization
Minify++ hardening
website redesign
canonical suite consolidation
```

Not every two-line fix needs a named checkpoint.

---

# 33. A checkpoint could aggregate several related fixes

For example, one ruthless audit might find:

```text
state validation issue
CLI status issue
path collision issue
watch inconsistency
```

If these belong to one hardening phase and are validated together, one checkpoint may represent the resulting coherent state.

Do not create arbitrary micro-checkpoints just because every bug had its own change.

---

# 34. But avoid giant unrelated checkpoints

The opposite is also bad:

```text
parser feature
+
website redesign
+
Minify++ rewrite
+
compiler work
+
release automation
```

all under one checkpoint makes attribution difficult.

A checkpoint should still represent a coherent phase.

---

# 35. Checkpoint reports were evidence-rich

A good checkpoint summary included things such as:

```text
what changed
bugs fixed
tests added
full test result
benchmark result
website build result
artifact names
remaining concerns
```

This was much more useful than:

```text
v1.0.15 done
```

Codex should preserve this reporting style.

---

# 36. Example checkpoint report shape

Something like:

```text
Checkpoint: Nift vX.Y.Z hardening

Source:
    clean build successful

New fixes:
    ...

Focused tests:
    ...

Full regression:
    N tests / 0 failures

Sanitizers:
    ...

Performance:
    ...

Website:
    built successfully with candidate binary

Artifacts:
    source
    suite
    website

Remaining:
    ...
```

Exact format can evolve.

The principle is reproducibility.

---

# 37. Checkpoints were often packaged

Because the previous workflow involved ChatGPT file exchange, checkpoints were frequently exported as ZIPs.

Typical package classes included:

```text
source
regression suite
website
```

and for related projects:

```text
Minify++ source
Minify++ website
tscc source
tscc suite
tscc website
```

That made artifact matching very explicit.

---

# 38. ZIP packaging was a workflow adaptation, not project doctrine

Codex now works directly on the user's machine and repositories.

Therefore it should **not reproduce ZIP-heavy development unnecessarily**.

The enduring idea is:

```text
validated coherent checkpoint
```

not:

```text
always make three ZIP files
```

Git/worktrees/tags/branches may represent checkpoints more naturally now.

---

# 39. Do not automatically commit checkpoints

Codex already correctly noted:

> writing files does not imply making Git commits.

Likewise:

```text
checkpoint prepared
```

does not mean:

```text
commit/tag/push without approval
```

A checkpoint can exist as:

```text
reviewed working tree
candidate branch/worktree
named local state
prepared patch
```

until the user decides how to preserve it.

---

# 40. Checkpoint metadata should become easier with Git

Now that Codex operates directly in Git, I would expect checkpoint reports to include:

```text
repository
branch
commit/base commit
working-tree status
```

This is better provenance than a ZIP filename alone.

---

# 41. A checkpoint should begin from a known baseline

Before starting a new bounded task:

```text
record current commit
record branch
run baseline tests
record known failures
```

Then the checkpoint report can distinguish:

```text
pre-existing problem
```

from:

```text
regression introduced during task
```

---

# 42. Intermediate work should not be called canonical prematurely

This happened during the parallel Nift audit branches.

Two branches each contained good work.

Neither should have become “the new Nift” until they were reconciled.

The final checkpoint came after comparing actual source differences and combining the strongest implementations.

This is a useful precedent for Codex parallel agents.

---

# 43. Parallel worktree checkpoint discipline

If Codex uses several agents:

```text
base checkpoint
    ├── parser audit
    ├── state audit
    ├── performance audit
    └── docs audit
```

each branch/worktree should report:

```text
base
scope
tests
changes
remaining issues
```

Then merge deliberately.

Do not automatically merge every agent's modifications.

---

# 44. Compare semantic differences before merging

During one parallel audit, both branches implemented duplicate JSON-key rejection differently.

Rather than choosing a branch wholesale, we compared the implementations.

That should remain the model:

```text
parallel solution A
parallel solution B
        ↓
understand actual differences
        ↓
choose/merge best reasoning
```

not:

```text
agent A finished first
→ merge A
```

---

# 45. Checkpoint after a feature should include the behavioral suite

For a change like `$[...]` parameter interpolation, a checkpoint should include:

```text
source implementation
+
new contract tests
+
implementation tests
+
documentation
+
updated handover/decision docs
```

These form one logical checkpoint.

---

# 46. Checkpoint after a performance change should include evidence

If code changes for speed or memory:

```text
baseline measurements
candidate measurements
same fixture
same methodology
correctness suites green
```

should be part of the checkpoint evidence.

Do not declare:

```text
performance checkpoint
```

based only on intuition.

---

# 47. Checkpoint after a parser change should include adversarial boundaries

Likewise:

```text
feature works
```

is insufficient.

Parser checkpoints should explicitly cover:

```text
malformed forms
adjacency
escaping
scope
ordinary web syntax
existing compatibility
```

---

# 48. Website changes should be tied to checkpoint type

A useful decision rule:

```text
Does this checkpoint change anything users need to know?
    ↓
yes → inspect docs/site

Does it change claims?
    ↓
yes → update claims

Does it change only internals/tests?
    ↓
probably no website update

Does it add meaningful new capability?
    ↓
likely docs/examples/site update
```

Avoid website churn for every internal refactor.

---

# 49. Examples of checkpoint/site coupling

### Internal hash implementation change

```text
site update?
probably no
```

unless benchmark/public claim changes.

### New `$[...]` parameter interpolation

```text
site update?
yes
```

because template-language documentation changes.

### Better malformed JSON handling

```text
site update?
possibly Battle Tested/changelog only
```

not necessarily main docs.

### Performance memory improvement

```text
site update?
only if memory/performance claims are published
```

### Minify++ internal semantic fix

```text
Nift site?
only if Nift docs/claims are affected
```

---

# 50. Documentation and website should be updated after behavior is settled

I generally prefer:

```text
implement
test
confirm semantics
        ↓
then finalize docs/site copy
```

rather than polishing extensive website copy before the implementation contract is stable.

Documentation can be drafted earlier, but final claims should follow evidence.

---

# 51. Use the candidate binary for documentation examples where feasible

After a feature lands:

```text
candidate Nift
    ↓
build example
    ↓
verify output
    ↓
then publish example
```

This prevents docs from describing syntax that never actually worked.

---

# 52. Checkpoint artifacts should be sanity-checked after packaging

Historically, before presenting a ZIP checkpoint, I tried to inspect:

```text
filename
internal version text
expected directory structure
presence of tests
absence of obvious build junk
```

With Git-based work, analogous checks should happen for release candidates.

---

# Part III — Suggested Checkpoint States for Future Codex Work

# 53. I would use three informal checkpoint levels

This is a **new recommendation**, not historical formal policy.

### Working checkpoint

```text
bounded change mostly works
focused tests pass
not yet fully validated
```

Useful inside a worktree.

### Validated checkpoint

```text
full intended validation passed
docs/handover reconciled
candidate is coherent
```

This is the normal handoff point to Nick.

### Release candidate

```text
validated checkpoint
+
packaging
+
website
+
release checks
+
public-facing version/release preparation
```

This terminology could help Codex avoid conflating progress with release readiness.

No need to encode it bureaucratically unless useful.

---

# 54. Checkpoint reports should identify level

For example:

```text
Status: validated checkpoint
Not published
```

This is very clear.

---

# 55. Preserve prior checkpoint until candidate is validated

When working on a risky change, do not destroy the previous known-good state.

Git makes this easy.

The workflow should conceptually remain:

```text
known good
    ↓
candidate work
    ↓
validation succeeds
    ↓
candidate becomes next baseline
```

---

# Part IV — Website Handover Maintenance

# 56. Website handovers must also be living documentation

The earlier handover-maintenance requirement applies strongly here.

If:

```text
branch structure changes
build command changes
deployment changes
asset pipeline changes
candidate executable policy changes
```

then the website `HANDOVER.md` must be updated.

A future Codex should not have to rediscover the deployment topology from Git oddities if we already know it.

---

# 57. Site design decisions worth preserving

The website handover should capture durable design direction, not every pixel adjustment.

Useful durable points include:

```text
clean/minimal layout
dark-mode-friendly
green gradient identity
ordinary web technologies
strong responsive behavior
avoid unnecessary heavy JS
documentation clarity over marketing spectacle
```

Historical one-off details like:

```text
this card needed 12px more margin
```

do not belong unless they reveal a general rule.

---

# 58. Website content decisions worth preserving

Examples:

```text
call Nift website generator
do not foreground removed scripting
@dep is advanced
@pathto examples must use correct semantic category
public/ is modern convention
minification is opt-in
AI claims should be evidence-based
test count is not main quality claim
```

These are durable.

---

# 59. Website screenshot/template workflow

When templates/examples are updated:

```text
build real template
inspect it
capture representative screenshot
update download artifact
update metadata/card
verify link
```

Do not show a screenshot that does not match the downloadable template.

That drift happened easily during iterative site work and was corrected.

---

# 60. Deployment branch must not become documentation authority

If `main` is generated deployment state while `stage` is authoritative source:

```text
handover docs
source content
development docs
```

belong with source.

Do not update generated `main` directly and assume the source will somehow catch up.

---

# Part V — How Checkpoint Style Should Enter Durable Handover Docs

# 61. Add a “Checkpoint Development” section to Nift `DEVELOPMENT.md`

I would explicitly document something like:

> Nift development commonly proceeds through validated checkpoints. A checkpoint is a coherent source/test/documentation state that has passed the validation appropriate to its changes. Checkpoints are development baselines, not automatically public releases.

Then show:

```text
baseline
→ bounded work
→ focused tests
→ full validation
→ docs/site reconciliation
→ checkpoint report
```

---

# 62. Add checkpoint reporting expectations

For substantial work:

```text
Baseline
Scope
Changes
Tests added
Focused result
Full result
Sanitizers
Performance/memory if relevant
Website/docs impact
Handover impact
Remaining concerns
Checkpoint status
```

This is a good operational template for future Codex runs.

---

# 63. Add a website checkpoint rule to website handover

Something like:

> When a product checkpoint changes documented behavior, syntax, terminology, benchmark evidence, or product claims, reconcile the website source before considering the product checkpoint complete.

And:

> Build the site with the candidate Nift executable when practical.

---

# 64. Checkpoint style should remain flexible

Do not turn it into a rigid release bureaucracy.

A typo fix does not need:

```text
checkpoint ceremony
benchmark suite
website rebuild
13-field report
```

Use it proportionately for meaningful development phases.

The important idea is **coherent validated baselines**.

---

# 65. Checkpoints should accumulate knowledge

This ties directly into the living handover requirement.

Every meaningful checkpoint should ask:

```text
Did we learn something future maintainers should know?
```

If yes, update:

```text
DECISIONS.md
TESTING.md
PROJECT-CONTEXT.md
DEVELOPMENT.md
```

where appropriate.

So a checkpoint is not merely:

```text
new code
```

It can advance institutional memory too.

---

# 66. Historical checkpoint lessons should be condensed over time

Do not add a new five-page essay for every checkpoint.

As patterns emerge:

```text
checkpoint A lesson
checkpoint B related lesson
checkpoint C confirms rule
```

consolidate them into one durable principle.

For example, many individual parser bugs eventually support:

> Use small positive lexical grammars and test boundaries adversarially.

The handover should mature, not just grow forever.

---

# 67. Old operational checkpoint notes may become changelog/history

If a checkpoint report is useful historically but no longer needed in active handover docs:

```text
active handover
    → current principle

Git/changelog/archive
    → historical checkpoint detail
```

This prevents handover bloat.

---

# Part VI — Checkpoint Guidance for the `$[...]` Feature Specifically

# 68. Baseline checkpoint

Before changing `$[...]` parameter behavior, record:

```text
current Nift version
current branch/commit
local tests result
external contract result
website build result if cheap
```

This is the feature's baseline.

---

# 69. Parser-contract checkpoint

Once behavior is specified externally:

```text
new tests added
tests fail for expected missing feature
no implementation yet
```

This can be treated as an internal working checkpoint.

It proves the desired contract independently of implementation.

---

# 70. Implementation checkpoint

After interpolation works:

```text
focused parser/value tests green
basic contract tests green
```

but before deep dependency validation, call it working rather than validated.

---

# 71. Dependency/incremental checkpoint

After:

```text
A → B dynamic @input
stale A removed
B active
dynamic @dep
dynamic requirement
dynamic JSON if in scope
```

work correctly, the implementation reaches a much stronger checkpoint.

---

# 72. Full validation checkpoint

Only after:

```text
full local suite
external contract suite
sanitizers
performance check
website build
docs updated
handover updated
```

should it become the next validated Nift checkpoint.

---

# 73. Website changes for `$[...]`

I would update at least the relevant template-language documentation.

Potential examples:

```text
@input($[page.partial])

@input('partials/$[page.layout].html')
```

and possibly:

```text
@dep('data/$[dataset].json')
```

depending on exact implemented directive coverage.

---

# 74. Website explanation should emphasize value interpolation

Use language like:

> Directive string parameters can interpolate `$[...]` values.

Then explicitly say:

> Nift directives are not recursively evaluated inside parameters.

This protects the architecture in user-facing docs.

---

# 75. Example should demonstrate why it matters

A nice example is data-driven layout choice:

```text
page metadata / JSON
    ↓
layout = "feature"
    ↓
@input('partials/$[layout].html')
```

This demonstrates real usefulness without implying general scripting.

---

# 76. Update AI-context examples

If the website/repository contains AI context prompts/examples, add the new pattern once behavior is stable.

This should reduce agents inventing unsupported nested directive syntax.

---

# 77. Potential release note

If `$[...]` parameter interpolation is a user-facing new capability, it probably deserves a concise release/changelog entry.

Do not turn release notes into the comprehensive design history.

Something like:

```text
Directive string parameters now support `$[...]` interpolation, allowing
metadata and JSON values to select or compose input/dependency/path values.
```

Then link to docs.

---

# Part VII — Related Websites

# 78. Minify++ website checkpointing

The same broad process applies:

```text
Minify++ behavior changes
    ↓
tests
    ↓
claims/docs reconciled
    ↓
website rebuild
```

But don't update the Minify++ website for Nift-only integration changes unless public Minify++ claims are affected.

---

# 79. tscc website checkpointing

Compiler feature support is particularly vulnerable to stale claims.

When tscc gains:

```text
new syntax
runtime lowering
module semantics
```

the website should update only after the feature is validated by the relevant regression/runtime/differential tests.

Do not publish:

```text
supports X
```

merely because one parser fixture passes.

---

# 80. Website versions should not be synchronized artificially

Do not force:

```text
Nift v1.0.50
Nift website v1.0.50
Nift suite v1.0.50
```

unless there is a deliberate reason.

Each artifact may have its own checkpoint identity.

What matters is recording which ones correspond.

---

# Part VIII — Cross-Artifact Checkpoint Manifest

# 81. A lightweight manifest could eventually be useful

**Status: FUTURE POSSIBILITY**

Once the ecosystem is fully in Git, it might be useful to have a small checkpoint record like:

```text
Nift checkpoint X

nift:
    commit abc123

nift-regression-suite:
    commit def456

nift-website:
    commit ghi789

Minify++ embedded:
    standalone commit ...
```

This could live in release notes or checkpoint reports.

Do not build a complex manifest system now.

But the idea solves a problem our ZIP filenames previously solved manually:

> which exact artifacts belong together?

---

# 82. For public releases this becomes even more useful

A release record could identify:

```text
Nift source tag
contract-suite checkpoint
Minify++ version
website source commit
benchmark fixture
```

This makes historical claims reproducible.

Again, implement only when useful.

---

# Part IX — Checkpoint Anti-Patterns

# 83. “Tests passed once, call it done”

Reject this.

---

# 84. “Website looks right, no need to build from source”

Reject this.

---

# 85. “Source changed, update version everywhere”

Reject this.

---

# 86. “Every agent branch is a checkpoint”

Reject this.

---

# 87. “Every checkpoint is a release”

Reject this.

---

# 88. “Package filenames prove contents match”

Reject this.

Validate contents.

---

# 89. “Website claims can lag until later”

Be careful.

If the behavior is public/candidate-facing, reconcile docs before declaring a validated/public checkpoint.

---

# 90. “Keep adding checkpoint history to HANDOVER forever”

Reject this.

Preserve durable lessons; Git/changelog stores detailed chronology.

---

# 91. “Checkpoint means frozen forever”

Reject this.

A checkpoint is a baseline.

Future evidence can supersede it.

---

# Part X — The Historical Nift Development Arc as Checkpoints

The exact names/versions should be reconciled with Git, but conceptually our progression resembled:

```text
stripped Nift checkpoint
    ↓
early parser cleanup
    ↓
146-test checkpoint
    ↓
211-test hardening checkpoint
    ↓
245+ adversarial checkpoint
    ↓
watch/state fixes
    ↓
architecture rewrite
    ↓
ruthless merged v1.0.15-era checkpoint
    ↓
JSON/control-flow expansion
    ↓
Minify++ integration/hardening
    ↓
collision/scaling checkpoints
    ↓
memory-optimized v1.0.42 checkpoint
    ↓
canonical external contract-suite checkpoint
    ↓
Codex handover/current development
```

Each step had a reasonably clear:

```text
baseline
problem
evidence
change
validation
new baseline
```

That pattern is more important than exact historical numbering.

---

# 92. Some checkpoints existed because our confidence changed, not just code

This is subtle but important.

For example, after a ruthless audit:

```text
code may change only modestly
```

but:

```text
confidence changes substantially
```

because hundreds of previously untested assumptions are now exercised.

That is still a meaningful checkpoint.

---

# 93. Likewise, a benchmark checkpoint can change our understanding

The O(n²) validation discovery did not merely produce faster code.

It changed what we knew about project scaling.

Then the hash-set memory measurements changed the architecture again.

Checkpoint development should record **what was learned**, not only diff size.

---

# Part XI — How Codex Should Use Checkpoints

# 94. Start each substantial task by declaring the baseline

For example:

```text
Base:
Nift v1.0.42
branch X
commit Y

Baseline:
local tests ...
contract suite ...
```

This makes later reporting precise.

---

# 95. State the intended checkpoint goal

For `$[...]`:

```text
Goal:
validated parameter-interpolation checkpoint
without general nested Nift evaluation
```

This keeps scope clear.

---

# 96. Intermediate updates can identify checkpoint progression

For example:

```text
parser contract established
implementation working
incremental lifecycle still under test
```

This helps Nick understand whether the state is exploratory or ready for review.

---

# 97. Final report should declare whether a new baseline is warranted

Something like:

```text
Result:
Validated checkpoint candidate.

Not published/tagged.

Suitable to become next development baseline after review.
```

That is ideal.

---

# 98. Do not overwrite previous checkpoint evidence unnecessarily

Keep useful benchmark/test reports around long enough to compare.

Once superseded, Git can preserve them.

No need to keep dozens of redundant generated files permanently.

---

# Part XII — Website Maintenance as Part of Long-Term Handover

# 99. Website handover should include checkpoint coupling

Add a section like:

## Product checkpoint synchronization

When Nift behavior changes, review the website for affected:

```text
syntax documentation
command documentation
config documentation
examples
AI context
benchmark claims
testing claims
version text
downloads/templates
```

Only update affected surfaces.

Rebuild the website using the candidate Nift binary before treating the website update as validated.

---

# 100. Website handover should explain its own checkpoint identity

For example:

```text
Website checkpoint/version numbers describe website states and do not
automatically correspond to Nift executable releases.
```

This will prevent future agents from “fixing” perceived version mismatch.

---

# 101. All handover/checkpoint docs must be maintained for the project's lifetime

This reinforces the prior handover requirement.

The checkpoint workflow itself should evolve.

If Git-based Codex development eventually makes some of the historical practices unnecessary:

```text
update DEVELOPMENT.md
```

Do not preserve obsolete ZIP/checkpoint mechanics as though they remain mandatory.

The durable principle is:

> coherent, validated, evidence-backed baselines.

The implementation of that principle may change.

---

# Final checkpoint philosophy

The previous development workflow can be summarized as:

```text
Never let "the latest files" become synonymous with "the trusted baseline."
```

Instead:

```text
trusted baseline
    ↓
bounded development
    ↓
evidence
    ↓
reconciliation
    ↓
validated checkpoint
    ↓
new trusted baseline
```

And for Nift specifically, a meaningful checkpoint often spans more than the executable:

```text
Nift source
        +
implementation tests
        +
external contract suite
        +
documentation
        +
website
        +
relevant Minify++ sync
        +
benchmark/sanitizer evidence
```

depending on what changed.

That is how I approached the recent Nift development checkpoints.

The ZIP-heavy mechanics were an artifact of the previous ChatGPT workflow.

The **checkpoint philosophy itself is worth preserving** now that Codex can work directly with Git, worktrees, repositories, real websites, and the user's machine.

Use Git to make the process better.

Do not abandon the underlying discipline.

And yes, to answer your direct question: **Codex had some of the checkpoint idea, but not enough of the actual workflow**. I had told it that ZIPs were development checkpoints rather than formal releases, that website versions could differ from executable versions, that candidate packages should be validated, and that the site should be rebuilt with the candidate Nift binary. But I hadn't yet transferred the fuller pattern of:

```text
source
+ suite
+ site
+ evidence
→ coherent baseline
```

or how frequently we used website work as part of the checkpoint itself.

I think that's worth making explicit because Codex now has a much better environment than I did. It can use **Git commits/worktrees/branches as checkpoint provenance** instead of us constantly passing around ZIPs, while preserving the thing that made the old workflow effective: we always knew which state had actually been battle-tested before moving on.

There's also a subtle point I think Codex will benefit from: some of our most important checkpoints weren't “feature releases” at all. They were **knowledge checkpoints**. The code may have changed only modestly, but after another ruthless audit, scaling test, or memory pass, we knew materially more about what Nift could safely do. That kind of checkpoint is especially appropriate for the evidence-driven way Codex is now approaching Nift.
