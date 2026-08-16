# Ecosystem history and institutional context

> Nift canonically owns this living cross-project history to avoid duplicating it into every sibling repository. Correct or supersede stale claims while retaining rationale and chronology that cannot be recovered from source alone.

Below is the detailed institutional context for the seven surrounding projects.

I am deliberately going substantially deeper than the previous overview. Some repetition across sections is intentional because I want each project handover to remain useful if Codex later reads it independently.

The most important epistemic rule throughout is:

> **Repository reality is authoritative for current implementation. These handovers are authoritative primarily for historical context, rationale, development philosophy, prior evidence, user preferences, and why particular decisions were made.**

Where I know something historically but cannot guarantee it still describes the live repository, I mark it accordingly.

---

# HANDOVER PACKAGE

## Nift ecosystem institutional-memory transfer

Projects covered:

```text
1. Nift website
2. Nift regression suite
3. Minify++
4. Minify++ website
5. tscc
6. tscc regression suite
7. tscc website
```

Nift core itself is deliberately not repeated in full here because Codex already received:

```text
comprehensive Nift project context
development methodology
checkpoint methodology
website/checkpoint methodology
$[...] parameter-resolution behavioral handover
$[...] implementation-oriented handover
```

This package should be reconciled with those documents.

---

# GLOBAL INSTRUCTIONS TO CODEX

Before the individual projects, there are several principles I want preserved across the entire ecosystem.

## 1. Do not mistake handover material for current source truth

Use this hierarchy:

```text
CURRENT IMPLEMENTATION BEHAVIOR
    current source
    current tests
    current repository state

CURRENT PUBLIC CONTRACT
    current tests
    current documentation
    explicit settled decisions

HISTORICAL RATIONALE
    handovers
    decision records
    Git history
    prior development evidence

EXACT HISTORICAL CHANGE
    Git history
```

If this handover says:

```text
X behaves like Y
```

but current tests/source clearly implement Z, investigate rather than immediately "fixing" Z back to Y.

The handover may be stale.

The repository may contain an accidental regression.

Or the behavior may have deliberately changed.

Determine which.

---

# 2. Preserve status vocabulary

When turning this knowledge into durable documentation, distinguish:

```text
SETTLED
CURRENT
STRONG PREFERENCE
HISTORICAL
HYPOTHESIS
UNRESOLVED
REJECTED
FUTURE POSSIBILITY
VERIFY FROM REPOSITORY
```

This was requested specifically because our conversations often explored ideas before deciding against them.

Do not turn exploratory discussion into requirements.

---

# 3. Handover documents are living project infrastructure

This deserves explicit language in every repository's root `HANDOVER.md`.

Something like:

> This handover is a living development document. It should be reviewed whenever architecture, workflows, tests, release procedures, repository relationships, or durable project decisions change. Update, correct, reorganize, split, consolidate, or remove material as appropriate. Do not preserve stale guidance merely because it was historically true.

And:

> A substantial development checkpoint should include a handover-impact review alongside tests, documentation, website, benchmark, and release-impact reviews where applicable.

The documents should improve over time.

They should **not** simply grow forever.

---

# 4. Do not create documentation bureaucracy

The conceptual categories in this handover do **not** imply that every repository requires:

```text
HANDOVER.md
PROJECT-CONTEXT.md
DEVELOPMENT.md
TESTING.md
DECISIONS.md
RELEASES.md
ARCHITECTURE.md
```

immediately.

Use:

```text
one concise root entry point
+
only the deeper documents justified by actual complexity
```

A four-page `HANDOVER.md` is better than six nearly empty files.

---

# 5. Checkpoint methodology applies everywhere

The shared development loop is:

```text
trusted baseline
      ↓
bounded objective
      ↓
investigation
      ↓
contract/reproducer
      ↓
implementation
      ↓
focused validation
      ↓
interaction/adversarial validation
      ↓
full validation
      ↓
docs/site/handover reconciliation
      ↓
validated checkpoint
```

The newest state is not automatically the trusted state.

---

# 6. Public actions remain deliberate

Unless Nick explicitly authorizes otherwise, implementation work does not imply authorization to:

```text
push
tag
publish release
publish website
rewrite public branch
upload release artifacts
perform irreversible public actions
```

Likewise, do not assume a checkpoint requires a Git commit unless requested.

---

# 7. Preserve unknown working state

Before modifying any repository:

```bash
git status
```

and inspect the relevant branches/worktrees.

Do not delete mysterious files because they look temporary.

This is particularly important for tscc because Codex has already encountered benchmark/debug/probe artifacts whose intended permanence was unclear.

---

# 8. Canonical ownership matters

The ecosystem should not evolve into several subtly divergent copies of the same implementation or knowledge.

Where source is embedded or tests are mirrored, explicitly establish:

```text
canonical owner
synchronization direction
allowed differences
verification mechanism
```

and document it.

---

# PART I — NIFT WEBSITE

# A. Everything / Project Context Handover

## 1. What the Nift website actually is

The Nift website should not be thought of as merely:

```text
marketing site
```

It has historically served at least six roles:

```text
official product website
official documentation
real-world Nift project
integration test
example of what Nift can build
AI onboarding surface
```

It has also carried:

```text
templates
downloadable examples
benchmark material
testing/battle-testing information
migration guidance
workflow guidance
hosting/deployment guidance
```

That means website correctness matters to Nift development itself.

A candidate Nift version successfully building the actual website is meaningful integration evidence.

---

# 2. Historical reason for the current website

The modern website was produced during a major Nift simplification.

Older Nift had accumulated capabilities such as:

```text
LuaJIT
ExprTk
system execution
pre/post build hooks
broader scripting machinery
```

The modern direction deliberately removed much of that.

The website therefore needed to stop selling Nift as a large programmable ecosystem and explain the much smaller core.

The emerging story became:

```text
Nift does the website-generation/build layer extremely well.
It interoperates with everything else rather than replacing everything else.
```

That philosophical transition is fundamental to understanding the current site.

---

# 3. Do not call Nift merely a static site generator

This became an important recurring correction.

### STRONG CURRENT PRODUCT PREFERENCE

Use:

> website generator

as the general category.

Avoid reducing Nift to:

> static site generator

unless discussing the static-generation use case specifically.

The distinction matters because Nift can own the document/build layer for:

```text
ordinary websites
documentation
blogs
SPAs
dashboards
full-stack frontends
React islands
Vue/Svelte islands
vanilla JS applications
API-consuming frontends
```

Nift executing at build time does not mean the resulting website has no runtime application behavior.

---

# 4. CloudFort Dash as conceptual evidence

A recurring real-world example was CloudFort Dash.

The important lesson was not its exact source architecture.

It was that a substantial application frontend could use Nift while the actual application complexity remained in:

```text
HTML
CSS
JavaScript
backend APIs
```

rather than requiring Nift to become an application framework.

That became part of the argument for Nift's minimalism.

---

# 5. React-islands experiment

Later, Codex independently built a Nift website with React islands.

The architectural split it chose was particularly instructive:

```text
Nift owns:
    document
    navigation
    SEO/content
    product narrative
    documentation
    asset paths

React owns:
    genuinely stateful interactive surfaces
```

Codex specifically created examples such as:

```text
service-health simulator
interactive alert-policy builder
```

rather than using React decoratively.

That independently reinforced the architectural model.

---

# 6. Requirement versus dependency insight from React integration

The React-islands experiment also clarified an important Nift behavior.

Given:

```html
<script type="module" src="@pathto('public/assets/app.js')"></script>
```

the React/Vite bundle must exist before the first Nift build because Nift refuses to bless a nonexistent concrete asset.

But once it exists:

```text
React bundle contents change
    ↓
HTML URL unchanged
    ↓
Nift pages need not rebuild
```

whereas:

```text
bundle disappears
    ↓
reference is no longer valid
    ↓
Nift has reason to detect/revalidate
```

Codex correctly recognized this as a **requirement-versus-content-dependency distinction**, not integration friction.

That is valuable conceptual material for docs/examples.

---

# 7. Core website messaging

Several pieces of copy captured the project's direction well.

Examples included:

> Keep your HTML. Keep your tools. Stop repeating yourself.

and:

> Nift provides the glue without trying to become the universe.

Another useful conceptual formulation was:

> We don't care how you build your website. Here's a really fast templating/build layer. Carry on.

These should not necessarily be fossilized forever, but they accurately describe the current philosophy.

---

# 8. Why Nift's simplicity is hard to communicate

One of the most important product-marketing insights we reached was that Nift has an unusual sales problem.

Many developer tools demonstrate power through:

```text
more features
more directives
more integrations
more framework concepts
more abstractions
```

Nift often demonstrates power through the absence of those things.

Historically, even substantial projects often required little more than:

```text
@content
@input(...)
@pathto(...)
$[...]
```

with newer capabilities now including:

```text
@json
loops/control flow
```

The website needs to communicate:

> "That's it" can be a feature rather than evidence of weakness.

The surrounding technologies retain their native form.

---

# 9. Important newer language state

Older website handovers predated some current work.

### CURRENT PER USER / VERIFY EXACT SYNTAX

Nift now has:

```text
@json
loops
```

in addition to the older stripped template core.

Do not reconstruct the website from an old assumption that Nift has only four template operations.

At the same time, do not swing back toward presenting Nift as a full scripting language.

The newer features were deliberately constrained.

---

# 10. Values versus operations

A major language-design idea should be reflected carefully in documentation.

Conceptually:

```text
operations:
    @input(...)
    @dep(...)
    @json(...)
    @pathto(...)

values:
    $[...]
```

This distinction is why the upcoming parameter-interpolation work favors:

```text
@input('partials/$[page.layout].html')
```

rather than arbitrary nested Nift execution:

```text
@input(@someOperation(...))
```

The website's template-language documentation should preserve this clean conceptual model if the implementation does.

---

# B. Design Handover

## 11. Overall visual direction

The modern redesign settled into a visual identity that the user liked.

Durable preferences include:

```text
dark-mode friendly
system theme support
light theme support
green-gradient visual identity
clean typography
minimal clutter
responsive layout
careful mobile behavior
restrained motion/JS
```

---

# 12. Theme behavior

The site eventually had:

```text
system
light
dark
```

theme selection.

The user personally tends to prefer dark mode.

Theme controls needed to remain visible/useful at tablet sizes rather than disappearing too aggressively.

---

# 13. Hero/banner history

The hero/banner went through substantial iteration.

Preferences discovered included:

```text
green gradient good
grid lines in sky bad
straight-edged hills preferred to curvy hills
visual gradient should continue naturally below demo
```

These are design context, not immutable architecture.

If redesigning the site later, preserve the overall taste rather than mechanically reproducing every historical decorative detail.

---

# 14. Interactive demo history

The homepage demo also evolved.

Issues/fixes included:

```text
double scrollbars
inconsistent demo height
layout cohesion
syntax highlighting
mobile sizing
```

At one stage an interactive/live-demo concept was reduced/replaced by more cohesive static code presentation.

The user did not want a live demo link merely for the sake of having one.

Lesson:

> Demonstration UI should explain Nift clearly, not become a mini-product that distracts from it.

---

# 15. Examples layout

The user preferred:

```text
one wide card per row
```

for examples rather than a cramped multi-column grid.

This improved readability and gave each example enough room to explain itself.

---

# 16. Template screenshots

Template cards eventually moved toward:

```text
one good representative screenshot per template
```

rather than excessive screenshot galleries.

Again, clarity over visual quantity.

---

# C. Documentation Handover

## 17. Major documentation areas

The modern site accumulated documentation covering approximately:

```text
Getting Started
Template Language
Commands
Configuration
Workflows / Patterns
Full Web Applications
Deployment / Hosting
Migration
AI-Assisted Development
Why Nift
Templates
Examples / Showcase
Battle Tested
comparisons/opinion
```

Current navigation wins.

---

# 18. Getting Started

Historical onboarding moved toward:

```bash
nift init .html
```

with:

```text
public/
```

as the default generated-output directory rather than `output/`.

### VERIFY CURRENT CLI

Do not preserve historical command examples if source has changed.

---

# 19. `public/` rename

The move:

```text
output/
→
public/
```

was deliberate.

It aligned better with common web-project conventions and made the generated/public asset boundary more intuitive.

When checking docs/examples/templates, search for stale `output/` assumptions.

---

# 20. Template-language documentation

Historically this included:

```text
@content
@input
@pathto
$[...]
@getenv
@ent
escaping
```

and later:

```text
@dep
```

as advanced material.

It now needs to include current:

```text
@json
loops
```

and eventually parameter interpolation once implemented.

---

# 21. `@dep` positioning

### STRONG PREFERENCE

`@dep` is useful but should not dominate beginner documentation.

It was intentionally treated as an advanced/escape-hatch mechanism.

Nift should teach ordinary relationships through the simpler primary operations first.

---

# 22. `@pathto` teaching

This was one of the most error-prone parts of our own earlier documentation work.

Do not casually treat `@pathto` as generic string path concatenation.

Its value is tied to Nift's build-aware relationship/requirement model.

Examples should reflect actual semantics.

Historically:

```text
tracked target
→ use tracked name
```

rather than supplying generated output path manually.

Current implementation should be inspected before writing examples.

---

# 23. `@pathtofile`

Historically this remained for compatibility while becoming less prominent in docs.

Do not assume its current status.

Verify.

---

# 24. Removed `@pathtopage`

Older `@pathtopage` material was removed.

Do not accidentally resurrect it from old documentation or examples.

---

# 25. Quoting conventions

The parser was simplified so that:

```text
'
"
```

are quote characters.

Backticks were deliberately not treated as a third quote syntax.

Examples generally shifted toward single quotes where that interacted better with syntax highlighting/readability.

---

# 26. CSS `@media`

One important parser improvement allowed ordinary CSS such as:

```css
@media (...)
```

to survive without requiring Nift escaping.

This represents a broader design principle:

> Nift syntax should coexist with normal web-language syntax rather than forcing users to rewrite unrelated CSS/JS.

---

# 27. Full web applications

A dedicated documentation area was added to counter the assumption that Nift was useful only for simple generated HTML.

Examples/concepts included:

```text
vanilla JS + Go backend
React islands
Vue/Svelte islands
Node/Express
Supabase
API-consuming frontends
```

The point is composability, not endorsement of a specific backend stack.

---

# 28. npm ecosystem

Nift does not need native abstractions for every npm package.

Users can compose external pipelines.

This is central to the philosophy.

---

# 29. TypeScript

TypeScript can be used with Nift through ordinary external tooling.

Historically this was part of the "wider ecosystem" story.

Now that tscc exists as a sibling project, be careful not to imply Nift requires tscc.

---

# 30. Deployment/hosting

Documentation included providers/workflows such as:

```text
Netlify
Vercel
GitHub Pages
AWS
Supabase
VPS
```

The purpose was to demonstrate that Nift's output is ordinary deployable web content.

Verify current provider guidance before updating.

---

# 31. Migration

A migration page emphasized that existing sites could adopt Nift without wholesale rewriting.

This is an important product advantage.

A useful mental model is:

```text
existing HTML/CSS/JS
        ↓
introduce repeated partials/templates
        ↓
track/generate with Nift
```

rather than:

```text
rewrite everything into framework-specific components
```

---

# D. AI Development Handover

## 32. AI-assisted development page

This became an intentional part of the website.

The idea was that Nift's small conceptual surface makes it unusually easy to explain to coding assistants.

The page included things like:

```text
barebones project
AI context prompt
correct @pathto examples
@input
@getenv
asset examples
```

and encouraged project-local AI context.

---

# 33. AI DX

We later used the term:

> AI DX / AI developer experience

for the broader question:

> How easy is a project/tool for coding agents to understand and operate correctly?

The term exists elsewhere; we did not claim invention.

Nift has unusually strong AI-DX characteristics because of:

```text
few concepts
ordinary files
predictable commands
deterministic behavior
explicit relationships
small template language
fast feedback
```

The new repository-local handovers are themselves part of improving AI DX.

---

# E. Templates Handover

## 34. Template collection

The website eventually had roughly ten templates rather than the original few.

Categories deliberately expanded beyond simple landing pages to include things such as:

```text
dashboard
SPA
docs
blog
```

because we wanted to demonstrate breadth.

Current template inventory should be taken from repository reality.

---

# 35. Template philosophy

Templates should be:

```text
real Nift projects
readable
useful starting points
consistent with current docs
not fake screenshots
```

A downloadable archive should correspond to what the card/screenshot advertises.

---

# 36. Barebones project

The barebones project became particularly important.

It serves:

```text
human onboarding
AI onboarding
template seed
testing seed
```

When scaffold conventions change, inspect it.

---

# F. Benchmark / Evidence Handover

## 37. 10,000-page benchmark

A major Nift comparison used a 10,000-page project.

Historical results included approximately:

```text
Nift full build: ~0.28 s first run
later full builds: ~0.17 s
no-change build-updated: ~0.046 s

Hugo:
~455 ms

Astro:
~5.13 s
```

These are **historical checkpoint measurements**, not evergreen guarantees.

Do not publish them without preserving their context or revalidating where appropriate.

---

# 38. Incremental performance

The more interesting result than full-build speed was often Nift's incremental behavior.

Shared partial and single-page edits could rebuild very quickly.

This became central to the user-experience argument.

---

# 39. Fan noise

Repeated Hugo/Astro runs visibly spun up machine fans more than Nift during our informal comparison.

This was a real user observation but not rigorous power-consumption evidence.

Treat it as anecdotal if ever mentioned.

---

# G. Battle-Tested Handover

## 40. Testing as product evidence

After the regression work became extensive, the site gained stronger "battle tested" positioning.

The right story is:

```text
Nift has been attacked across many bug families
```

not:

```text
Nift has exactly 492 tests
```

Counts age rapidly.

Failure-family methodology is more meaningful.

---

# H. Website Development Workflow

## 41. When Nift behavior changes

At a checkpoint:

```text
1. Identify affected docs.
2. Search globally for stale syntax.
3. Update canonical website source.
4. Build with candidate Nift.
5. Inspect build failures as product evidence.
6. Inspect affected rendered pages.
7. Check responsive behavior for visual changes.
8. Run Lighthouse when change justifies it.
9. Check links/assets/downloads.
10. Review generated diff.
11. Update handover if durable workflow/design knowledge changed.
```

---

# 42. Candidate self-hosting

This deserves an explicit invariant:

> For significant Nift release/checkpoint validation, build the Nift website with the exact candidate Nift executable.

This gives us:

```text
synthetic tests
+
real-world dogfooding
```

---

# I. Deployment / Branch Handover

## 43. Historical branch structure

The website has had a source/generated arrangement involving:

```text
stage
```

and a public/main deployment checkout.

I do not want to reconstruct exact commands from memory.

### VERIFY FROM CURRENT GIT

Codex should establish:

```text
authoritative source branch
generated branch
working-tree arrangement
build command
publication process
deployment target
```

and put exact operational instructions into `HANDOVER.md`.

---

# 44. Generated branch rule

Do not blindly place handover documents into generated deployment output.

The authoritative handover belongs with authoritative source.

Only copy it to generated output if there is a deliberate reason.

---

# PART II — NIFT REGRESSION SUITE

# A. Everything Handover

## 45. Why this suite exists

The Nift regression suite became one of the project's most important engineering assets.

Before it existed, Nift had years of practical use but many obscure edge cases had never been systematically attacked.

The suite transformed development from:

```text
this seems to work
```

into:

```text
these observable behaviors are protected
```

---

# 46. Independent contract

The standalone suite should ideally remain capable of testing an arbitrary Nift executable.

That means its conceptual API is:

```text
path to Nift executable
        ↓
black-box operations
        ↓
observable result
```

not:

```text
include Nift internal headers
```

This independence is valuable enough to preserve deliberately.

---

# 47. Why black-box tests matter

They survive:

```text
refactoring
architecture rewrite
class renaming
internal representation changes
possible future language port
```

If a future Rust Nift implements the same external contract, much of this suite should theoretically remain useful.

---

# B. Historical Evolution

## 48. Early suite

The suite initially focused on core behavior such as:

```text
@content
@input
@pathto
metadata
escaping
incremental build
watch
track/rm
commands
```

and grew rapidly.

---

# 49. Milestones

Historical progression included:

```text
146
211
245+
492+
```

tests/assertions/checks at different stages.

These are milestones, not current suite statistics.

The meaningful fact is cumulative adversarial expansion.

---

# C. Important Historical Bug Families

## 50. Backticks

A parser inconsistency treated backticks partly like quotes.

We decided:

```text
single quote
double quote
```

only.

Regression tests were adjusted accordingly.

---

# 51. Function-name lexical boundary

`@content<` exposed over-broad function-name parsing.

The parser was tightened.

Lesson:

> Always test syntax immediately adjacent to punctuation/text.

---

# 52. Unknown `@` syntax / CSS

CSS `@media` historically interacted badly with Nift parsing.

Modern behavior was changed to let unknown/non-Nift `@` constructs pass through appropriately.

This should remain protected because web-language coexistence is fundamental.

---

# 53. Watch initialization

One historical bug:

```text
WatchList::save()
```

created some state files but omitted a per-directory `tracked.json` expected by later logic.

The first build after watch could therefore fail.

This was fixed by ensuring consistent state initialization.

The broader test principle is:

> Test the complete lifecycle, not merely serialization functions individually.

---

# 54. Malformed watch JSON

Malformed:

```text
exts.json
watched.json
```

could crash/abort.

Tests drove controlled error handling.

---

# 55. `tracked.json` empty state

Zero tracked files once produced invalid JSON because serialization assumed at least one entry.

This was fixed.

Test empty collections explicitly.

---

# 56. JSON escaping

Titles containing quotes could corrupt tracked JSON.

Serialization was hardened.

This represents the general family:

```text
user-controlled metadata
→ persistent serialization
```

---

# 57. Structural JSON assumptions

Cases involving non-object members exposed unchecked RapidJSON assumptions.

The broader lesson was:

> Parsing syntactically valid JSON is not enough; validate expected structure/types before accessing them.

---

# 58. Targeted build status

A requested untracked name could previously produce an error-like condition while still returning success.

Exit status became part of the tested contract.

---

# 59. Build-updated status

Similar issues existed around failed updated pages and removed outputs.

Tests hardened failure propagation.

---

# 60. Deleted generated outputs

A generated output disappearing must be recognized as requiring action.

Incremental logic should not interpret unchanged source as meaning the project is valid when expected output is missing.

---

# 61. Directory dependency

Directory `@dep` behavior exposed unstable hashing/invalidation semantics.

Tests forced this area to become explicit.

Current behavior should be taken from current suite/source.

---

# 62. Hash cache refresh

Hash-mode build-auto could rebuild repeatedly after edits because cache state was not refreshed correctly.

Regression coverage was added.

---

# 63. Traversal

Commands such as:

```text
track
cp
mv
```

were attacked with `../` and related path escape cases.

This became part of Nift's filesystem-safety contract.

---

# 64. Collision protection

Later ruthless testing included collisions involving:

```text
tracked paths
derived content
output paths
```

because silent overwriting is dangerous.

---

# 65. Sub-second mtimes

A particularly important performance/correctness area was rapid edits.

The initial test could flake because it depended on filesystem timing.

It was replaced/improved with deterministic mtime manipulation within the same second.

Preserve this testing philosophy.

---

# 66. 32-bit hash collision

A deliberately constructed collision exposed the weakness of an earlier 32-bit hashing path.

This eventually led to 64-bit hashing.

This is a good example of adversarial testing going beyond ordinary use.

---

# 67. Dependency sidecars

Later work hardened:

```text
*.deps.json
```

invalidation/lifecycle behavior.

Dynamic dependency work should inspect these tests because the upcoming parameter interpolation feature can affect runtime-selected dependencies.

---

# 68. Lexical JSON scope

With `@json` and control flow, lexical scope became part of the language contract.

The suite should now protect scope behavior around:

```text
loops
nested scopes
shadowing
value resolution
```

according to current implementation.

---

# D. Regression Development Methodology

## 69. Bug workflow

Preferred:

```text
reproduce
↓
reduce
↓
make deterministic
↓
add regression
↓
confirm old implementation fails
↓
fix
↓
confirm focused pass
↓
attack siblings
↓
full suite
```

---

# 70. Test the family

If:

```text
malformed watched.json
```

fails, ask about:

```text
exts.json
tracked.json
dependency sidecars
configuration
```

Do not stop automatically at one filename.

---

# 71. Failure-only output

Historically the suite was designed to keep successful output relatively quiet while making failures obvious.

Test numbering was added so a failure in a large shell suite could be located quickly.

Preserve useful diagnostics.

---

# 72. Determinism over sleeps

Prefer:

```text
explicit timestamp
explicit temporary directory
explicit fixture state
```

over:

```text
sleep 1
hope
```

whenever practical.

---

# 73. Isolation

Tests should not depend on prior tests accidentally leaving state.

Each case/family should establish the state it needs.

---

# 74. Temporary projects

Black-box tests can cheaply create small Nift projects.

Use that power rather than constructing one enormous stateful test project where failures cascade.

---

# E. Testing New Language Features

## 75. `$[...]` parameter interpolation

The independent suite should own observable contract cases.

At minimum, once behavior is settled:

```text
whole-value interpolation
literal prefix
literal suffix
multiple values
metadata
JSON strings
loop-local values
nested lexical scope
shadowing
missing value
wrong JSON type
malformed $[
escaping
quotes
spaces
empty result
non-recursion
```

and directive integration:

```text
@input
@dep
@pathto
@json path argument
other eligible textual parameters
```

---

# 76. Dynamic dependency lifecycle

This is critical.

Example:

```text
selector = "a.html"
@input($[selector])
```

build.

Then:

```text
selector = "b.html"
```

rebuild.

The dependency graph/sidecar must cease treating `a.html` as the active dependency and begin treating `b.html` correctly.

Test:

```text
A → B
B → A
A removed after switching
B missing
selected path becomes invalid
```

as appropriate.

---

# 77. Non-recursive interpolation

If a value resolves to:

```text
$[other]
```

or:

```text
@input(...)
```

the generated text should not silently become a second round of Nift evaluation unless the settled language contract explicitly says so.

This boundary is important to preventing accidental general expression/template composition.

---

# 78. Injection boundaries

Values containing:

```text
quotes
parentheses
braces
commas
@ signs
$ signs
path separators
```

should be tested.

The value is data, not syntax.

---

# F. Relationship to C++ Tests

## 79. Separate responsibilities

Implementation-local C++:

```text
resolver internals
parser helpers
scope data structures
dependency update units
```

External regression:

```text
what users observe
```

Use both.

Do not weaken black-box coverage because internal unit tests are easier.

---

# G. Suite Handover Maintenance

## 80. When to update suite handover

Update durable testing docs when:

```text
new testing methodology emerges
suite ownership changes
canonical sync direction changes
new major failure family becomes important
fixture strategy changes
runner interface changes
```

Do not append every individual regression case to `HANDOVER.md`.

The test itself is the durable record for individual cases.

---

# PART III — MINIFY++

# A. Project Identity Handover

## 81. Naming history

This project was initially developed/referred to as:

```text
Sift
```

The name later became problematic partly because the GitHub namespace and general software name were crowded.

We explored alternatives.

The current favored/project identity became:

```text
Minify++
```

with:

```text
display/project name: Minify++
likely repository slug: minifypp
command: minify
```

Current repository wins.

---

# 82. Why Minify++ worked as a name

The name has a useful double reading:

```text
Minify++
      ├─ C++
      └─ more/better minification
```

without requiring a contrived acronym.

The command can remain obvious:

```bash
minify index.html
```

That separation between brand and executable is intentional.

---

# 83. Product purpose

Minify++ should be understood as:

> A small, fast standalone native minifier for HTML, CSS and JavaScript.

The "standalone" part matters.

Although it grew alongside Nift, it should not require Nift.

---

# 84. Philosophy

Minify++ belongs to the same general engineering school as Nift:

```text
small
focused
fast
few dependencies
ordinary CLI
easy to understand
easy to compose
```

Do not turn it into a general frontend toolchain.

---

# 85. What Minify++ is not trying to become

Unless product direction deliberately changes, resist feature creep toward:

```text
bundler
module resolver
tree shaker
transpiler
source compiler
framework pipeline
asset manager
package manager
```

Minification is enough of a problem.

---

# B. Relationship to Nift

## 86. Why Minify++ exists separately

Nift needed/benefited from minification capability, but separating the minifier provides:

```text
independent testing
independent use
clear architecture
independent benchmarks
independent release maturity
```

and prevents Nift's build engine from becoming cluttered with minifier-specific concerns.

---

# 87. Nift minification is opt-in

This was an important clarification.

Nift does **not** minify everything by default.

Users configure extensions to minify.

Conceptually:

```text
configured extension
        ↓
minify

not configured
        ↓
leave output unchanged
```

This was important to our release-risk reasoning.

---

# 88. Release-risk implication

Because minification is opt-in:

```text
ordinary Nift user
```

is not silently exposed to minifier transformations.

That lowers the degree to which Minify++ maturity needs to block all Nift releases, although Minify++ should still be held to a serious correctness bar when shipped.

---

# 89. Canonical source

We discussed an intended relationship:

```text
standalone Minify++
        ↓
Nift/minifypp
```

with standalone Minify++ likely canonical.

### VERIFY

Codex should establish this from actual repositories before documenting it as fact.

If true, document:

```text
canonical directory
copy/sync procedure
allowed Nift-specific wrapper files
verification command
```

---

# C. Correctness Philosophy

## 90. Why minifier correctness is unusually tricky

A build tool bug often causes:

```text
error
crash
missing output
```

A minifier bug can produce:

```text
valid-looking smaller file
```

whose semantics have subtly changed.

That is worse in some respects because it can reach production unnoticed.

Therefore:

> **Correctness outranks marginal byte savings.**

---

# 91. Conservative optimization is acceptable

Minify++ does not need to produce the smallest possible output on every adversarial file.

A transformation that saves two bytes but requires fragile semantic reasoning may not be worth it.

Prefer:

```text
obviously safe
well-tested
predictable
```

over maximum aggression.

---

# D. HTML Handover

## 92. HTML is not just tags

HTML contains:

```text
markup
text
attributes
comments
raw-text elements
preformatted content
embedded languages
whitespace-sensitive contexts
```

A simplistic whitespace stripper will be wrong.

---

# 93. Whitespace

Whitespace between tags may sometimes be removable, but text-node whitespace can affect rendering.

Examples involving:

```html
<span>A</span> <span>B</span>
```

are semantically different from:

```html
<span>A</span><span>B</span>
```

depending on layout/content.

Tests should preserve such distinctions.

---

# 94. Preformatted contexts

Be extremely careful around:

```text
pre
textarea
```

and any other context the implementation recognizes as whitespace-sensitive.

Current parser defines actual behavior.

---

# 95. Raw text

`script` and `style` contents cannot be minified as ordinary HTML token streams.

If Minify++ recursively invokes JS/CSS minification there, test boundary handling aggressively.

---

# 96. Comments

HTML comment removal can have edge cases.

Do not assume every comment-like sequence is safely disposable without checking the actual grammar/context supported by Minify++.

---

# E. CSS Handover

## 97. CSS token boundaries

CSS contains cases where whitespace is syntactically meaningful or prevents tokens from merging.

Test:

```text
selectors
calc
custom properties
URLs
strings
functions
numbers/units
comments
```

---

# 98. Custom properties

CSS variables can contain token sequences that are interpreted later.

Be conservative about rewriting their values.

---

# 99. `calc()` and modern CSS

Whitespace/operator rules and modern syntax can make aggressive compaction risky.

Corpus coverage should include contemporary CSS, not merely old CSS2-style examples.

---

# F. JavaScript Handover

## 100. JS is the highest-risk language

JavaScript minification can be deceptively difficult.

Important categories include:

```text
ASI
regex vs division
template literals
strings
escapes
comments
operators
numeric literals
optional chaining
private fields
Unicode identifiers
module syntax
```

according to the project's intended supported syntax.

---

# 101. Automatic semicolon insertion

Newline removal can change behavior.

Classic families around:

```text
return
throw
break
continue
postfix ++/--
```

and expression boundaries deserve explicit tests.

---

# 102. Token merging

Removing whitespace can transform two tokens into another token or change parsing.

Test adjacent:

```text
+
-
/
*
<
>
?
.
identifier
number
```

combinations systematically.

---

# 103. Regex/division ambiguity

A slash can mean:

```text
division
regex literal
comment
```

depending on parser context.

Any JS minifier logic handling slash must have strong coverage.

---

# 104. Template literals

Template strings combine:

```text
raw text
escapes
${ expressions }
nested syntax
```

and are a classic scanner trap.

---

# G. Minify++ Development Handover

## 105. Change methodology

For a minification change:

```text
1. Identify exact transformation.
2. Build minimal examples where it is safe.
3. Construct neighboring unsafe examples.
4. Add tests before broadening implementation.
5. Implement conservatively.
6. Run language-specific corpus.
7. Run full corpus.
8. Run semantic/runtime checks.
9. Run sanitizers.
10. Benchmark.
11. Synchronize Nift embedding if applicable.
12. Run Nift integration tests.
13. Update docs/site/handover if behavior changed.
```

---

# 106. Don't combine optimization and unrelated parser rewrites casually

If an optimization fails, we should be able to identify why.

Keep checkpoint scope coherent.

---

# H. Minify++ Testing Handover

## 107. Golden-output tests

Useful for deterministic lexical transformations:

```text
input
→
expected minified output
```

They make regressions obvious.

---

# 108. Semantic tests

Especially for JS:

```text
original
→ execute

minified
→ execute

compare
```

can detect behavior changes even if output spelling differs.

---

# 109. Corpus testing

Build corpora containing:

```text
small focused cases
real-world files
malformed files
modern syntax
large files
```

A hundred near-identical whitespace tests are less valuable than broad grammar coverage.

---

# 110. Property/differential ideas

FUTURE POSSIBILITY:

```text
minify(minify(x)) == minify(x)
```

may be useful as an idempotence property for supported valid inputs.

But don't assume idempotence blindly if the current transformation model makes multi-pass compaction intentional.

Verify before making it contract.

---

# 111. Fuzzing

This project is particularly suitable for sanitizer-backed fuzzing.

Potentially:

```text
mutate valid HTML/CSS/JS
run minifier
ensure no crash/hang
validate output
execute JS where feasible
```

---

# 112. Malformed input

Explicitly decide what malformed input means.

Possible policies:

```text
reject
preserve conservatively
best-effort minify
```

should be defined per current implementation rather than left accidental.

---

# I. Performance Handover

## 113. Performance dimensions

Measure:

```text
startup
many small files
large file throughput
HTML
CSS
JS
mixed workload
RSS
output size
```

as relevant.

---

# 114. Output size versus speed

A benchmark table should not imply:

```text
smaller = universally better
```

without considering semantic aggressiveness.

Likewise:

```text
faster
```

is not meaningful if the implementation performs fewer valid transformations.

---

# J. Nift Integration Handover

## 115. Integration checkpoint

After canonical Minify++ change:

```text
standalone Minify++ passes
↓
synchronize embedded version
↓
verify exact/expected diff
↓
compile Nift
↓
Nift C++ tests
↓
Nift external regression
↓
minification-specific Nift fixtures
↓
Nift website if minification enabled there
```

as appropriate.

---

# PART IV — MINIFY++ WEBSITE

# A. Purpose

## 116. Keep the site proportional to the tool

The site should explain Minify++ without making a small CLI look artificially complicated.

A strong opening is essentially:

> Fast HTML, CSS and JavaScript minification.

followed by obvious usage.

---

# 117. CLI-first explanation

The command is a strength:

```bash
minify index.html
minify styles.css
minify app.js
minify index.html styles.css app.js
```

The website should exploit that immediate understandability.

---

# 118. Explain rather than brand excessively

Avoid inventing:

```text
Minify++ ecosystem
Minify++ platform
Minify++ architecture universe
```

The smallness is part of the product.

---

# B. Content

## 119. Useful content areas

Likely:

```text
overview
installation
usage
supported file types
behavior/options
examples
benchmarks
testing/correctness
Nift integration
source/download
```

Current site decides exact structure.

---

# 120. Relationship to Nift

A concise explanation is enough:

```text
Minify++ is standalone.
Nift can use its minification engine for explicitly configured outputs.
```

Neither project should appear subordinate to the other.

---

# C. Benchmark Site Content

## 121. Claims need context

For comparisons, document enough of:

```text
tool versions
input corpus
machine
compiler flags
number of iterations
output size
```

to make the result interpretable.

---

# 122. Do not cherry-pick

If another tool:

```text
compresses slightly better but runs slower
```

say so if relevant.

If Minify++:

```text
runs faster but is deliberately more conservative
```

say so.

Trustworthy comparison is better long-term marketing.

---

# D. Correctness Content

## 123. Testing story

Given minifier risk, a testing page/section can be valuable.

Explain categories rather than stale raw test counts.

For example:

```text
HTML whitespace contexts
CSS token boundaries
JavaScript ASI
comments
strings
templates
real-world corpus
malformed input
sanitizers
```

only insofar as current suite actually covers them.

---

# E. Website Development

## 124. Behavior change

When Minify++ changes:

```text
search site for feature/limitation
update examples
build site
verify command snippets
verify benchmark claims if affected
review downloads/version strings
```

---

# 125. Design

I have less reliable historical design context here than for nift.dev.

Do not redesign it to mimic Nift automatically.

Preserve current design unless asked.

---

# F. Deployment

## 126. Verify repository reality

Codex should determine:

```text
is site built by Nift?
which branch is source?
which branch deploys?
is generated output tracked?
what command publishes?
```

I do not want to invent this.

Document it once verified.

---

# PART V — TSCC

# A. Everything Handover

## 127. Identity

tscc is the TypeScript/JavaScript compiler project developed alongside Nift and Minify++.

It should remain an independent tool.

The three projects need not become a tightly branded suite.

---

# 128. Relative project maturity

Historically we considered:

```text
Nift
    most mature

Minify++
    smaller, correctness-hardening stage

tscc
    ambitious long-term compiler effort
```

Codex may have moved tscc forward substantially since that assessment.

### VERIFY CURRENT MATURITY

---

# 129. Why compiler work is different

A website generator can often be reasoned about in terms of filesystem relationships.

A compiler has many interacting semantic stages.

Something that "works" in one syntax example may fail under:

```text
scope
evaluation order
modules
nested expressions
runtime behavior
diagnostics
lowering
```

Therefore tscc requires particularly disciplined checkpoints.

---

# B. Scope / Product Contract

## 130. Establish intended compatibility target

This is one of the first things Codex should make explicit from current docs/source.

Possible goals are very different:

```text
full TypeScript compatibility
large practical subset
transpilation-focused subset
JavaScript + selected TS syntax
alternative compiler with deliberate differences
```

Do not assume which.

---

# 131. TypeScript is a moving target

If compatibility with TypeScript is a goal, version matters.

Document:

```text
target TypeScript version/range
ECMAScript target(s)
module modes
```

if the project currently defines them.

---

# 132. Feature support needs dimensions

Instead of:

```text
supports feature X
```

consider:

```text
parses X
validates X
lowers X
emits X
runs X correctly
diagnoses invalid X
```

depending on compiler architecture.

---

# C. Architecture Handover

## 133. Reconstruct from source

I will not invent exact current class names or passes.

Codex should map:

```text
source
↓
lexer
↓
parser
↓
AST
↓
binding/scope
↓
type/semantic analysis
↓
transforms/lowering
↓
emission
↓
output
```

and note which stages actually exist.

Some compilers intentionally combine stages.

Document reality.

---

# 134. Architecture document value

Unlike the small websites, tscc probably **does** justify an `ARCHITECTURE.md` once the architecture is stable enough.

It can prevent future agents from solving transform problems in the parser or duplicating semantic state across passes.

---

# D. Development Philosophy

## 135. Implement vertical semantic slices

Prefer:

```text
one feature
through every required compiler stage
with tests
```

over:

```text
add twenty parser productions
and worry about semantics later
```

A narrow fully supported feature is more valuable than broad syntactic acceptance.

---

# 136. Reference compiler

Where compatibility is intended, use TypeScript as a reference.

For an example:

```text
input.ts
```

capture:

```text
Does tsc accept it?
What JS does it produce?
What does that JS do?
What diagnostics arise?
```

Then compare tscc at the appropriate level.

---

# 137. Reference is not automatically specification

There may be deliberate differences.

Before changing tscc to match a surprising TypeScript behavior, determine whether:

```text
compatibility is intended
```

for that area.

---

# E. Parser Handover

## 138. Parser changes need adversarial neighbors

For a new grammar feature, test:

```text
minimal valid
nested valid
adjacent punctuation
ambiguous prefix
malformed
unexpected EOF
newline boundary
comments
parenthesized form
```

as appropriate.

---

# 139. Parser recovery

If tscc has recovery behavior, test it explicitly.

A compiler should not:

```text
hang
loop
crash
consume arbitrary following declarations
```

on malformed syntax.

---

# F. Scope / Binding Handover

## 140. Scope bugs are high leverage

A seemingly local feature can interact with:

```text
function scope
block scope
class scope
loop bindings
closures
shadowing
imports
```

where supported.

Any scope fix should prompt sibling cases.

---

# 141. Shadowing

Always consider:

```ts
let x = ...

{
    let x = ...
}
```

and nested functions where relevant.

Do not let name-based shortcuts accidentally resolve the wrong binding.

---

# G. Transform / Lowering Handover

## 142. Preserve evaluation order

Transforms must not accidentally:

```text
evaluate expression twice
evaluate it too early
evaluate it too late
skip side effect
change short circuiting
```

This is one of the most important compiler testing principles.

---

# 143. Side-effect probes

Use examples containing:

```text
counter++
fn()
obj.getter
computed property
```

to expose incorrect lowering.

---

# 144. Temporary variables

If lowering introduces temporaries, test:

```text
name collision
scope
evaluation order
nested transforms
```

and inspect how the compiler guarantees hygiene.

---

# H. Emission Handover

## 145. Valid output is not enough

Generated JavaScript can parse successfully while being wrong.

Therefore combine:

```text
snapshot
+
runtime
```

where useful.

---

# 146. Formatting versus semantics

Do not chase byte-identical `tsc` formatting unless that is a project goal.

Semantic equivalence matters more.

---

# I. Diagnostics Handover

## 147. Decide diagnostic compatibility level

Possible levels:

```text
correct failure/success only
similar category
stable tscc messages
tsc-compatible messages
exact codes
```

Document intended level.

Do not accidentally create an impossible maintenance obligation.

---

# J. Runtime Validation

## 148. Execute output

For runtime features:

```text
compile with tscc
run with appropriate JS runtime
assert output/state
```

This should become routine.

---

# 149. Compare runtime against reference

Where compatibility is intended:

```text
tsc compile → run
tscc compile → run
```

and compare behavior.

This is stronger than output snapshots alone.

---

# K. Module Behavior

## 150. Modules are ecosystem-heavy

If tscc supports module features, test:

```text
imports
exports
default
renaming
cycles
relative paths
resolution
extensions
```

according to actual scope.

Do not casually expand module resolution into a massive package-manager compatibility project without a deliberate decision.

---

# L. Native Safety

## 151. Sanitizers

If tscc is native C/C++ as current context suggests, serious parser/AST changes should consider:

```text
ASan
UBSan
```

and other appropriate tooling.

Compiler inputs are untrusted text.

---

# M. Performance

## 152. Avoid premature micro-optimization

Compiler architecture correctness is worth more than shaving nanoseconds from an unstable pass.

Once semantics are protected, profile.

---

# 153. Benchmark dimensions

Potentially:

```text
startup
lexing
parsing
semantic work
transform
emit
whole compile
many files
large file
RSS
```

depending on instrumentation.

---

# 154. Keep benchmark evidence

Codex has already encountered benchmark-related JSON and temporary artifacts.

Some may encode useful historical checkpoints.

Classify before cleaning.

---

# N. Debug/Probe Artifacts

## 155. `.shadow-debug` and similar files

Codex explicitly raised uncertainty about:

```text
.shadow-debug
hidden probe files
temporary benchmark output
historical benchmark JSON
```

I cannot reliably tell which are permanent.

### DO NOT DELETE BLINDLY.

Use:

```text
git log
git blame
scripts
README
test references
```

to determine purpose.

Then classify:

```text
permanent fixture
debug facility
benchmark evidence
temporary residue
```

and document/clean accordingly.

---

# O. tscc Development Checkpoint

## 156. Suggested feature checkpoint

```text
1. Establish baseline.
2. Define exact semantic slice.
3. Query/reference TypeScript where relevant.
4. Add black-box contract case.
5. Add internal unit/pass test where useful.
6. Implement through all required stages.
7. Inspect output.
8. Execute output.
9. Test side effects.
10. Test nested/scope variants.
11. Test malformed form.
12. Run focused suite.
13. Run full suite.
14. Run sanitizers.
15. Benchmark if hot path changed.
16. Update support docs.
17. Update website.
18. Update handover/decisions if durable knowledge changed.
19. Review repository state.
20. Report checkpoint evidence.
```

---

# PART VI — TSCC REGRESSION SUITE

# A. Identity

## 157. Purpose

The independent tscc regression suite should answer:

> Does this compiler executable implement the externally observable behavior tscc claims to support?

This is the compiler equivalent of Nift's black-box contract suite.

---

# 158. Independence

Ideally it should accept/select an arbitrary compiler executable.

That makes it useful for:

```text
baseline
candidate
old release
alternate implementation
```

comparisons.

---

# B. Relationship to Local Regression Tests

## 159. Current duality

Codex has identified:

```text
tscc/tscc/regression
```

and a standalone:

```text
tscc-regression-suite
```

The intended canonical direction remains to be established from repository evidence.

---

# 160. Do not assume duplication is necessary

Possible architecture:

```text
local regression
    fast development integration

standalone suite
    independent public contract
```

or they may intentionally mirror.

Codex should determine which.

---

# 161. If mirrored

If they should be identical:

```text
choose canonical owner
document sync direction
automate diff verification
```

Do not rely on human memory.

---

# C. Test Categories

## 162. Positive compile

```text
valid supported input
→ success
```

---

# 163. Negative compile

```text
invalid/unsupported input
→ controlled failure
```

---

# 164. Emission

```text
input
→ expected emitted structure
```

Use snapshots where useful.

---

# 165. Runtime

```text
compile
→ execute
→ assert behavior
```

This is one of the strongest categories.

---

# 166. Differential

Where compatibility is intended:

```text
tsc
vs
tscc
```

on:

```text
acceptance
runtime behavior
diagnostic class
```

as appropriate.

---

# D. Feature Matrices

## 167. Don't use one happy-path test per feature

For a feature, think dimensions.

Example conceptual matrix:

```text
basic
nested
parenthesized
inside function
inside loop
shadowed variable
side-effect operand
malformed
boundary syntax
```

Not every feature needs every dimension.

Choose semantically relevant ones.

---

# E. Evaluation Order

## 168. Explicitly test once-only evaluation

A transform bug often looks like:

```text
fn()
```

being evaluated twice.

Use counters.

For example conceptually:

```ts
let calls = 0;
function value() {
    calls++;
    return ...
}
```

then assert both result and call count.

---

# 169. Short circuit

Where supported, test that:

```text
right side
```

is not evaluated when language semantics say it shouldn't be.

---

# F. Scope

## 170. Scope matrix

Where applicable:

```text
global
function
nested function
block
loop
class
module
```

plus shadowing.

---

# G. Error Behavior

## 171. Malformed input

The compiler should fail controllably.

Test:

```text
unexpected EOF
unclosed delimiter
bad token
invalid nesting
unsupported construct
```

according to parser goals.

---

# 172. No hangs

Timeout protection can be valuable for malformed parser tests if the harness supports it.

A parser infinite loop is an important regression class.

---

# H. Determinism

## 173. Stable environment

Tests should control:

```text
working directory
temporary directory
file names
module fixture paths
runtime environment
```

as much as practical.

---

# 174. Version-sensitive reference results

If tests invoke TypeScript/Node externally, record/pin relevant versions.

Otherwise a TypeScript upgrade can make the suite appear to regress when the reference changed.

---

# I. Benchmark Suite Relationship

## 175. Keep correctness and performance conceptually separate

Performance fixtures can live alongside regression infrastructure, but:

```text
faster
```

must not substitute for:

```text
correct
```

A compiler benchmark should only compare candidates that pass the relevant semantic contract.

---

# J. Regression Development

## 176. Bug workflow

```text
reproduce
↓
reference behavior
↓
minimal fixture
↓
failing test
↓
implementation fix
↓
focused tests
↓
sibling variants
↓
full suite
```

---

# 177. Compiler bug families

If one lowering case fails, ask:

```text
same operator under nesting?
same feature with side effects?
same feature in different scope?
same feature combined with another transform?
```

Do not explode combinatorially, but deliberately inspect neighbors.

---

# K. Suite Handover Maintenance

## 178. Document methodology, not every test

`TESTING.md` should explain:

```text
how to run
fixture structure
oracle model
runtime tests
differential tests
canonical ownership
determinism rules
how to add a regression
```

Individual test cases explain themselves through names/fixtures.

---

# PART VII — TSCC WEBSITE

# A. Everything Handover

## 179. The website has a special truthfulness burden

A compiler website can easily overstate maturity.

A phrase like:

> TypeScript compiler

can be interpreted by users as:

```text
drop-in replacement for tsc
```

even when that was not intended.

Therefore product positioning must track actual compatibility.

---

# 180. Capability claims must be evidence-backed

Do not add:

```text
supports decorators
```

because the parser recognizes `@`.

A public feature claim should correspond to the level of support the project actually promises.

---

# B. Support Matrix

## 181. Consider explicit status

Depending on current site design, a support matrix can distinguish:

```text
supported
partial
experimental
planned
unsupported
```

This can be much more honest/useful than a generic feature list.

---

# 182. Avoid stale matrices

A support matrix is only useful if checkpoint workflow updates it.

Therefore tscc feature completion should include:

```text
website support impact reviewed
```

---

# C. Examples

## 183. Use tested examples

One of the safest documentation rules for tscc:

> Prefer website examples that also exist as passing test fixtures, or are trivially derived from them.

That minimizes "documentation syntax" that the compiler doesn't actually support.

---

# 184. Executable examples

Where practical, verify:

```text
example.ts
→ tscc
→ generated JS
→ runtime
```

before publishing.

---

# D. Compatibility Wording

## 185. Be precise

Depending on current goals, wording might need to distinguish:

```text
TypeScript compiler
TypeScript-compatible compiler
compiler for a TypeScript subset
TypeScript-to-JavaScript compiler
```

These are not interchangeable.

Codex should derive the correct wording from current project intent.

---

# E. Performance

## 186. Benchmark comparisons

When comparing tscc with `tsc`, control:

```text
type checking
emit
module count
input size
cold/warm process
runtime
tool version
machine
```

A transpile-only compiler versus full type-checking `tsc` is not automatically an apples-to-apples comparison.

---

# 187. Claims should explain workload

Prefer:

> On benchmark X under configuration Y...

over:

> tscc is N times faster than TypeScript.

unless the evidence genuinely supports the broader claim.

---

# F. Project maturity

## 188. Let the website mature with the compiler

Early in compiler development, it is fine for the website to emphasize:

```text
what works now
what is being built
architecture/goals
benchmarks with context
```

rather than pretending to have ecosystem completeness.

---

# G. Relationship to Nift/Minify++

## 189. Sibling, not dependency

The site may mention the sibling projects, but tscc should not be presented as:

```text
Nift's TypeScript compiler
```

unless product direction explicitly changes.

Likewise Nift should not require tscc.

---

# H. Website Development

## 190. Feature checkpoint

After a compiler feature reaches validated checkpoint:

```text
inspect feature docs
inspect support matrix
inspect examples
inspect compatibility statements
inspect benchmark implications
build website
check links/downloads/version
```

---

# 191. Internal compiler work

A refactor with no observable change should generally **not** trigger website churn.

Do not update marketing simply because implementation changed.

---

# I. Deployment

## 192. Repository-specific reality

Codex previously noted that the tscc website appeared not to have the same publication arrangement as the Nift website.

Determine actual:

```text
source branch
generated branch if any
build command
deployment command
hosting
```

and document it.

Do not impose Nift's deployment architecture merely for symmetry.

---

# PART VIII — CROSS-PROJECT DEVELOPMENT HANDOVER

Now I want to transfer something less repository-specific: **how we tended to develop these projects together**.

# 193. Development was iterative rather than roadmap-heavy

A lot of progress came from:

```text
build something
↓
attack it
↓
discover failure class
↓
improve architecture
↓
encode regression
↓
repeat
```

rather than designing enormous speculative roadmaps.

Codex should feel free to investigate aggressively, but keep checkpoint scope bounded.

---

# 194. Adversarial work was unusually productive

For Nift especially, the most valuable rounds often began with:

> Try to break it.

rather than:

> Add feature X.

This exposed:

```text
persistent-state corruption
path traversal
hash collisions
mtime precision
dependency lifecycle
CLI status bugs
parser boundaries
```

that normal happy-path development missed.

The same style should be applied to Minify++ and tscc.

---

# 195. "Ruthless" testing does not mean random testing

The best adversarial tests target assumptions.

Ask:

```text
What does this code assume is always true?
```

Then violate that assumption.

Examples:

```text
array assumed non-empty
JSON member assumed string
mtime assumed sufficiently precise
hash assumed collision-free
path assumed inside project
dependency assumed static
token assumed followed by whitespace
AST child assumed present
```

This is much more valuable than random weird inputs alone.

---

# 196. Code review should generate tests

When inspecting source and seeing:

```cpp
something[0]
```

ask:

```text
Can it be empty?
```

If yes, build a test.

When seeing:

```cpp
GetString()
```

ask:

```text
Was type checked?
```

When seeing:

```cpp
path / userInput
```

ask:

```text
Can it escape root?
```

This methodology drove many Nift discoveries.

---

# 197. Prefer architectural fixes over symptom patches

If five tests fail because dependency lifecycle is wrong, fix the lifecycle model.

Don't add five special cases.

---

# 198. But avoid rewrites without evidence

We did eventually rewrite/simplify major Nift areas, but only after enough understanding existed.

Do not rewrite tscc passes or Minify++ scanners merely because a cleaner architecture can be imagined.

First establish:

```text
what is wrong
what behavior must remain
what evidence protects it
```

---

# PART IX — WEBSITE CHECKPOINT HANDOVER ACROSS ALL THREE SITES

# 199. Websites follow product checkpoints, not every edit

A product checkpoint should ask:

```text
Did observable behavior change?
```

If no:

```text
website may need nothing
```

If yes:

```text
docs/examples/claims may need reconciliation
```

---

# 200. Candidate versions should build their own docs where sensible

For Nift this is especially strong because Nift builds its website.

If Minify++/tscc websites are also Nift projects, that provides additional integration fixtures.

Verify rather than assume.

---

# 201. Website source is canonical

Never edit generated output as though it were source unless repository design explicitly says so.

---

# 202. Visual validation

For meaningful visual changes:

```text
desktop
tablet
mobile
light
dark
```

as relevant.

Not every typo requires exhaustive browser testing.

---

# 203. Lighthouse

We used Lighthouse significantly during Nift website work.

At one stage desktop reached:

```text
100 / 100 / 100 / 100
```

and mobile was subsequently improved.

Do not fetishize scores, but they are useful regression indicators for significant web changes.

---

# 204. Accessibility

High Lighthouse accessibility scores are useful, but actual semantics matter.

Maintain:

```text
heading structure
labels
contrast
keyboard behavior
responsive navigation
```

when modifying sites.

---

# PART X — HANDOVER FILE MAINTENANCE POLICY

I would like Codex to encode something close to the following in the repository handovers.

## 205. Suggested policy

> ### Maintaining this handover
>
> This document is part of the project's development infrastructure and must evolve with the project.
>
> Review it whenever a change affects architecture, workflows, repository relationships, build/test commands, release/deployment procedures, major testing methodology, durable design decisions, or important project rationale.
>
> Update existing sections rather than merely appending chronological notes. Remove or mark obsolete guidance. Split the document when size materially harms discoverability. Consolidate duplicated knowledge under a canonical owner.
>
> Individual bugs normally belong in tests and Git history rather than this handover. Add them here only when they reveal a durable rule, hazard, architectural lesson, or development practice.
>
> A substantial validated checkpoint should include a handover-impact review.

I would put that—or a project-appropriate shorter version—in every root handover.

---

# 206. Decision maintenance

`DECISIONS.md` should not merely say:

```text
we decided X
```

Prefer:

```text
Status: SETTLED

Decision:
Use constrained value interpolation rather than arbitrary nested template evaluation.

Why:
...

Rejected:
...

Revisit if:
...
```

This prevents historical conclusions from becoming unexplained dogma.

---

# 207. Superseded decisions

Do not delete important architectural history silently.

Use:

```text
SUPERSEDED
```

with the newer decision reference where useful.

But minor obsolete preferences can simply be removed.

---

# 208. Operational commands age fastest

Commands such as:

```text
build
test
publish
sync
benchmark
```

should live close to repository reality and be reviewed frequently.

Do not bury current commands inside a ten-page historical narrative.

This is why `HANDOVER.md` should remain concise and operational even if deeper context files become large.

---

# PART XI — WHAT I WOULD HAVE CODEX DO NEXT

After receiving this handover package, I would ask Codex **not to implement anything yet**.

Instead:

```text
1. Inspect all seven repositories.
2. Read current docs.
3. Read current tests.
4. Inspect recent Git history.
5. Establish branches and working-tree state.
6. Identify build/test commands.
7. Identify canonical/synchronized copies.
8. Classify obvious generated/debug/benchmark artifacts.
9. Compare repository reality with these handovers.
10. Produce reconciliation report.
```

Then specifically report contradictions.

For example:

```text
HANDOVER:
Standalone Minify++ was expected to be canonical.

REPOSITORY:
Nift/minifypp has three newer commits/features absent from standalone.

CONCLUSION:
Canonical direction is currently ambiguous; do not document until resolved.
```

That is exactly the kind of finding I want.

---

# 209. Reconciliation should classify claims

For every meaningful discrepancy:

```text
handover stale
repository likely regressed
deliberate newer decision
unclear
```

Do not silently choose.

---

# 210. Then propose document structure

After inspection, Codex can say something like:

```text
Nift website:
    HANDOVER.md sufficient

Nift regression:
    HANDOVER.md
    TESTING.md

Minify++:
    HANDOVER.md
    docs/DEVELOPMENT.md
    docs/TESTING.md
    docs/DECISIONS.md

Minify++ website:
    HANDOVER.md

tscc:
    HANDOVER.md
    docs/ARCHITECTURE.md
    docs/DEVELOPMENT.md
    docs/TESTING.md
    docs/DECISIONS.md

tscc regression:
    HANDOVER.md
    TESTING.md

tscc website:
    HANDOVER.md
```

But that should be a result of repository complexity, not this handover dictating filenames.

---

# PART XII — WHAT I THINK CODEX SHOULD UNDERSTAND ABOUT NICK'S DEVELOPMENT STYLE

This is useful context because Codex is taking over an existing collaboration rather than starting with an anonymous repository.

## 211. Evidence changes opinions

We have repeatedly changed our views based on experiments.

Examples include:

```text
Nift's stripped architecture initially looked perhaps too minimal.
Actual projects strengthened confidence in it.

React initially looked unnecessary for many Nift sites.
React-islands experiment showed a very clean boundary where state justified it.

Minification initially looked potentially release-blocking.
Explicit opt-in behavior reduced that concern.

Nift's performance looked impressive.
Scaling/adversarial testing was used to challenge rather than merely celebrate it.
```

Codex should preserve that willingness to update beliefs.

---

# 212. Do not flatter the projects

If Minify++ loses a benchmark, report it.

If tscc semantics are wrong, say so.

If Nift's architecture becomes awkward for a workload, investigate it.

The goal of testing is not to prove that our prior opinions were right.

---

# 213. But challenge conventional assumptions too

Likewise, do not assume:

```text
larger framework = more capable
more template features = better
React = required for applications
native tool = necessarily difficult DX
simple architecture = toy
```

Our Nift work repeatedly found those assumptions unhelpful.

Test the actual proposition.

---

# 214. Small conceptual surfaces are valued

There is a strong preference for designs where users can understand the model.

For Nift:

```text
operations
values
dependencies
requirements
tracked files
```

should remain understandable.

For Minify++:

```text
files in
minified files out
```

should remain understandable.

For tscc:

```text
source
compiler
JavaScript
```

should not acquire unnecessary ecosystem machinery unless needed.

---

# 215. Performance matters

Performance is not merely marketing here.

The user genuinely values:

```text
fast startup
fast incremental builds
quiet machine
small binaries
low memory
rapid edit/test cycles
```

But performance work must remain evidence-backed and correctness-preserving.

---

# 216. AI friendliness matters

The user intends to use AI heavily in development.

Therefore project structure should make it easy for coding agents to:

```text
understand commands
find authoritative docs
run tests
interpret failures
discover invariants
avoid stale assumptions
```

The handover system is partly being created for exactly this reason.

---

# 217. Ordinary technologies are preferred

A recurring preference is:

```text
HTML
CSS
JavaScript
C++
shell
JSON
ordinary files
```

over project-specific abstraction layers where those layers do not earn their complexity.

This does not mean "never use abstraction."

It means abstractions should solve real problems.

---

# PART XIII — PROJECT-SPECIFIC "DO NOT ACCIDENTALLY..." LIST

These might actually be useful as small sections in root handovers.

## Nift website

Do not accidentally:

```text
call Nift only a static site generator
restore removed scripting-era messaging
teach incorrect @pathto semantics
publish stale benchmark numbers
edit generated branch as canonical source
forget AI-facing docs
forget to build with candidate Nift
```

---

## Nift regression suite

Do not accidentally:

```text
couple it tightly to implementation internals
remove strange tests without understanding bug history
replace deterministic fixtures with sleeps
equate test count with coverage quality
silently change old contract to make new code pass
```

---

## Minify++

Do not accidentally:

```text
optimize size at the cost of semantics
turn it into a bundler/compiler
let standalone and embedded copies drift
assume HTML/CSS/JS whitespace rules are interchangeable
judge correctness solely by exact output snapshots
```

---

## Minify++ website

Do not accidentally:

```text
overcomplicate the product story
claim unsafe/aggressive optimization superiority
make it look dependent on Nift
publish benchmark claims without corpus context
```

---

## tscc

Do not accidentally:

```text
equate parsing with support
copy tsc behavior blindly without compatibility intent
ignore runtime semantics
duplicate side-effectful expressions in transforms
delete mysterious debug/benchmark evidence
broaden compiler scope casually
```

---

## tscc regression suite

Do not accidentally:

```text
test compile success only
ignore runtime output
depend on unpinned reference versions
duplicate local suite without canonical ownership
treat snapshot equality as semantic proof
```

---

## tscc website

Do not accidentally:

```text
claim full TypeScript compatibility implicitly
advertise parser-only features
let support matrix rot
publish unfair tsc benchmarks
use untested examples
```

---

# PART XIV — THE MOST IMPORTANT TESTING QUESTION FOR EACH PROJECT

I think these seven questions summarize the testing mentality extremely well.

### Nift website

> Can the candidate Nift build the real product website correctly, and does the resulting site still accurately explain the product?

### Nift regression suite

> What observable Nift behavior would a hostile or careless user expose that our current contract does not yet protect?

### Minify++

> Can this transformation change program/document meaning while still producing output that looks valid?

### Minify++ website

> Does every capability/performance claim correspond to behavior we have actually validated?

### tscc

> Does this feature preserve the source program's intended semantics through the entire compiler pipeline?

### tscc regression suite

> Can we demonstrate that semantics externally, preferably by executing the emitted program or comparing against a reference where appropriate?

### tscc website

> Are we claiming exactly the compiler we have, rather than the compiler we hope to have?

---

# PART XV — THE MOST IMPORTANT DEVELOPMENT QUESTION FOR EACH PROJECT

### Nift website

> Does this change make Nift easier to understand without making the site more complicated than Nift itself?

### Nift regression suite

> What failure family does this test protect?

### Minify++

> Is this optimization demonstrably safe?

### Minify++ website

> Can this be explained more simply?

### tscc

> What is the smallest complete semantic slice?

### tscc regression suite

> What observable evidence proves that slice works?

### tscc website

> Is this statement still true at the current validated checkpoint?

---

# FINAL HANDOVER TO CODEX

The biggest thing I want Codex to inherit from the previous ChatGPT development work is not a collection of historical implementation details.

It is the **way the projects gradually became understood**.

We repeatedly went through:

```text
assumption
    ↓
implementation
    ↓
real use
    ↓
adversarial testing
    ↓
unexpected evidence
    ↓
revised understanding
    ↓
simpler/stronger architecture
    ↓
permanent regression
```

Nift benefited enormously from that process.

It went from a larger tool with considerable embedded machinery to a much smaller architecture, and instead of becoming obviously less capable, repeated real-world use and increasingly hostile testing mostly strengthened the case for the simpler model.

Minify++ should receive the same treatment.

Don't assume a tiny minifier is correct because it works on normal files. Attack the lexical and semantic boundaries until we understand what transformations are genuinely safe.

tscc should receive the same treatment even more strongly.

Don't measure progress by grammar productions or feature names. Measure it by coherent semantic slices that survive runtime tests, scope interactions, malformed inputs, differential comparison, and cumulative regression.

The websites should then tell the truth revealed by that development rather than racing ahead of it.

And the regression suites should preserve that truth mechanically.

The handover documents now being created are the final piece of that methodology:

```text
tests
    preserve behavioral knowledge

Git
    preserves exact change history

code comments
    preserve local rationale

decision records
    preserve architectural reasoning

handover documents
    preserve development orientation and institutional memory

websites
    communicate validated product reality
```

Those layers should reinforce one another rather than duplicate one another.

Most importantly, **maintain these handovers for the lifetime of the projects**.

When a checkpoint teaches us something durable:

```text
capture it
```

When a handover statement becomes wrong:

```text
correct it
```

When a decision is reversed:

```text
mark it superseded and explain why
```

When a workflow becomes automated:

```text
replace the old manual instructions
```

When a document becomes too large:

```text
split it intelligently
```

When historical detail stops helping developers understand the present:

```text
move it to the appropriate historical layer
```

The objective is not to preserve this particular ChatGPT conversation forever.

The objective is to make the repositories eventually contain enough of their own accumulated understanding that **this conversation is no longer necessary to work on them intelligently**.

If we achieve that, the handover has succeeded.

And once Codex has reconciled these notes against the actual repositories, I would trust **that reconciled repository-local documentation over this message** for future development. That is exactly how it should be.
