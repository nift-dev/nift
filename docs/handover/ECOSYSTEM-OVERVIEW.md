# Ecosystem overview

> Nift canonically owns this living cross-project overview to avoid duplicating shared history into every sibling repository. Each sibling owns its local operational handover; current repositories remain authoritative.

This overview transfers the institutional memory needed to understand the seven repositories around Nift.

I would also deliberately give Codex **more than one conceptual handover per project**. I wouldn't necessarily tell it that all of these must become separate Markdown files—that should depend on repository size and what it finds—but there are really several different kinds of knowledge:

```text
PROJECT / EVERYTHING CONTEXT
    What is this thing?
    Why does it exist?
    How did we get here?
    What do we believe about it?

DEVELOPMENT
    How have we worked on it?
    How should changes be approached?
    What traps have we learned about?

TESTING / VALIDATION
    What constitutes evidence?
    What does this project's suite protect?
    How do checkpoints get earned?

OPERATIONS
    How is it built, synchronized, published, packaged, deployed?

DECISIONS
    What is settled?
    What is preference?
    What is unresolved?
    What was rejected?
```

For some of these repositories, Codex may ultimately collapse those into one `HANDOVER.md`. For others—particularly Minify++, tscc, and the Nift regression suite—I suspect multiple files will eventually be justified.

One extremely important qualification for everything below:

> **These are institutional-memory handovers, not claims that override the current repositories.**

Some of my knowledge of Nift and its website/suite is very detailed because I directly worked on them. My knowledge of the newer Minify++ and especially tscc repositories is less complete, and some development has occurred with Codex since the point at which I was directly manipulating those trees. I will mark uncertainty rather than manufacture specifics.

Codex should reconcile all of this against:

```text
current source
current tests
current Git history
current documentation
current website source
current branches
actual build commands
actual benchmark artifacts
```

before turning any statement into durable project documentation.

---

# ECOSYSTEM HANDOVER PREAMBLE

I would give Codex this preamble before the seven project handovers.

## The project map

The current ecosystem should be thought of approximately as:

```text
                         NIFT
                  website generator
                        │
             ┌──────────┴──────────┐
             │                     │
       Nift website        Nift regression suite
             │                     │
       real-world user       independent external
       + documentation       behavioral contract
             │
             │ embeds/uses
             ▼
         MINIFY++
     standalone native
       web minifier
             │
      ┌──────┴──────┐
      ▼             ▼
 Minify++       embedded copy
 website          in Nift


                         TSCC
              TypeScript/JavaScript
                    compiler
                       │
             ┌─────────┴─────────┐
             ▼                   ▼
        tscc website      tscc regression suite
                              +
                    implementation-local tests
```

These are related projects, but **do not turn them into one artificial framework or product suite**.

They share development values more than they share architecture.

Those values include:

```text
small native tools
explicit behavior
few dependencies
ordinary web/tooling compatibility
performance
correctness
determinism
good human DX
good AI-DX
strong regression protection
evidence rather than feature-count marketing
```

Each project should remain capable of having its own identity.

---

# Shared rule: canonical ownership

Avoid copying whole histories between repositories.

Conceptually:

```text
Nift
    owns Nift architecture/history

Nift regression suite
    owns external Nift behavioral-contract methodology

Nift website
    owns Nift website source/build/deployment/design conventions

Minify++
    owns Minify++ implementation/history

Minify++ website
    owns Minify++ website workflow/content

tscc
    owns compiler architecture/history

tscc regression suite
    owns independent compiler behavioral validation

tscc website
    owns tscc website workflow/content
```

A sibling repository should summarize the relationship and point toward the canonical owner rather than duplicate pages of detail.

---

# Shared rule: handovers are living documents

Every repository handover should contain this concept explicitly:

> **This handover is maintained project infrastructure, not a one-time onboarding artifact.**

During the project's lifetime, developers and coding agents should:

```text
update it
correct it
add new durable knowledge
remove obsolete guidance
mark superseded decisions
split documents when they become unwieldy
consolidate repetitive historical material
```

A meaningful development checkpoint should include:

```text
handover impact reviewed
```

alongside:

```text
tests reviewed
docs reviewed
website reviewed
```

when appropriate.

But handovers should **not become append-only diaries**. Detailed chronology belongs partly in Git/changelogs. Handover documents should distill durable knowledge.

---

# Shared rule: checkpoint development

All seven projects inherit the checkpoint philosophy already handed over:

```text
trusted baseline
      ↓
bounded development
      ↓
evidence
      ↓
coherent candidate
      ↓
validated checkpoint
      ↓
new trusted baseline
```

The newest files do not automatically become the trusted state.

---

# 1. NIFT WEBSITE

## Suggested documentation

Potentially:

```text
HANDOVER.md
docs/handover/
    WEBSITE-CONTEXT.md
    DEVELOPMENT.md
    DEPLOYMENT.md
    DESIGN.md
```

Whether those exact paths fit the repository should be determined from the current tree.

---

# Nift Website — Project Context Handover

## Identity

The Nift website is much more than a marketing page.

Historically it has simultaneously served as:

```text
official Nift documentation
product explanation
design showcase
real-world Nift project
integration fixture
AI onboarding resource
template distribution point
benchmark/evidence communication
```

That combination is important.

The site should therefore be treated as **part of the Nift product**, not as disposable marketing collateral.

---

## Product terminology

### SETTLED / STRONG CURRENT PREFERENCE

Prefer:

> **website generator**

over:

> static site generator

when describing Nift generally.

This became an important correction because Nift can generate the document/build layer of applications that include:

```text
vanilla JS applications
API-consuming frontends
React islands
Vue/Svelte islands
dashboards
SPAs
full-stack frontends
```

The fact that Nift itself runs at build time does not mean the resulting website must be "static" in the colloquial sense.

Codex has now independently demonstrated this by building a Nift + React-islands project successfully.

Do not casually regress the site back to framing Nift solely as an SSG.

---

## Central product story

The modern website emerged after Nift's architectural simplification.

Older Nift had much more machinery:

```text
LuaJIT
ExprTk
system scripting
pre/post hooks
broader template-programming capabilities
```

Those were deliberately removed.

The redesigned website therefore shifted toward:

```text
small website generator
fast builds
incremental builds
simple templating
ordinary HTML/CSS/JS
composability
external tooling
low conceptual overhead
AI-friendly development
```

The site should not nostalgically foreground removed machinery.

---

## Core messaging

Several phrases became useful expressions of the project's philosophy:

> **Keep your HTML. Keep your tools. Stop repeating yourself.**

> **Nift provides the glue without trying to become the universe.**

And a broader sentiment along the lines of:

> We don't care how you build your website. Here's a really fast templating/build layer. Carry on.

These are not immutable slogans, but they accurately capture the current architecture.

---

## Nift's simplicity should not be marketed as lack of capability

This is one of the hardest communication problems we encountered.

A user may see:

```text
@content
@input(...)
@pathto(...)
$[...]
```

plus the newer:

```text
@json(...)
loops/control flow
```

and initially think:

> "That's it?"

The website needs to communicate that much of Nift's capability comes from **not replacing the surrounding ecosystem**.

For example:

```text
HTML stays HTML
CSS stays CSS
JavaScript stays JavaScript
React stays React
npm stays npm
Go stays Go
REST APIs stay REST APIs
```

Nift supplies the build/template/dependency layer.

This is a stronger story than simply enumerating template functions.

---

# Nift Website — Historical Design Context

The redesign went through many visual iterations.

Durable preferences include:

```text
dark-mode-friendly
system/light/dark support
green-gradient visual identity
clean layout
responsive behavior
minimal unnecessary JS
strong typography/readability
no needless visual clutter
```

Historical fixes included:

```text
mobile menu behavior
double scrollbars
demo height consistency
card spacing
overflow
theme-toggle visibility
banner graphics
template screenshots
responsive layouts
```

Do **not** encode every pixel adjustment into permanent handover documentation.

Preserve the principles they revealed.

---

## Specific historical visual preferences

The user generally preferred:

```text
dark mode
green gradient at top
gradient continuing visually below hero/demo
straight-edged hills over curvy decorative hills
no grid lines in banner sky
one wide card per row on examples
consistent demo heights
clean screenshots
```

These are useful design context but should not become inviolable design law.

---

# Nift Website — Documentation History

The modern docs were substantially reorganized.

Important areas included:

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
Showcase / examples
Battle Tested / testing
comparison/opinion material
```

Current tree remains authoritative for exact navigation.

---

## Important documented conventions

Historically/currently important examples include:

### Project initialization

The stripped-era onboarding moved toward:

```bash
nift init
```

and the default output directory became:

```text
public/
```

rather than:

```text
output/
```

Verify current syntax before preserving this as current documentation.

---

## `@pathto` semantics

This was repeatedly misunderstood during earlier work.

The important conceptual distinction is that `@pathto` participates in Nift's build-aware path/requirement model.

Do not casually teach:

```text
@pathto("public/generated-output.html")
```

when the directive semantically expects a tracked name.

Historically, tracked-file references should use names such as:

```text
@pathto("work")
```

rather than generated output paths.

Current implementation must be checked because the language has continued evolving.

---

## `@dep`

`@dep` was intentionally treated as an **advanced/escape-hatch feature** in documentation rather than a first-class thing every beginner needs to understand.

Preserve that positioning unless current product direction changes.

---

## Removed/legacy language features

Historical documentation cleanup included removing or de-emphasizing:

```text
@pathtopage
LuaJIT
ExprTk
system scripting
backtick quoting
```

`@pathtofile` was retained for compatibility at one stage but no longer prominently documented.

Verify current implementation before documenting its present status.

---

# Nift Website — AI-Assisted Development

An AI-assisted development page became part of the site.

Its goals included:

```text
give an AI enough context to use Nift correctly
provide a barebones project
show correct @pathto usage
show @input
show environment/value usage
encourage project-local AI context
```

This was before our current, much richer handover architecture.

As Nift's language changes—for example `$[...]` interpolation inside directive parameters—the AI-facing examples need reconciliation too.

AI docs are a first-class documentation surface.

---

# Nift Website — Templates

The template collection expanded significantly.

Historically there were eventually around ten templates spanning categories such as:

```text
general sites
dashboard
SPA
documentation
blog
```

They were built from Nift rather than being arbitrary screenshots.

Important invariant:

> A template screenshot, downloadable archive, and source project should describe the same thing.

When templates change:

```text
build
inspect
capture representative screenshot
package
update card/metadata
verify download
```

---

# Nift Website — Barebones Project

The downloadable barebones project was particularly important because it served as:

```text
beginner starting point
AI starting point
template seed
test fixture
```

Changes to Nift's scaffold conventions should trigger review of the downloadable barebones artifact.

---

# Nift Website — Benchmarks and Claims

The site has included performance comparisons and Nift's 10,000-page results.

Historical evidence included roughly:

```text
Nift full 10k build: sub-second, around a few tenths of a second
Nift no-change incremental: tens of milliseconds
Hugo comparison: hundreds of milliseconds
Astro comparison: several seconds
```

Do **not** copy historical numbers into current docs without verifying the current benchmark methodology and checkpoint.

Performance claims should identify enough context to be meaningful.

---

## Fan-noise observations

The user noticed repeated Hugo/Astro benchmark builds caused noticeably more fan activity while Nift remained comparatively quiet.

This was experiential evidence, not a controlled scientific power benchmark.

If mentioned, frame it appropriately.

---

# Nift Website — Battle Tested

As Nift's regression work became more serious, the website gained stronger testing language.

The durable message should be:

```text
Nift is attacked adversarially across failure families
```

rather than:

```text
Nift has exactly N tests
```

Useful categories include:

```text
parser boundaries
malformed state
incremental invalidation
filesystem safety
dependency lifecycle
watch behavior
collisions
scaling
failure propagation
```

Exact categories should follow current suite reality.

---

# Nift Website — Development Handover

For a product behavior change:

```text
inspect affected documentation
search globally for stale syntax/terminology
edit canonical source
build with candidate Nift
inspect output
check links/assets
inspect rendered pages when visual
run relevant web-quality checks
review generated diff
```

The site build itself is part of Nift checkpoint evidence.

---

## Candidate-binary principle

When validating a Nift release/checkpoint:

> Build the Nift website using the exact candidate Nift binary.

This turns the website into a substantial real-world integration test.

---

# Nift Website — Branch/Deployment Handover

We historically had a source/generated arrangement involving `stage` and `main`/`public`.

Codex has already asked for exact operational details because source inspection is more authoritative than my recollection here.

Therefore:

### VERIFY FROM REPOSITORY

Determine:

```text
authoritative source branch
generated/deployment branch
build command
copy/sync behavior
publication command
GitHub Pages or equivalent setup
```

Do not invent this from historical conversation.

The durable principle is:

> Edit authoritative source, generate deployment output, do not hand-maintain two competing copies.

---

# Nift Website — Checkpoint Rule

A Nift product checkpoint that changes:

```text
syntax
behavior
commands
configuration
terminology
benchmarks
testing claims
AI guidance
```

should explicitly review the website.

Internal implementation changes that do not affect users need not create website churn.

---

# 2. NIFT REGRESSION SUITE

## Suggested docs

Potentially:

```text
HANDOVER.md
TESTING.md
DEVELOPMENT.md
CONTRACT.md
HISTORY.md
```

I think this repository particularly deserves a substantial `TESTING.md`.

---

# Nift Regression Suite — Everything Context

This suite became one of the most important assets in Nift's development.

Its purpose is not merely:

> Does today's implementation pass some tests?

It is closer to:

> **What observable behavior has Nift accumulated an obligation to preserve?**

The suite should remain as implementation-independent as practical.

---

# Historical evolution

The suite began relatively modestly and expanded through successive adversarial rounds.

Historical milestones included approximately:

```text
146 tests/assertions
211
245+
later 492+
further expansion afterward
```

Do not treat these exact numbers as current.

The important history is that **each bug discovery became an opportunity to protect a failure family permanently**.

---

# Core testing philosophy

The suite should test Nift from the outside.

Conceptually:

```text
arbitrary Nift executable
        ↓
temporary/project fixture
        ↓
commands
        ↓
filesystem/output/state
        ↓
assertions
```

It should not need to know whether Nift internally uses:

```text
class Parser
unordered_map
vector
filesystem helper X
```

That belongs in implementation-local testing.

---

# Major historical coverage families

The suite grew to cover areas including:

```text
@content
@input
@pathto
metadata/value access
escaping
quoting
incremental builds
hash/modified/hybrid modes
watch
track/rm/cp/mv
CLI validation
persistent JSON state
dependency behavior
missing files
deleted outputs
malformed data
filesystem traversal
failure statuses
parser adjacency
```

The current suite now also needs to reflect newer:

```text
@json
loops/control flow
lexical scope
```

functionality already added since the older handovers.

Codex should inventory actual current coverage.

---

# Important historical bug families

These matter because they explain why seemingly strange tests exist.

## Backtick quoting

Nift once treated backticks inconsistently as quote characters.

Decision:

```text
only single and double quotes
```

unless current implementation has deliberately changed.

---

## Function-name parsing

A parser issue caused something like:

```text
@content<
```

to consume too much as a function name.

A fix restricted function-name lexical recognition more tightly.

This is an example of a broader testing principle:

> Test punctuation immediately adjacent to syntax.

---

## CSS `@media`

Older parsing could require escaping CSS at-rules.

Modern stripped Nift was changed so unknown `@...` syntax could pass through appropriately, allowing ordinary CSS.

Regression significance:

> Nift must coexist with normal web-language syntax.

---

## Persistent JSON state

Important failures historically included:

```text
empty tracked.json becoming invalid
quotes in titles corrupting JSON
malformed watched/exts files causing aborts
non-object members causing unsafe assumptions
missing watch-directory state
```

These led to a broader principle:

> Persistent project state is untrusted input and must fail controllably.

---

## CLI status propagation

Several cases existed where Nift printed an error or failed a requested operation but still returned success.

The suite began protecting exit status as part of the public contract.

---

## Path safety

Tests were added around:

```text
../ traversal
track
copy
move
derived output collisions
```

This should remain a serious contract family.

---

## Incremental invalidation

Important historical cases included:

```text
shared partial changes
single content changes
deleted output
directory dependencies
hash cache refresh
same-second/sub-second edits
dependency sidecar lifecycle
```

These are core to Nift's value proposition.

---

# Deterministic fixture philosophy

Avoid timing luck.

The same-second dependency test was eventually made deterministic by forcing mtimes into controlled sub-second positions.

General rule:

> If a filesystem/time test can be deterministic, make it deterministic.

Avoid `sleep` as the primary correctness mechanism when direct control is possible.

---

# Failure output

At one point the suite was intentionally designed to be mostly quiet and report failures clearly.

Test numbering was added to failures so debugging a large suite was practical.

Preserve concise useful output unless current suite has intentionally evolved.

---

# Regression-suite Development Handover

When a bug is discovered:

```text
1. Reproduce externally.
2. Reduce to deterministic fixture.
3. Add failing regression.
4. Verify it fails against baseline for expected reason.
5. Fix Nift separately.
6. Verify new test.
7. Run related family.
8. Run entire suite.
```

---

# Feature-development workflow

For a new feature such as `$[...]` parameter interpolation:

write contract cases for:

```text
whole-value parameter
mixed literal + value
multiple interpolations
quoted/unquoted forms as specified
lexical scope
invalid value types
missing values
malformed syntax
escaping
non-recursion
directive-specific behavior
dependency switching
requirement switching
incremental rebuild
watch if applicable
path-safety parity
```

Do not merely test:

```text
@input($[foo])
```

once.

---

# External-suite independence

The standalone suite should ideally be runnable against:

```text
old Nift
candidate Nift
alternate implementation
future Rust Nift
```

without source modification.

That makes it a behavioral specification.

---

# Relationship to C++ tests

The future/now-existing implementation-local C++ tests serve a different role.

Conceptually:

```text
C++ tests
    internal unit/integration correctness

standalone suite
    public observable contract
```

Do not collapse the standalone suite into implementation internals.

---

# Suite synchronization

Codex previously asked whether the standalone suite or implementation-local copy is canonical.

I do not want to invent the answer.

### VERIFY

Inspect current repository arrangement and history.

Then document:

```text
canonical location
synchronization direction
files intentionally allowed to differ
verification mechanism
```

If exact synchronization is intended, automate a diff/check where useful.

---

# Suite checkpoint standard

A suite checkpoint may itself represent progress even without Nift source changes.

Examples:

```text
new adversarial coverage
deterministic fixture improvement
contract reorganization
failure-family expansion
better diagnostics
```

A test-count increase alone is not the objective.

---

# 3. MINIFY++

This is the project where naming history matters.

## Naming history

The HTML/CSS/JavaScript minifier was previously referred to during development as:

> **Sift**

The current intended/project name became:

> **Minify++**

with the likely/desired CLI:

```bash
minify
```

The display/project identity and executable name are intentionally allowed to differ.

Historical Sift references should be treated as references to the predecessor name unless the repository history shows otherwise.

---

# Suggested Minify++ docs

Potentially:

```text
HANDOVER.md
docs/handover/
    PROJECT-CONTEXT.md
    DEVELOPMENT.md
    TESTING.md
    DECISIONS.md
    INTEGRATION.md
```

---

# Minify++ — Everything Context

## Purpose

Minify++ is intended to be a:

```text
small
fast
native
standalone
HTML/CSS/JavaScript minifier
```

written in C++.

It grew partly from Nift development but should remain useful independently.

---

## Product philosophy

Minify++ should resist becoming:

```text
a JavaScript bundler
a transpiler
a CSS framework
an HTML optimizer with dozens of semantic transformations
a build system
```

Its value is focused minification.

Like Nift, its appeal partly comes from **doing one bounded job well**.

---

# Relationship to Nift

Nift can integrate/contain Minify++ functionality, but Nift's minification is **opt-in**, not automatic by default.

Historically, the intended Nift model became:

```text
user configures extensions to minify
        ↓
only those outputs are minified
```

This materially reduces Nift release risk compared with silently minifying every user's HTML/CSS/JS.

Preserve that distinction.

---

# Canonical-source question

The intended direction discussed with Codex was approximately:

```text
standalone Minify++
        ↓
Nift embedded minifypp
```

but Codex explicitly asked for confirmation.

Current repository history should determine the actual canonical arrangement.

Once verified, document it prominently.

Avoid two independent implementations drifting.

---

# Why standalone matters

Even though Minify++ originated alongside Nift:

```text
Minify++ should be testable
buildable
benchmarkable
documentable
releasable
```

without Nift.

That makes both projects cleaner.

---

# Correctness philosophy

Minifiers are unusually dangerous because the worst failure is often:

```text
input works
→ minifier emits smaller output
→ output silently changes semantics
```

rather than an obvious crash.

Therefore Minify++ correctness should generally outrank marginal size wins.

---

# HTML, CSS and JS are separate semantic domains

Do not assume one generic whitespace-removal model.

Each language has different traps.

Minify++ architecture/tests should respect those boundaries even if implementation shares scanning infrastructure.

---

# JavaScript caution

JavaScript is particularly dangerous because whitespace/newlines can affect semantics through mechanisms such as:

```text
automatic semicolon insertion
tokens joining
operators
regex/division ambiguity
strings/templates
comments
```

Current implementation and tests are authoritative for supported transformations.

Do not broaden optimization aggressively without semantic evidence.

---

# CSS caution

CSS minification has its own grammar and token-boundary issues.

Things that appear cosmetically removable may alter:

```text
calc expressions
custom properties
strings
URLs
comments
token separation
```

Again, inspect current implementation before documenting exact support.

---

# HTML caution

HTML combines:

```text
markup
text content
whitespace semantics
script/style raw text
attributes
comments
preformatted contexts
```

Minification must distinguish structural whitespace from content-significant whitespace.

---

# Development philosophy

Prefer transformations that can be justified locally.

A useful hierarchy is:

```text
obviously safe lexical removal
        ↓
well-tested grammar-aware compaction
        ↓
semantic rewriting only with very strong evidence
```

Minify++ does not need to win every possible compression contest.

---

# Minify++ Testing Handover

The suite should contain both:

```text
expected-output tests
semantic/equivalence tests where possible
```

For JS in particular, runtime comparison can be stronger than checking exact minified spelling.

Where practical:

```text
run original
run minified
compare observable result
```

for a corpus.

---

## Adversarial categories

Codex should inventory current tests, but useful families include:

```text
empty input
whitespace-only
comments
unterminated constructs
strings
escaped quotes
templates
regex
operators
adjacent tokens
HTML raw text
preformatted HTML
CSS custom properties
URLs
Unicode
malformed input
large files
mixed newline conventions
```

---

## Differential testing

Where trustworthy mature minifiers can provide useful comparison, differential testing may reveal edge cases—but do not assume another minifier's exact output is the specification.

The real question is semantic correctness.

---

# Fuzzing

Minify++ is an excellent fuzzing candidate.

Potential future work:

```text
random lexical inputs
structured grammar generation
mutation of valid corpus files
round-trip/runtime equivalence
sanitizer-backed fuzzing
```

Mark this as FUTURE POSSIBILITY unless already implemented.

---

# Sanitizers

For native parsing/scanning code, use:

```text
ASan
UBSan
```

as appropriate during serious checkpoints.

If multithreading exists/currently emerges, evaluate TSan where relevant.

---

# Performance

Minify++ should be fast, but benchmarks must preserve correctness.

Measure things such as:

```text
throughput
startup overhead
large files
many small files
peak RSS
output size
```

Do not optimize solely for one giant synthetic file if normal use is many web assets.

---

# Nift integration validation

When canonical Minify++ changes:

```text
standalone suite
        ↓
sync/integrate into Nift
        ↓
Nift local tests
        ↓
Nift external suite
        ↓
representative Nift minification build
```

should be considered.

---

# Minify++ checkpoint definition

A meaningful semantic checkpoint should answer:

```text
Does standalone build?
Does standalone suite pass?
Are sanitizers clean where relevant?
Did output semantics remain intact?
Did performance regress?
Is embedded Nift copy synchronized?
Does Nift integration still pass?
Are docs/site claims accurate?
```

---

# Minify++ Decisions / Naming

Current naming direction:

```text
Display: Minify++
Repository slug: likely minifypp
Executable: minify
C++ namespace: possibly minify
```

Repository reality wins.

The `++` intentionally suggests both:

```text
C++
```

and:

```text
minification taken further
```

without needing an acronym.

---

# 4. MINIFY++ WEBSITE

## Purpose

The Minify++ website should explain a **small tool simply**.

It should not inflate Minify++ into a platform.

A suitable product explanation is conceptually:

> Minify++ is a small, fast HTML, CSS and JavaScript minifier written in C++.

The website should let the simplicity remain a strength.

---

# Suggested docs

Probably initially:

```text
HANDOVER.md
```

plus perhaps:

```text
DEVELOPMENT.md
```

if the repository warrants it.

I would not create five ceremonial handover files for a small site.

---

# Website content priorities

Users should quickly understand:

```text
what it does
supported formats
how to install/build
how to invoke `minify`
what happens to files/output
how to integrate it into workflows
performance/correctness philosophy
relationship to Nift if useful
```

---

# Avoid overbranding

One reason Minify++ became attractive as a name was that the command could remain beautifully boring:

```bash
minify index.html
minify styles.css
minify app.js
minify index.html styles.css app.js
```

The website should preserve that directness.

---

# Benchmark claims

If performance is marketed, benchmark:

```text
same files
same machine
same build mode
same output/correctness assumptions
```

and distinguish:

```text
speed
output size
semantic aggressiveness
```

A minifier producing smaller output through unsafe transformations is not necessarily better.

---

# Correctness messaging

Given minifier risk, the website should avoid language implying infallibility.

Evidence-based language around:

```text
regression corpus
adversarial testing
sanitizers
semantic checks
```

is preferable.

---

# Relationship to Nift

Explain if useful:

```text
Minify++ is standalone.
Nift can use/embed it for opt-in minification.
```

Do not imply users need Nift to use Minify++.

Likewise do not make the Minify++ site primarily an advertisement for Nift.

---

# Development workflow

For a Minify++ behavior change:

```text
update implementation first
validate standalone
update docs/site claims
build website
inspect examples
verify command snippets
```

If the website itself is built with Nift, verify that from the repository and document it.

Given our ecosystem philosophy, it would be entirely sensible for these sites to be Nift-built, but I will not state that as current fact without repository confirmation.

---

# Visual identity

I do not have enough authoritative historical information to prescribe a detailed Minify++ website design language.

Codex should inspect the actual current site.

Do not simply clone Nift's green-gradient identity unless that is already intentional.

Related tools do not need identical branding.

---

# Website checkpointing

Website checkpoints can be independent from Minify++ executable versions.

A CSS/layout improvement does not imply:

```text
Minify++ version bump
```

A semantic feature may require docs updates but not necessarily a website checkpoint number.

---

# 5. TSCC

This is the area where I want Codex to be particularly careful about distinguishing my context from repository evidence.

My direct historical involvement in the current tscc implementation is less complete than for Nift.

Therefore the handover should transfer **project intent and development principles**, while Codex reconstructs exact compiler architecture from source.

---

# Suggested tscc docs

This project likely justifies:

```text
HANDOVER.md
docs/handover/
    PROJECT-CONTEXT.md
    DEVELOPMENT.md
    TESTING.md
    ARCHITECTURE.md
    DECISIONS.md
    RELEASES.md
```

depending on existing docs.

---

# tscc — Project Identity

tscc is the TypeScript/JavaScript compiler project being developed alongside Nift and Minify++.

It should be treated as an independent compiler project, not as a Nift plugin.

The relationship is philosophical/tooling-oriented rather than architectural necessity.

---

# Name

Current name:

```text
tscc
```

The GitHub organization/name collision discussion does not imply the project itself should be renamed.

Repository/org naming can differ from display/project naming.

---

# Ambition level

Historically, we regarded tscc as the **longest-term and most ambitious** of the three core tools.

Roughly:

```text
Nift
    mature/release-near

Minify++
    smaller but needs correctness hardening

tscc
    compiler-scale long-term development
```

Current maturity may have advanced since then. Verify.

---

# Compiler correctness model

tscc should never equate:

```text
parser accepts syntax
```

with:

```text
feature supported
```

A compiler feature may involve:

```text
lexing
parsing
AST
binding/scope
type behavior
transform/lowering
emission
runtime semantics
modules
diagnostics
source positions
```

depending on architecture and scope.

Checkpoint claims must reflect the whole relevant pipeline.

---

# Scope discipline

One of the biggest risks in a TypeScript/JavaScript compiler is endless scope expansion.

Codex should establish from current source/docs exactly what tscc intends to support.

Do not infer:

```text
"TypeScript compiler"
→ must immediately implement every behavior of tsc
```

The supported subset/compatibility target should be explicit.

---

# Reference behavior

Where tscc intends TypeScript compatibility, the official/reference TypeScript compiler is useful as an oracle.

But distinguish:

```text
intentional compatibility
intentional divergence
unsupported behavior
bug
```

before treating every difference as a tscc defect.

---

# Compiler development philosophy

For each new feature:

```text
define supported semantics
add external contract
add focused internal tests
implement smallest coherent slice
compare emitted JS
run emitted JS where relevant
compare diagnostics where relevant
run old corpus
```

This is slower than adding parser productions indiscriminately but much safer.

---

# Runtime behavior is crucial

For transformations, one of the strongest tests is:

```text
source TS
    ↓
tscc
    ↓
JS
    ↓
execute
    ↓
observable result
```

and where applicable compare against:

```text
source TS
    ↓
reference compiler
    ↓
JS
    ↓
execute
```

This catches bugs that snapshots miss.

---

# Emission snapshots

Exact emitted-JS tests are still useful because they catch:

```text
unexpected transformation changes
formatting changes
lowering changes
module changes
```

but exact text equality is not always equivalent to semantic correctness.

Use both kinds of evidence.

---

# Diagnostics

If diagnostics are part of tscc's intended contract, test:

```text
failure/success status
diagnostic category
source location
message semantics
```

according to current project goals.

Do not promise byte-identical `tsc` diagnostics unless that is intentionally a goal.

---

# Hidden/debug artifacts

Codex previously specifically asked about:

```text
.shadow-debug
hidden probe files
benchmark temporary outputs
historical benchmark JSON
```

because it found them in/around tscc.

I cannot authoritatively classify all of those.

### VERIFY BEFORE CLEANING

Some may be intentional evidence.

Some may be investigative residue.

Do not delete them simply because they look temporary.

Classify them from:

```text
Git history
scripts
test references
benchmark tooling
documentation
```

first.

---

# tscc Development Handover

A feature-development checkpoint should ideally progress:

```text
1. Establish current supported behavior.
2. Ask reference compiler what it does, where compatibility applies.
3. Write external regression.
4. Write focused internal test if appropriate.
5. Trace compiler stages involved.
6. Implement coherent semantic slice.
7. Inspect emitted JS.
8. Execute emitted JS.
9. Compare reference behavior.
10. Test errors/edge cases.
11. Run full suite.
12. Run sanitizers if native implementation.
13. Benchmark if hot path affected.
14. Update support matrix/docs/site.
```

---

# Compiler bug-family thinking

One failure often implies siblings.

If one bug concerns:

```text
nested lexical scope
```

ask about:

```text
functions
blocks
loops
catch bindings
shadowing
closures
```

where supported.

If one concerns:

```text
optional chaining
```

ask about:

```text
property
element
call
nested
side effects
short-circuit behavior
```

where relevant.

Do not patch one AST shape blindly.

---

# Compiler performance

Compiler benchmarks should distinguish:

```text
startup
lex/parse
transform
emit
whole compile
many small files
large single file
memory
```

if architecture/tooling permits.

Do not sacrifice semantic clarity prematurely for micro-optimizations.

---

# Differential testing

tscc is particularly suited to differential testing.

For supported syntax:

```text
tscc result
vs
TypeScript result
```

can compare:

```text
accept/reject
runtime behavior
possibly emitted semantics
diagnostic category
```

Do not require identical formatting.

---

# Fuzzing / generated programs

FUTURE POSSIBILITY unless current repository already does it.

Compiler testing could eventually use:

```text
grammar-generated TS subsets
AST mutation
random expression generation
differential execution
sanitizer builds
```

This could become extremely powerful once the supported language subset is sufficiently explicit.

---

# tscc checkpoints

Because tscc is larger in scope, I would make checkpoint objectives particularly narrow:

```text
class fields checkpoint
optional chaining checkpoint
module-resolution checkpoint
scope hardening checkpoint
parser recovery checkpoint
```

rather than:

```text
TypeScript support improvements
```

---

# 6. TSCC REGRESSION SUITE

Codex has already identified an important repository relationship here:

```text
tscc/tscc/regression
```

and a standalone:

```text
tscc-regression-suite
```

The exact canonical direction must be verified.

---

# Suggested docs

```text
HANDOVER.md
TESTING.md
CONTRACT.md
DEVELOPMENT.md
```

This repository probably benefits more from testing documentation than general product history.

---

# Purpose

The standalone tscc regression suite should ideally answer:

> **Does this tscc executable implement the externally observable compiler behavior we claim to support?**

It should remain sufficiently independent that it can test:

```text
current tscc
older tscc
candidate tscc
alternate implementation
```

where practical.

---

# Relationship to implementation-local regression

The two suites may have different roles.

Potential model:

```text
implementation-local tests
    fast/internal/developer-oriented

standalone suite
    independent black-box contract
```

But Codex should verify actual current intent.

Do not mechanically duplicate every test in both places unless synchronization is explicitly the design.

---

# Canonical synchronization question

This was one of Codex's original handover questions.

It specifically asked whether:

```text
tscc/tscc/regression
```

is canonical and standalone suite mirrors it, or vice versa.

I do not have enough verified information to declare this.

Codex should determine it from the repositories and then encode the answer in both handovers.

If they are intended to match exactly, consider an automated synchronization check.

---

# Testing dimensions

A serious compiler regression suite should distinguish:

### Acceptance

```text
valid supported program compiles
```

### Rejection

```text
invalid/unsupported program fails correctly
```

### Emission

```text
generated JavaScript has expected structure
```

### Runtime

```text
generated JavaScript behaves correctly
```

### Differential

```text
behavior agrees with reference compiler where compatibility is intended
```

### Stability

```text
malformed input fails controllably
```

### Performance

```text
representative workloads remain within expected envelope
```

Not every case needs all dimensions.

---

# Runtime tests are particularly valuable

A compiler can emit syntactically valid but semantically wrong JavaScript.

Therefore:

```text
compile succeeded
```

is weak evidence by itself.

For runtime-relevant transformations, execute output.

---

# Side-effect-sensitive tests

When testing transforms, include expressions where evaluation count/order matters.

For example, where supported:

```text
getter calls
incrementing counters
function calls
computed properties
short-circuiting
```

These expose transforms that duplicate or reorder evaluation.

---

# Scope tests

Compiler regressions frequently hide in scope.

Test where relevant:

```text
shadowing
nested blocks
functions
closures
loop variables
class scopes
imports
```

according to supported language features.

---

# Error-path tests

Malformed/unsupported syntax should not:

```text
abort
hang
corrupt output
silently compile nonsense
```

unless explicitly designed behavior dictates otherwise.

Test controlled failure.

---

# Determinism

Compiler tests should avoid environment dependence where possible.

If tests depend on:

```text
filesystem paths
module resolution
timestamps
working directory
Node version
```

make assumptions explicit.

---

# Test fixture ownership

Each fixture should make clear:

```text
input
expected compile status
expected output/runtime/diagnostic
```

Avoid mysterious files whose purpose can only be reconstructed from shell-script line numbers.

---

# Historical benchmark artifacts

Again: Codex found historical JSON/temp evidence in tscc.

Do not automatically incorporate all of it into the standalone suite.

First decide whether each artifact is:

```text
permanent fixture
benchmark result
debugging residue
historical evidence
```

---

# Suite checkpoint philosophy

A good suite checkpoint might be:

> Added runtime/differential coverage for all currently supported optional-chaining forms and evaluation-order edge cases.

That is much stronger than:

> Added 38 tests.

---

# 7. TSCC WEBSITE

## Identity

The tscc website should communicate a compiler project whose capabilities are **evidence-backed and explicitly scoped**.

This is particularly important because the phrase "TypeScript compiler" can imply near-total compatibility with Microsoft's TypeScript toolchain.

Do not let marketing outrun implementation.

---

# Suggested docs

Probably:

```text
HANDOVER.md
DEVELOPMENT.md
```

and perhaps a deployment section/file if publication is nontrivial.

---

# Capability claims

Every statement such as:

```text
supports feature X
```

should correspond to a meaningful validated checkpoint.

For compiler features, that ideally means more than parser acceptance.

The site should distinguish where useful:

```text
supported
partially supported
planned
experimental
unsupported
```

rather than forcing a binary marketing story.

---

# Compatibility positioning

Codex should inspect current project intent before choosing language such as:

```text
TypeScript-compatible
TypeScript compiler
TypeScript subset
alternative TypeScript compiler
```

These have materially different implications.

Repository/docs current truth wins.

---

# Performance claims

Compiler performance comparisons need careful methodology.

If comparing with `tsc`, document:

```text
input corpus
cold/warm conditions
emit/no-emit behavior
type-checking scope
module count
machine/tool versions
```

Comparing unlike workloads is misleading.

---

# Benchmark artifacts and website

Do not manually type benchmark numbers into several pages if a more maintainable evidence path exists.

But do not overengineer dynamic benchmark generation prematurely.

At minimum, keep:

```text
benchmark source
website claim
```

traceable to one another.

---

# Examples

Website examples should be actual supported tscc programs.

Prefer examples that are also fixtures/tests.

The ideal loop is:

```text
test fixture
    ↓
known supported behavior
    ↓
documentation example
```

rather than inventing flashy syntax from memory.

---

# Development workflow

When a tscc feature lands:

```text
implementation
→ internal tests
→ standalone regression
→ runtime/differential validation
→ checkpoint
→ support docs
→ website capability claim
```

not:

```text
parser implementation
→ homepage badge
```

---

# Website build/deployment

Codex previously observed that Minify++ and tscc sites may not have the same multi-branch publication structure as the Nift site.

### VERIFY

Determine:

```text
branch structure
canonical source
build tool
deployment target
publication command
whether Nift builds the site
whether candidate binaries are involved
```

Document actual reality.

Do not copy Nift's `stage`/generated-branch arrangement simply for consistency.

---

# Visual identity

I do not have enough reliable context to prescribe detailed tscc site styling.

Codex should preserve the existing site's design unless asked to redesign it.

The three products do not need to look like a corporate suite.

---

# Documentation maturity

Because tscc is likely to evolve rapidly, its website needs particular protection against stale feature matrices.

At each compiler checkpoint ask:

```text
Did support change?
Did limitation change?
Did CLI change?
Did benchmark evidence change?
Did installation/build workflow change?
```

Only then update affected surfaces.

---

# CROSS-PROJECT HANDOVER: WHAT CODEX SHOULD NOW DO

Once Codex receives all of these handovers, I would **not** have it immediately create a mountain of Markdown.

Its proposed approach earlier was better:

```text
handover knowledge
        ↓
repository inspection
        ↓
reconcile against reality
        ↓
propose exact document map
        ↓
write only useful documents
```

I would explicitly ask it to inspect all eight areas:

```text
Nift
Nift website
Nift regression suite

Minify++
Minify++ website

tscc
tscc regression suite
tscc website
```

with Nift's own comprehensive handover already supplied separately.

---

# What it should determine for every repository

For each:

```text
repository root
current branch
Git status
recent history
current version
authoritative source
build command
test command
benchmark command
sanitizer command
release process
deployment process if site
relationship to siblings
generated files
tracked evidence
known temporary residue
```

Then compare those facts with the handovers.

---

# Suggested final handover architecture

After reconciliation, I suspect something roughly like this will emerge.

## Nift

```text
HANDOVER.md
docs/handover/
    PROJECT-CONTEXT.md
    DEVELOPMENT.md
    TESTING.md
    RELEASES.md
    DECISIONS.md
```

Potentially architecture-specific material too.

## Nift regression suite

```text
HANDOVER.md
TESTING.md
```

Maybe `CONTRACT.md` if useful.

## Nift website

```text
HANDOVER.md
```

plus perhaps:

```text
DEVELOPMENT.md
```

if deployment/source-branch mechanics justify it.

## Minify++

```text
HANDOVER.md
docs/handover/
    PROJECT-CONTEXT.md
    DEVELOPMENT.md
    TESTING.md
    DECISIONS.md
```

if its complexity warrants that many.

## Minify++ website

Probably:

```text
HANDOVER.md
```

unless publication mechanics are substantial.

## tscc

Likely:

```text
HANDOVER.md
docs/handover/
    PROJECT-CONTEXT.md
    ARCHITECTURE.md
    DEVELOPMENT.md
    TESTING.md
    DECISIONS.md
    RELEASES.md
```

A compiler can justify this depth.

## tscc regression suite

```text
HANDOVER.md
TESTING.md
```

possibly:

```text
CONTRACT.md
```

## tscc website

Probably:

```text
HANDOVER.md
```

plus `DEVELOPMENT.md` only if useful.

---

# Don't create empty bureaucracy

I would strongly tell Codex:

> **Do not create every file in that proposed tree merely because ChatGPT named it.**

If:

```text
Minify++ website HANDOVER.md
```

can clearly explain everything in four pages, excellent.

Don't split it into:

```text
PROJECT-CONTEXT.md
DESIGN.md
DEVELOPMENT.md
DEPLOYMENT.md
DECISIONS.md
```

with half a paragraph in each.

The documentation architecture should grow with the projects.

---

# CROSS-PROJECT DEVELOPMENT PRINCIPLES

There are some principles I would deliberately repeat briefly in each root `HANDOVER.md`, because they are operationally important enough that requiring an agent to discover the ecosystem handover first would be fragile.

## 1. Repository truth wins

```text
source/tests/docs/Git
```

are authoritative for current implementation.

Handover explains reasoning.

---

## 2. Don't destroy unknown work

Before editing:

```bash
git status
```

Understand uncommitted changes.

Never assume unexplained files are disposable.

---

## 3. Establish baseline

Before substantial changes:

```text
build
tests
relevant benchmark
```

as appropriate.

---

## 4. Work toward validated checkpoints

Do not silently treat latest code as trusted baseline.

---

## 5. Tests accumulate

A discovered bug should usually become a permanent regression.

---

## 6. Test failure families

Don't stop at the first reproducer if it exposes a broader class.

---

## 7. Evidence before claims

Especially:

```text
performance
memory
compatibility
correctness
feature support
```

---

## 8. Update institutional memory

Review handovers after meaningful development.

---

## 9. Public actions require explicit approval

Unless Nick later delegates otherwise:

```text
commit?
follow current instruction

push
tag
release
publish website
create irreversible public artifact
```

should not be inferred merely from "implement this."

Codex already understood this boundary.

---

# CROSS-PROJECT CHECKPOINT MATRIX

I think this compact matrix would be genuinely useful to Codex.

| Change                     |   Core tests | External suite | Sanitizers |                 Benchmark |                       Website |                          Handover |
| -------------------------- | -----------: | -------------: | ---------: | ------------------------: | ----------------------------: | --------------------------------: |
| Nift parser semantics      |            ✓ |              ✓ |    usually |                     smoke |                        docs ✓ |                                 ✓ |
| Nift internal refactor     |            ✓ |              ✓ |     useful |               if hot path |                    usually no |           if architecture changed |
| Nift performance           |            ✓ |              ✓ |     useful |                         ✓ |              if claims change |               ✓ if lesson durable |
| Minify++ semantics         |            ✓ |       ✓/corpus |          ✓ |                    useful |                docs if public |                                 ✓ |
| Minify++ performance       |            ✓ |              ✓ |     useful |                         ✓ |              if claims change |                ✓ if architectural |
| tscc language feature      |            ✓ |              ✓ |     useful |               if hot path |                ✓ support docs |                                 ✓ |
| tscc refactor              |            ✓ |              ✓ |     useful |               if hot path |                    usually no |           architecture if changed |
| website visual change      |          n/a |            n/a |        n/a | Lighthouse if significant |                          self |       design rule only if durable |
| regression-suite expansion | suite itself |            n/a |        n/a |                    rarely | testing page if claims change | TESTING.md if methodology changed |

This is guidance, not a rigid CI matrix.

---

# A NOTE ABOUT AI-DX

One of the broader conclusions from Nift development was that these tools can be unusually pleasant for coding agents because they favor:

```text
small conceptual surfaces
predictable commands
ordinary files
explicit build behavior
good errors
few hidden abstractions
```

We referred to this as **AI-DX** / AI developer experience.

We did not invent the general term, but it became useful vocabulary.

The handover infrastructure itself is now part of that AI-DX.

The objective is not to write documents specifically so an AI can blindly obey them.

It is to make the repositories legible enough that:

```text
human developer
or
coding agent
```

can quickly understand:

```text
what is true
why it is true
how to verify it
what not to assume
```

---

# WHAT I WOULD ESPECIALLY WANT CODEX TO PRESERVE

If I had to reduce this entire transfer to the highest-value institutional knowledge for each project, it would be:

### Nift website

> The website is documentation + product explanation + a real Nift integration fixture. Keep it synchronized with product truth and build it with candidate Nift.

### Nift regression suite

> It is an independent behavioral contract. Preserve cumulative failure-family coverage and deterministic black-box testing.

### Minify++

> Correctness comes before heroic compression. It is a focused standalone native minifier, and its Nift integration should not turn either project into the other's implementation detail.

### Minify++ website

> Explain the small tool honestly and directly. Don't turn simplicity into marketing bloat.

### tscc

> Compiler support is semantic, not syntactic. A parser accepting something does not mean tscc supports it.

### tscc regression suite

> Test observable compilation and runtime semantics independently, and use differential evidence where compatibility is intended.

### tscc website

> Never let feature/compatibility claims outrun the compiler evidence.

And across **all seven**:

> **A project handover is living infrastructure. A development checkpoint should leave both the code and the accumulated understanding in a better state.**

---

# WHAT I WOULD ASK CODEX TO REPORT BACK AFTER RECONCILIATION

After Codex reads this, I wouldn't ask it for another philosophical essay. I'd ask for a concrete repository audit along these lines:

```text
For each project:

1. Current repository/branch/status.
2. Current version or checkpoint identity.
3. Actual build command.
4. Actual test command(s).
5. Actual sanitizer/benchmark commands if present.
6. Canonical source and generated artifacts.
7. Relationship/synchronization with sibling repositories.
8. Files that appear to be temporary/investigative and need classification.
9. Existing docs that overlap the proposed handovers.
10. Proposed HANDOVER/document structure.
11. Historical handover statements that current source contradicts.
12. Important operational facts ChatGPT did not know.
13. Known current defects/deferred work discovered from the repository.
```

Then I would have Codex **show you that reconciliation before it starts turning all of this into permanent documentation**.

That is important because at this point Codex has something I no longer have: **direct access to the live current state of every repository**. I have much more of the historical/institutional context; Codex has much better current operational evidence. The ideal durable handovers should be the intersection of those two things:

```text
           my historical context
                  │
                  ▼
        ┌───────────────────┐
        │ durable handovers │
        └───────────────────┘
                  ▲
                  │
        Codex repository evidence
```

And once those are written, I think you've actually solved a fairly substantial problem we ran into repeatedly during our work: knowledge that previously existed only in a **very long sequence of conversations** will finally live beside the projects themselves.

That should make switching between me, Codex, another coding agent, or a human contributor dramatically less lossy—and because we've explicitly made **maintaining those handovers part of checkpoint completion**, this shouldn't just capture what we know *today*. It should keep capturing what the projects learn from here.
