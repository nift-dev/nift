# Handover maintenance

> This is the living information architecture for project handovers. Update it when document ownership or repository boundaries change.

This document defines the architecture for preserving handover knowledge without turning institutional memory into another sprawling documentation system.

The key distinction I would preserve is:

```text
HANDOVER.md
    = "How do I orient myself in this repository?"

docs/handover/*
    = "Why is this project like this, and how do I safely continue it?"
```

And yes: **the comprehensive Nift context should be canonical in the Nift repository, not copied into the suite, website, Minify++, or tscc.** Those projects should contain enough local context to stand on their own, then point outward where necessary.

Here is what I would send Codex.

---

# Response to Codex — Durable Project Handover Structure

I agree strongly with your proposed direction, including the root-level `HANDOVER.md`.

I would make one adjustment to the philosophy before discussing files:

> These documents should preserve **reasoning and operational knowledge that cannot be cheaply reconstructed from the repository**, rather than attempting to narrate everything we know about the project.

That distinction is what will keep them useful.

Source tells you what a function does.

Tests tell you what behavior is required.

Git tells you when something changed.

The handover should primarily tell you:

```text
why this boundary exists
why this strange-looking code must not be casually simplified
which project owns a cross-project responsibility
which historical direction was deliberately abandoned
what validation standard is expected
what remains unresolved
how releases/publication actually work
```

If we follow that rule, I think these files can remain durable rather than becoming another documentation burden.

## 1. Yes: use `HANDOVER.md` consistently

**Recommendation: YES.**

I like having:

```text
/HANDOVER.md
```

at the root of every related repository.

It gives humans and coding agents one predictable entry point.

A newly arrived agent should be able to do:

```text
README.md
HANDOVER.md
```

and understand the distinction immediately:

```text
README.md
    → how to use the project

HANDOVER.md
    → how to work on the project
```

That is a very clean boundary.

`HANDOVER.md` should remain reasonably short. I would aim for something that can be read in several minutes rather than the comprehensive Nift history.

Its job is orientation, not archival completeness.

A useful standard opening could even be shared conceptually across repositories:

```markdown
# Development Handover

This document is an orientation guide for developers and coding
agents working on this repository.

The repository is authoritative for current implementation behavior.
This handover preserves development workflow, architectural rationale,
historical hazards, and cross-project relationships that may not be
obvious from the source alone.
```

Then immediately identify authority:

```text
Current implementation
    → source

Required behavior
    → tests / contract suite

User-facing behavior
    → current documentation

Historical rationale
    → handover documents + Git history
```

That prevents the handover from accidentally becoming a shadow specification.

---

# 2. I would not create every deeper document immediately

This is where I would be slightly more conservative.

For **Nift**, yes, I think the context is already large enough to justify several deeper documents.

For the other repositories, create only what their existing institutional knowledge warrants.

I would start approximately like this:

```text
nift/
├── HANDOVER.md
└── docs/
    └── handover/
        ├── PROJECT-CONTEXT.md
        ├── DEVELOPMENT.md
        ├── TESTING.md
        ├── RELEASES.md
        └── DECISIONS.md
```

But something like Minify++ might initially need only:

```text
minifypp/
└── HANDOVER.md
```

or perhaps:

```text
minifypp/
├── HANDOVER.md
└── docs/
    └── handover/
        └── TESTING.md
```

if its semantic-hardening history is substantial enough to justify it.

Likewise:

```text
tscc/
├── HANDOVER.md
└── docs/
    └── handover/
        └── TESTING.md
```

might be appropriate because compiler validation deserves specialized explanation.

Do not create:

```text
PROJECT-CONTEXT.md
DEVELOPMENT.md
TESTING.md
RELEASES.md
DECISIONS.md
```

for every project merely because Nift has them.

Empty structure is not organization.

It is ceremony.

---

# 3. Proposed Nift document division

I would divide the comprehensive handover roughly as follows.

## `HANDOVER.md`

This should contain the operational map.

Approximately:

```text
Nift identity
current maturity
repository authority
quick architecture
build commands
test commands
sanitizer commands
important sibling repositories
embedded Minify++ relationship
website relationship
regression-suite relationship
development expectations
public-action approval boundary
links to deeper handover docs
```

It should also contain perhaps the **five most dangerous things to misunderstand**.

For example:

```text
1. Nift is a website generator, not primarily an SSG.

2. The external regression suite is an implementation-independent
   behavioral contract.

3. Standalone Minify++ is canonical; Nift's embedded copy follows it.

4. Do not restore general scripting/orchestration merely because
   historical Nift contained it.

5. Green tests are the beginning of validation, not necessarily the end:
   implementation-guided adversarial testing is expected.
```

That gives a new agent the project shape immediately.

---

# 4. `PROJECT-CONTEXT.md`

This is where most of the comprehensive handover belongs.

I would preserve substantial detail here.

Sections should include:

```text
What Nift is
Terminology
Older Nift
The stripping/simplification phase
Why minimalism proved powerful
Modern language evolution
JSON/Schema/loops/conditions
Composition philosophy
External-tool philosophy
Performance evolution
AI-DX
Human DX
Tool-choice boundaries
Evidence versus hypotheses
Future possibilities
Chronology
```

I would preserve the **chronology nearly intact**, as you suggested.

It has unusual value because source and Git history can tell you *what happened*, but the chronology explains how one finding changed our interpretation of the next.

For example:

```text
simplification
    ↓
unexpected capability retained
    ↓
serious regression testing
    ↓
architecture survives hostile testing
    ↓
structured rendering added cautiously
    ↓
scaling problem found
    ↓
CPU solution creates memory problem
    ↓
better staged solution
    ↓
Codex independently uses Nift
    ↓
React composition works
    ↓
tool-choice claims become more evidence-driven
```

That intellectual progression is worth preserving.

---

# 5. `DEVELOPMENT.md`

This should be much less historical.

It should answer:

> What standard of work is expected when modifying Nift?

Include things such as:

```text
inspect before modifying
establish baseline
write/reproduce failure where applicable
make smallest justified fix
run implementation-local tests
run external regression suite
run sanitizers where relevant
benchmark when touching hot/scaling paths
check memory when changing data structures/lifetimes
rebuild website where appropriate
inspect Git diff
update affected docs/handover
```

And preserve the development rule from the comprehensive handover:

```text
Get green.

Read the implementation anyway.

Find an assumption nobody has tested.

Construct the smallest hostile case.

Prove whether it fails.

Preserve the reproducer.

Fix the family rather than the symptom.

Run everything again.

Measure performance/memory where relevant.
```

This belongs in `DEVELOPMENT.md` because it describes the **engineering culture**, not merely history.

---

# 6. `TESTING.md`

This should explain the test architecture and philosophy.

I would include:

```text
implementation-local C++ tests
        versus
external executable contract suite
```

with a very explicit ownership boundary.

Something like:

```text
Nift-local tests may know implementation details.

The external Nift regression suite must not.

A hypothetical independent implementation should be able to pass
the external suite without sharing Nift's C++ architecture.
```

Then cover:

```text
bug-family testing
implementation-guided adversarial review
deterministic fixtures
timing/mtime control
failure semantics
state corruption
filesystem containment
incremental invalidation
interaction tests
performance/scaling guards
sanitizers
future fuzzing
```

I would also preserve representative historical bugs here, but **not the entire history**.

For example:

```text
same-second mtime fixture
hash collision reproducer
malformed state crash
empty tracked state
deleted generated output
multi-item mutation
path traversal
```

Each is valuable because it illustrates a testing principle.

---

# 7. `RELEASES.md`

This should be aggressively operational.

Do not fill uncertainty with invented procedure.

Start by reconciling actual Git history and repository structure.

Where policy is not established, say:

```text
UNRESOLVED
```

rather than manufacturing one.

Include:

```text
authoritative version source
when version bumps are expected
build/package commands
clean-package validation
website self-build check
regression suite
sanitizer expectations
artifact contents
tag conventions if established
release branches if established
website publication relationship
public actions requiring approval
```

The packaging principle should be explicit:

```text
repository passes
        ≠
release archive passes
```

A release candidate should be tested from the actual packaged artifact where practical.

---

# 8. `DECISIONS.md`

I agree particularly strongly with preserving the ledger.

This could become one of the most valuable files in the repository.

I would make it structured enough to scan but not bureaucratic.

For example:

```markdown
## Nift terminology

**Status:** SETTLED

Nift is described as a website generator rather than primarily as a
static site generator.

### Rationale

Nift generates website artifacts but does not constrain the resulting
website to static behavior. Nift-generated sites may contain client-side
applications, consume APIs, or form frontends for dynamic backends.

### Revisit if

Only if the project's product identity changes substantially.
```

Another:

```markdown
## Arbitrary Nift evaluation inside directive parameters

**Status:** REJECTED

Directive parameters should not be evaluated as arbitrary nested Nift
templates.

### Rationale

Nift operations may emit output, load files, record dependencies, create
bindings, or modify parser state. Treating them as value-returning
expressions would require a much broader expression/composition model.

### Current direction

Parameters may support constrained value interpolation such as literal
text plus `$[...]` resolution.
```

And:

```markdown
## Rust implementation

**Status:** FUTURE POSSIBILITY

An independent Rust implementation of the Nift behavioral contract has
been discussed as an architectural experiment.

It is not current development work.
```

That structure makes status difficult to misinterpret.

---

# 9. I would add one more concept: “revisit conditions”

This could make `DECISIONS.md` much more useful.

A rejected idea should not necessarily mean:

> never discuss this again.

Instead:

```text
Decision
Status
Rationale
Evidence
Revisit if
```

For example:

```text
General scripting

Status:
REJECTED

Rationale:
Modern Nift deliberately moved away from owning a general scripting
runtime.

Revisit if:
A substantial class of real Nift projects repeatedly cannot express
necessary build-time rendering without awkward external machinery,
and a constrained alternative cannot solve the problem.
```

That prevents architectural decisions from becoming dogma.

It also tells future Codex instances **what kind of new evidence would actually justify reopening them**.

I like that very much.

---

# 10. What should be canonical in Nift itself

Nift should own canonical context for:

```text
Nift identity
Nift philosophy
Nift history
Nift language boundaries
Nift parser philosophy
Nift dependency model
Nift state/filesystem principles
Nift performance history
Nift implementation-development methodology
Nift relationship to external tools
Nift relationship to Minify++
Nift tool-choice thinking
Nift AI-DX observations
Nift decision ledger
Nift chronology
Nift release process
```

It should **summarize**, not duplicate, sibling-project details.

For example:

```text
Minify++ is the canonical standalone minifier implementation.
Nift embeds a synchronized copy for optional final-output minification.
See <Minify++ handover location> for Minify++ architecture and testing.
```

Nift does not need ten pages describing JavaScript minification semantics.

---

# 11. What should be canonical in the Nift regression suite

The regression suite should own:

```text
its purpose as executable behavioral contract
how to run against arbitrary Nift executable
fixture structure
test numbering/output conventions
determinism requirements
how to add a regression
contract versus implementation-test boundary
historical contract hazards where useful
expected portability
```

It should **not** own:

```text
Nift product philosophy
full Nift history
Minify++ architecture
Nift website deployment
C++ internal design
```

It can link back to Nift's project context.

The key sentence should probably be something like:

> This repository specifies externally observable Nift behavior. It must not depend on the internal architecture of the current C++ implementation.

That is worth making extremely prominent.

---

# 12. What should be canonical in the Nift website repository

The website should own:

```text
source branch
generated/deployment branch
build command
candidate Nift binary expectations
publication workflow
deployment target
generated-file boundaries
design conventions
documentation/content conventions
version-display conventions
asset conventions
public approval boundary
```

It should not own the entire Nift architectural history.

It can say:

```text
For Nift development philosophy and historical architectural decisions,
see the Nift repository's handover documentation.
```

The website handover should be especially clear about:

```text
EDIT HERE
        versus
GENERATED — DO NOT EDIT
```

because that is the sort of mistake both humans and agents can make easily.

---

# 13. Generated deployment branches should not automatically receive handovers

I agree with your caution.

The canonical handover should live on the **authoritative source branch**.

If the deployment branch is generated website output, I would generally **not** intentionally copy development handover files into it unless there is a concrete operational reason.

Otherwise we risk:

```text
source/HANDOVER.md
generated/HANDOVER.md
```

both appearing authoritative and eventually diverging.

Worse, a Markdown file could accidentally become part of the deployed artifact.

So the rule should be:

> Development handover lives with authoritative source, not generated deployment output, unless explicitly required.

---

# 14. Development history and present operational instructions should absolutely be separated

This is one of the strongest recommendations I would make.

Mixing:

```text
"In 2026 we discovered..."
```

with:

```text
"Run this command before every release..."
```

creates a maintenance problem.

Historical reasoning ages slowly.

Operational commands age quickly.

Therefore:

```text
PROJECT-CONTEXT.md
DECISIONS.md
    → slow-moving institutional memory

HANDOVER.md
DEVELOPMENT.md
TESTING.md
RELEASES.md
    → current operational knowledge
```

This gives us different update expectations.

A command change does not require rewriting Nift's history.

A new historical insight does not require touching release instructions.

---

# 15. I would explicitly mark commands as repository-verified

For operational documents, it may be useful to use wording such as:

```text
Verified against repository: <date/version/commit>
```

I would **not** require this on every paragraph.

But something like the top of `RELEASES.md` could say:

```text
Last reconciled with repository state: YYYY-MM-DD
```

and perhaps:

```text
Authoritative command definitions:
- Makefile
- package.json
- CI workflow
```

That makes stale operational documentation easier to recognize.

Do not turn this into a manual versioning bureaucracy, though.

---

# 16. Update discipline

I would use a simple rule:

> A change is not complete until the developer has asked whether it invalidates a handover statement.

Not:

> Every change must modify handover files.

Most changes will not.

The checklist can simply include:

```text
[ ] Does this change alter user-visible behavior?
[ ] Does it alter the behavioral contract?
[ ] Does it alter development/test commands?
[ ] Does it alter release/publication procedure?
[ ] Does it alter a settled/rejected architectural decision?
[ ] Does it invalidate historical rationale attached to current code?
[ ] Does it change a cross-project ownership/synchronization boundary?
```

If all are no, no handover update is necessary.

That keeps maintenance proportional.

---

# 17. Tests should guard machine-checkable invariants instead of prose

This is important for preventing drift.

If a handover says:

```text
Nift's embedded Minify++ must match standalone Minify++
```

and that can be checked mechanically, prefer an actual synchronization check/test.

If a handover says:

```text
the external contract suite must accept an arbitrary Nift executable
```

its runner should actually take an executable path rather than merely documenting that intention.

Use documentation for reasoning.

Use automation for invariants.

That is the strongest anti-drift mechanism available.

---

# 18. Comments near strange code still matter

The handover should not become the only explanation for surprising implementation details.

If a particular function contains an apparently unnecessary operation because removing it recreates a subtle correctness bug, a concise nearby comment may still be appropriate.

Think of the layers as:

```text
code comment
    → why this local implementation detail exists

test
    → proves the behavior must remain

DECISIONS / TESTING
    → broader architectural/history explanation
```

Each serves a different audience.

---

# 19. What should intentionally stay out of public repositories

Yes, some material should remain outside.

I would exclude or heavily rewrite:

### Casual conversational material

Things like:

```text
"Hahaha, Codex actually did it 😂"
```

are useful conversational history but not durable project documentation.

The underlying observation belongs:

> Codex independently produced a working Nift + React/Vite islands architecture.

The conversation itself does not.

### Unsupported speculation

If we said:

> “I bet Nift would destroy tool X at Y”

without testing it, that does not belong as project fact.

At most it belongs as:

```text
HYPOTHESIS
```

if the question is strategically useful.

### Ephemeral ratings

Things like:

```text
Nift 10/10
Astro 8/10
```

should not become architectural documentation unless they are part of a specifically preserved historical comparison.

### Private operational information

Obviously exclude:

```text
credentials
tokens
private URLs
machine-specific secrets
personal identifiers
```

### Unresolved conversational brainstorming with no lasting value

Not every idea deserves institutional memory.

If we once casually considered twelve names for Minify++, only the fact that Sift was the historical name and Minify++ became the chosen identity matters.

### Statements we no longer believe

These can be preserved **only where the change of belief teaches something**.

For example:

```text
We initially considered React-first build ordering integration friction.
Further investigation showed it represented correct requirement semantics.
```

is useful.

An obsolete random opinion is not.

---

# 20. Public repositories should preserve uncertainty where uncertainty matters

I would not sanitize the handover into false certainty.

It is perfectly professional for `DECISIONS.md` to contain:

```text
## Richer expression facilities

Status: UNRESOLVED
```

or:

```text
## Rust implementation

Status: FUTURE POSSIBILITY
```

That is much better than either pretending the decision was made or deleting the history entirely.

The status vocabulary makes this safe.

---

# 21. Ecosystem-level index

I like this idea, but only if there is a natural canonical home.

It should be tiny.

Something like:

```text
Nift ecosystem/project map

Nift
    website generator
    canonical Nift context

nift-regression-suite
    external behavioral contract

nift-website
    public documentation/site

Minify++
    standalone minifier/library
        ↓
    embedded into Nift

minifypp-website
    public Minify++ site

tscc
    TypeScript/JavaScript compiler

tscc-regression-suite
    external tscc contract/regression suite

tscc-website
    public tscc site
```

Then cross-project invariants:

```text
Standalone Minify++ owns canonical minifier implementation.

Nift's embedded Minify++ follows standalone Minify++.

External regression suites remain implementation-independent.

Website source repositories/branches own website content;
generated deployment state does not.

Public release/deployment actions require approval.
```

That's enough.

Do not create an “ecosystem architecture manual.”

---

# 22. Where should that ecosystem index live?

I would not invent a ninth repository solely for it.

If there is already an organization-level `.github` repository or another natural shared repository, that may be appropriate.

Otherwise Nift is probably the least-bad canonical home for now because it is the most mature project and the historical origin of the related tooling.

But even there, label it explicitly as:

```text
Related project map
```

rather than implying Nift owns Minify++ and tscc architecturally.

They are independent projects with related engineering history.

---

# 23. Minify++ handover

I would make its root `HANDOVER.md` explain:

```text
identity
historical Sift name
standalone canonical ownership
public library/API
CLI
semantic-preservation-first philosophy
HTML/CSS/JS scope
what transformations are intentionally conservative
test strategy
differential/reference testing if present
sync relationship with Nift
version/release procedure
website relationship
```

Most importantly:

> Do not optimize compression ratio at the expense of semantic confidence.

That is probably its equivalent of Nift's “glue, not universe” principle.

---

# 24. tscc handover

tscc's handover should have a different center of gravity.

I would emphasize:

```text
compiler/transpiler identity
language/feature scope
semantic correctness
diagnostics
parser/compiler pipeline
benchmark methodology
differential testing
regression ownership
tracked benchmark evidence
temporary/probe artifacts
.shadow-debug behavior if still relevant
release maturity
relationship to Nift: independent
```

Do not make tscc read like “another Nift tool.”

Its engineering problem is different.

---

# 25. Regression-suite handovers

For both Nift and tscc suites, I would use nearly the same high-level doctrine:

```text
This suite tests externally observable behavior.

It must not depend on implementation internals.

A different implementation of the same contract should be testable.

Every discovered behavioral regression should ideally leave a minimal,
deterministic reproducer.

Fixtures should control timing/environment where practical.

Failure output should identify the smallest useful failing case.
```

Then specialize by project.

---

# 26. Website handovers

All three websites should probably have a similar *shape* even if their actual branch models differ:

```text
Purpose
Source authority
Branch structure
Build tool/version
Build command
Generated output
Local preview
Deployment
Version text
Asset conventions
Design/content conventions
Do-not-edit boundaries
Publication approval
Relationship to product repository
```

This consistency will be useful to Codex.

But do **not** force the same branch architecture across them.

Document reality.

---

# 27. One particularly useful convention: “Authority”

Every `HANDOVER.md` should contain a short section near the top:

```markdown
## Authority

For current implementation behavior, source and tests are authoritative.

For public usage, current user documentation is authoritative unless it
conflicts with tested behavior, in which case investigate the discrepancy.

This handover is authoritative for development workflow and documented
project decisions only where explicitly marked as such.
```

Adapt as appropriate per repository.

This directly addresses the biggest danger with handover documents.

---

# 28. Another useful convention: “Do not infer”

A small section could save future agents from repeating mistakes:

```markdown
## Do not infer

- Do not infer current behavior from historical discussion.
- Do not infer release policy where Git history does not establish one.
- Do not infer sibling-project ownership from copied source.
- Do not infer that an undocumented compatibility feature is unsupported.
- Do not infer that a discussed future idea is planned work.
```

For Nift specifically I would add:

```text
Do not infer that a missing framework-specific integration is a missing
feature; first test whether ordinary composition already solves it.
```

That is very aligned with the project's history.

---

# 29. Preserve the decision ledger and chronology almost intact

I agree with you here.

Of everything in the comprehensive handover, these are probably the two pieces I would be **least aggressive about shortening**.

The chronology answers:

> How did we arrive here?

The ledger answers:

> Which conclusions from that journey are actually binding?

Together they prevent a huge class of future misunderstandings.

The topical prose can be compressed and reorganized around them.

---

# 30. I would add a third durable artifact: historical bug lessons

Not necessarily immediately as another file.

It could initially be a section in `TESTING.md`.

Something like:

```text
Historical bug family
→ lesson
→ current regression location
```

For example:

| Historical issue              | Lesson                                                     |
| ----------------------------- | ---------------------------------------------------------- |
| `@content<` parsing           | Define valid token characters positively                   |
| malformed watch/tracking JSON | Persisted state is untrusted input                         |
| empty `tracked.json`          | Test zero-item serialization explicitly                    |
| same-second edit fixture      | Control timestamps deterministically                       |
| 32-bit hash collision         | Security/correctness assumptions need reproducers          |
| O(n²) collision validation    | Correctness structures need scaling tests                  |
| hash-set memory spike         | Measure memory as well as runtime                          |
| traversal through mutations   | Path containment must be semantic                          |
| React bundle ordering         | Investigate apparent friction before changing architecture |

This would be enormously useful.

If possible, link each lesson to the actual regression/test that protects it.

Then the handover becomes navigable institutional memory rather than folklore.

---

# 31. Eventually, use Git history as part of the documentation

Where a particularly important architectural change has a clean commit, the decision document can mention it.

Not every decision needs a commit archaeology exercise.

But something like:

```text
See commit <hash> for the validation architecture change.
```

can be valuable when a future maintainer needs to understand the actual diff.

Again:

```text
handover = why
Git = what changed
tests = what must remain true
source = how it works now
```

That combination is powerful.

---

# 32. The handovers should optimize for future agents with no conversational memory

This is probably the ultimate test.

Imagine a new Codex instance receives only:

```text
repository checkout
```

and nothing from this conversation.

Can it discover:

```text
what the project is
how to build it
how to test it
what not to casually change
why important boundaries exist
where sibling projects fit
what is unresolved
how to prepare a release
which actions need approval
```

without needing Nick to retell several years of project history?

If yes, the handover is working.

---

# 33. But don't turn the repository into a museum

There is an opposite failure mode.

We do **not** want:

```text
docs/handover/
    47 historical essays
    19 abandoned-roadmap documents
    every ChatGPT conversation
    every benchmark ever run
    every discarded design sketch
```

Institutional memory should help future development.

If a historical detail no longer informs:

```text
architecture
correctness
testing
workflow
release
product identity
```

it probably does not belong.

---

# 34. Recommended initial structure

Subject to repository inspection, my starting proposal would be:

```text
Nift/
├── HANDOVER.md
└── docs/handover/
    ├── PROJECT-CONTEXT.md
    ├── DEVELOPMENT.md
    ├── TESTING.md
    ├── RELEASES.md
    └── DECISIONS.md
```

```text
nift-regression-suite/
└── HANDOVER.md
```

possibly later:

```text
docs/
└── CONTRACT.md
```

if the behavioral-contract concept deserves a dedicated document.

For Minify++:

```text
minifypp/
├── HANDOVER.md
└── docs/handover/
    └── TESTING.md       # only if current context warrants it
```

For its website:

```text
minifypp-website/
└── HANDOVER.md
```

For tscc:

```text
tscc/
├── HANDOVER.md
└── docs/handover/
    └── TESTING.md       # likely valuable for compiler validation
```

For its external suite:

```text
tscc-regression-suite/
└── HANDOVER.md
```

For the websites:

```text
nift-website/
└── HANDOVER.md

minifypp-website/
└── HANDOVER.md

tscc-website/
└── HANDOVER.md
```

But **do not implement this exact tree yet**.

First inspect each repository's current documentation layout and propose the least disruptive equivalent.

---

# 35. Recommended maintenance rule

I would put this sentence in every root handover:

> **When changing behavior, architecture, build/test workflow, release procedure, cross-project ownership, or a documented decision, check whether this handover or a linked context document must be updated.**

That's enough.

No elaborate handover-versioning system is necessary.

Git already versions it.

---

# 36. One more recommendation: handover changes should be reviewed like code

Because a wrong handover can mislead every future coding agent.

When updating one:

```text
compare claim with source
compare behavior with tests
compare command with actual build configuration
compare release statement with Git history
preserve status vocabulary
avoid turning opinion into requirement
```

If uncertain, write:

```text
UNRESOLVED
```

or:

```text
VERIFY AGAINST CURRENT REPOSITORY
```

rather than manufacturing certainty.

---

# 37. Final recommendation

So my preferred architecture is:

```text
                    ecosystem map
                         │
          ┌──────────────┼──────────────┐
          ▼              ▼              ▼
        Nift         Minify++          tscc
          │              │              │
     HANDOVER.md     HANDOVER.md     HANDOVER.md
          │              │              │
          ▼              ▼              ▼
   deeper context     only where      only where
   because Nift       warranted       warranted
   needs it
          │
      ┌───┴────────┐
      ▼            ▼
 regression      website
   suite
      │            │
 HANDOVER.md   HANDOVER.md
      │            │
 contract       publication
 ownership       workflow
```

The principles are:

```text
one canonical owner for each fact
minimal duplication
history separate from operations
status labels preserved
source/tests remain implementation authority
automation enforces machine-checkable invariants
handover explains reasoning
generated branches do not become competing authorities
uncertainty remains explicit
public actions require approval
```

And yes: **preserve the Nift chronology and decision ledger nearly intact.**

Those two pieces are probably the closest thing we currently have to Nift's institutional memory.

---

## Before writing any of these files

Your proposed sequence is exactly right:

```text
1. receive comprehensive Nift context       ✓
2. receive this handover-structure response ✓
3. receive targeted $[...] task handover
4. inspect all current repositories
5. reconcile historical claims against reality
6. propose exact file map
7. only then write/commit handover files
8. establish clean development baseline
9. implement the $[...] change
```

Do not spend time creating a documentation architecture based solely on our memory before inspecting the repositories.

The repository layouts should influence the final structure.

---

I think Codex's instinct here is excellent. In particular, **splitting historical reasoning from operational instructions solves the biggest problem I had with simply committing my giant handover verbatim**.

The giant handover is valuable *right now* because we're transferring my mental state to Codex. But five years from now, nobody should have to read 10,000 words before learning how to run Nift's tests. 😄

The durable version should instead let someone go:

```text
HANDOVER.md
    ↓
"Oh, I understand the project."

DEVELOPMENT.md
    ↓
"I know how to work safely."

TESTING.md
    ↓
"I understand what green actually means here."

DECISIONS.md
    ↓
"Ah, that's why they deliberately didn't do that."

PROJECT-CONTEXT.md
    ↓
"Now I understand how all of this came to be."
```

And the **“revisit if”** idea is one addition I'd strongly recommend. It prevents our current architectural preferences from becoming religious doctrine. “Rejected” should mean *we had good reasons*, not *future evidence is forbidden*.

The next thing I should give you/Codex is now the **specific `$[...]` parameter-resolution development handover**. That can be much more exacting and implementation-oriented, because the broad architectural context is finally transferred.
