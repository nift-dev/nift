# Project history and institutional context

> This is a living historical companion to `PROJECT-CONTEXT.md`. Repository evidence remains authoritative. Maintain, correct, reorganize, or supersede conclusions as the project evolves while retaining rationale that cannot be recovered from source alone.

This document contains the detailed institutional context transferred when Codex assumed development responsibility.

I’m deliberately writing this as **institutional memory rather than another development brief**. Where I know something is a settled decision, I say so. Where it is merely our current implementation, a strong preference, an architectural hypothesis, a rejected direction, or an unresolved question, I label it accordingly.

I would expect Codex to reconcile all implementation-specific claims against the current repositories before acting on them. The purpose here is to transfer the *why* behind Nift, including quite a lot that cannot be reconstructed from the current source tree.

---

# Nift Project Context Handover

## Complete historical, architectural, product, testing, performance, DX and decision context

### Purpose of this document

This is **not** a substitute for the repository.

For current implementation details, syntax, version strings, test counts, branch state, commands and behavior, the authority order should generally be:

```text
current source
    ↓
current tests / contract suite
    ↓
current documentation
    ↓
Git history / release history
    ↓
this handover
```

This handover exists because those sources cannot fully tell you:

* why Nift became what it is;
* which apparent omissions are deliberate;
* which strange-looking defensive code exists because of real bugs;
* which ideas were considered and rejected;
* which project claims have strong evidence versus architectural intuition;
* how our view of Nift changed through repeated testing;
* what terminology matters;
* what kinds of future changes would violate the current philosophy;
* where important questions remain genuinely unresolved.

Throughout this document I will use these classifications.

### Decision status vocabulary

**SETTLED DECISION**

Something we have deliberately chosen and should not casually revisit during unrelated work.

**CURRENT IMPLEMENTATION**

What Nift presently does. This can change, but changing it may alter the behavioral contract.

**STRONG PREFERENCE**

A design direction we believe strongly in but which is not necessarily an immutable contract.

**HYPOTHESIS / OPINION**

Something we currently believe based on architecture or evidence, but which further real-world use should be allowed to challenge.

**UNRESOLVED**

A question we deliberately have not settled.

**REJECTED DIRECTION**

Something considered and consciously moved away from.

**FUTURE POSSIBILITY**

Potential work that is interesting but is not an instruction to implement it now.

---

# Part I — What Nift is

## 1. Nift is a website generator

**Status: SETTLED TERMINOLOGY**

Please call Nift a:

> **website generator**

or, depending on context:

> website build system
> website generation/build layer

Do **not** habitually describe Nift as a “static site generator.”

This correction matters enough that I have caught the previous ChatGPT slipping back into the old terminology several times.

The reason is not semantic pedantry.

“Static site generator” tends to imply a particular product category:

```text
content
→ generator
→ static informational site
```

Nift's role is broader.

For example, Codex has now independently built a website where:

```text
Nift
├── owns the document
├── owns navigation
├── owns SEO/content
├── owns templates
├── owns asset paths
└── generates the website

React
├── owns a service-health simulator
└── owns an alert-policy builder
```

The resulting website contains runtime React applications.

Likewise a Nift-generated frontend can consume a Go/Python/Node API, use authentication, communicate with a database-backed backend, contain Web Components, or behave as an application.

The fact that Nift generates files does not mean the resulting website is “static” in the colloquial sense.

So:

```text
Nift = website generator
```

is intentional positioning.

---

# 2. The shortest useful mental model

**Status: STRONG PREFERENCE**

One useful model is:

```text
track
  ↓
compose
  ↓
transform
  ↓
generate
  ↓
rebuild exactly what became invalid
```

Another is:

```text
data + content + templates
            ↓
           Nift
            ↓
      website artifacts
```

Nift should remain a relatively thin generation/build layer around ordinary web technologies.

It should not try to own the entire application architecture.

---

# 3. “Glue, not universe”

**Status: SETTLED PHILOSOPHICAL DIRECTION**

One of the phrases we settled on for the website was:

> **Nift provides the glue without trying to become the universe.**

Related phrases included:

> **Keep your HTML. Keep your tools. Stop repeating yourself.**

and:

> **We don’t care how you build your website. Here’s a really fast templating/build layer. Carry on.**

Those are not merely marketing slogans.

They capture an architectural principle.

Nift should work well with:

```text
HTML
CSS
JavaScript
TypeScript
React
Vue
Svelte
Vite
Sass
npm
Go
Python
Node
REST APIs
databases
CDNs
deployment systems
image pipelines
documentation generators
```

without needing a built-in abstraction for each of them.

---

# Part II — How Nift got here

## 4. Older Nift was substantially broader

**Status: HISTORICAL FACT**

Earlier Nift contained or experimented with much more machinery, including things such as:

```text
LuaJIT
ExprTk
system execution
scripting capabilities
pre/post build hooks
broader programming/orchestration functionality
```

This made Nift more feature-rich in the conventional sense.

It also made the conceptual surface considerably larger.

---

# 5. The stripping-down phase was foundational

**Status: SETTLED ARCHITECTURAL DIRECTION**

A major turning point came when Nift was stripped back toward its core website-generation responsibilities.

We did **not** conclude:

> “Those old features were impossible to implement.”

We concluded something closer to:

> “Nift is better when it does not own those responsibilities.”

The stripped architecture initially centered heavily around a tiny set of operations:

```text
@content
@input(...)
@pathto(...)
$[...]
```

with a few other deliberate primitives such as:

```text
@dep(...)
@getenv(...)
@ent(...)
```

and compatibility functionality where appropriate.

The striking result was that Nift remained capable of building substantial websites even after losing a great deal of language machinery.

That materially changed our opinion of the project.

---

# 6. The “that’s it?” problem

**Status: PRODUCT/DX OBSERVATION**

Nift has an unusual communication problem.

Someone sees:

```text
@content
@input
@pathto
$[...]
```

and thinks:

> “That's all the template language does?”

Whereas after actually using it, the reaction can become:

> “Oh. That's the point.”

A lot of developer tools advertise power through the number of concepts they provide.

Nift increasingly demonstrates power by **not requiring Nift-specific concepts** for things that HTML/CSS/JS or another existing tool already does perfectly well.

This makes feature-list comparisons potentially misleading.

---

# 7. Why simplicity proved more powerful than expected

**Status: STRONG OPINION SUPPORTED BY EXPERIENCE**

We repeatedly found that complex websites often did not require a correspondingly complex Nift language.

The complexity could live naturally in:

```text
HTML structure
CSS
JavaScript
backend logic
API behavior
application state
external tooling
data
```

while Nift remained responsible for:

```text
composition
paths
dependencies
build-time data
generation
incremental correctness
```

CloudFort Dash was an important conceptual example because it demonstrated that a substantial application frontend does not necessarily require Nift itself to become an application framework.

---

# Part III — Modern Nift's language boundary

## 8. Modern Nift did regain some expressive power

**Status: CURRENT IMPLEMENTATION**

After the stripping phase, several capabilities proved useful enough to justify inclusion.

The current repository should be treated as authoritative for exact syntax, but modern Nift includes concepts such as:

```text
@json
JSON Schema validation
@for
object iteration
@if
sorting
$loop.* metadata
lexical bindings/scopes
```

The exact current loop syntax discovered by Codex is of the form:

```text
@for(item : site.items){
    ...
}
```

not the earlier conceptual syntax we sometimes discussed:

```text
@for(item in site.items)
```

There are richer current forms including object iteration and sorting.

Do not add aliases merely because older discussion used conceptual syntax.

---

# 9. JSON, loops and conditions do not reverse the simplification

**Status: SETTLED DESIGN PRINCIPLE**

This is extremely important.

The reasoning was:

```text
structured data
      +
static template
      ↓
static/generated output
```

is directly within Nift's purpose.

For example, without iteration:

```text
$[products[0].name]
$[products[1].name]
$[products[2].name]
```

quickly becomes ridiculous.

A controlled loop:

```text
@for(product : products){
    ...
}
```

is therefore not “general scripting.”

It is a static rendering primitive.

Likewise conditions can be legitimate because static rendering often needs conditional output.

---

# 10. The line we do not currently want to cross

**Status: STRONG PREFERENCE / PARTLY REJECTED DIRECTION**

The existence of loops should **not** automatically lead to:

```text
mutable variables
general assignment
arbitrary arithmetic
general Boolean/expression runtime
user-defined functions
arbitrary process execution
general scripting
package orchestration
```

The feared progression is:

```text
loop
→ variables
→ assignments
→ arithmetic
→ functions
→ mutable state
→ general-purpose template programming language
```

We deliberately do not want to stumble into that merely because each individual step appears convenient.

Every new language capability should have to justify itself against Nift's website-generation role.

---

# 11. Lexical scope is part of the language model

**Status: CURRENT CONTRACT**

Bindings should be thought of lexically.

For example:

```text
outer scope
    ↓
loop scope
    ↓
nested condition
    ↓
inner binding
    ↓
scope exits
    ↓
outer binding restored
```

This became important during later hardening.

Do not treat binding behavior as incidental parser state.

Nested loops, JSON bindings, object iteration, shadowing and `$loop.*` all need correct scope restoration.

---

# 12. `$loop.*` is contextual state

**Status: CURRENT IMPLEMENTATION/CONTRACT**

Nested loops should conceptually behave like:

```text
outer loop
    $loop.* = outer metadata

    inner loop
        $loop.* = inner metadata

    after inner
    $loop.* = outer metadata again
```

Potential bug families include:

```text
inner metadata leaking outward
outer metadata being destroyed
empty loop leaving stale metadata
wrong count/index after sorting
previous iteration metadata leaking
conditional branches disturbing loop context
```

This deserves ongoing adversarial coverage.

---

# 13. Stable sorting is semantically meaningful

**Status: CURRENT IMPLEMENTATION EXPECTATION**

Where Nift provides stable sorting, preserve stability deliberately.

Equal sort keys should not unpredictably reorder items.

Generated website output should remain deterministic.

---

# Part IV — Parameter values and the current `$[...]` design discussion

## 14. Operations versus values

**Status: STRONG DESIGN MODEL**

A useful conceptual distinction emerged:

```text
Nift operations:
    @input(...)
    @dep(...)
    @json(...)
    @pathto(...)

Nift values:
    $[...]
```

This matters for parameter resolution.

An operation may:

```text
read a file
modify parser state
record dependencies
emit output
introduce bindings
validate paths
```

A value should simply resolve to a value/text.

That gives a clean model:

```text
operation(value)
```

rather than:

```text
operation(operation(operation(...)))
```

---

# 15. Full Nift evaluation inside parameters was rejected

**Status: REJECTED DIRECTION**

We considered the idea that every directive parameter could simply be evaluated as a miniature Nift template.

For example:

```text
@input(@input('which-partial.txt'))
```

or:

```text
@pathto(@dep('foo.txt'))
```

This creates immediate semantic problems.

What is the value returned by `@dep`?

Does nested `@input` emit output or return text?

When does its dependency get recorded?

If the outer operation fails, what happens to side effects produced while evaluating its parameter?

This effectively forces Nift to define which operations are expressions, which return values, which emit output and how nested side effects compose.

That is the beginning of a general expression/composition language.

We rejected that direction.

---

# 16. Parameter interpolation using `$[...]` is the preferred model

**Status: STRONG PREFERENCE / TARGET OF CURRENT DEVELOPMENT**

The much cleaner model is for parameters to support a small **value template**:

```text
literal text
+
$[...] value expressions
```

Conceptually:

```text
'partials/$[page.layout].html'
```

becomes:

```text
Literal("partials/")
Value(page.layout)
Literal(".html")
```

This enables things like:

```text
@input($[page.partial])

@input('partials/$[page.layout].html')

@pathto('$[manifest.dir]/$[manifest.file]')

@dep('generated/$[release]/$[dataset].json')
```

without evaluating arbitrary Nift operations inside parameters.

This is better described as:

> parameter interpolation

or:

> string value resolution

than full template evaluation.

The **specific development-task handover for this change should follow this comprehensive context separately**, as requested.

---

# 17. Scalar/type behavior matters

**Status: DESIGN DIRECTION; VERIFY CURRENT TASK SPEC**

Built-in metadata values naturally produce textual values.

JSON-derived values require deliberate type rules.

We discussed a conservative model where path/string parameters accept textual/scalar values rather than silently serializing arrays/objects.

The exact current task specification should be verified separately before implementation.

Do not infer current required coercion rules solely from this historical discussion.

---

# Part V — Parser philosophy

## 18. Small lexical rules have repeatedly beaten permissive parsing

**Status: SETTLED DESIGN PREFERENCE**

One memorable bug involved something like:

```text
@content<
```

being consumed incorrectly because function-name recognition was too permissive.

The eventual rule became very simple:

> Nift function names are lowercase ASCII letters.

That is much better than maintaining an ever-growing list of characters that might terminate a function name.

General principle:

```text
small explicit grammar
>
permissive grammar + endless repair cases
```

---

# 19. Unknown web-language `@` syntax should survive

**Status: CURRENT BEHAVIOR / IMPORTANT COMPATIBILITY PRINCIPLE**

CSS constructs such as:

```css
@media
@supports
@keyframes
```

should not require escaping merely because Nift uses `@`.

Modern Nift was changed so unrecognized Nift functions pass through rather than consuming arbitrary CSS syntax.

This is important.

Nift should be **strict about syntax it has actually recognized**, while remaining friendly to ordinary non-Nift text.

---

# 20. Transparency is desirable

**Status: STRONG PREFERENCE**

An aspirational transparency contract we discussed was approximately:

> Valid HTML/CSS/JS containing no recognized Nift constructs should ideally pass through unchanged.

That captures the desired relationship with ordinary web languages.

Nift should not unnecessarily reinterpret the user's source.

---

# 21. Backticks are not another quote syntax

**Status: SETTLED/CURRENT BEHAVIOR**

Earlier behavior around backticks created unnecessary ambiguity.

Modern Nift uses single and double quotes for quoted parameters.

Do not casually reintroduce backticks as Nift quoting merely because JavaScript uses them.

---

# Part VI — Paths, names and dependencies

## 22. Tracked names and concrete paths are different concepts

**Status: IMPORTANT CURRENT MODEL**

Nift has a distinction between a tracked resource/name and a concrete filesystem path.

This has repeatedly been a source of both human and AI confusion.

For example, a Nift tracked page should generally be referenced by its Nift identity where that API expects a tracked name rather than manually spelling its generated output path.

Do not collapse these concepts for convenience.

---

# 23. `public/` is the modern output convention

**Status: SETTLED CURRENT CONVENTION**

The default scaffold/output directory was changed from the older `output/` convention to:

```text
public/
```

Documentation and examples should use the current convention unless discussing historical material.

---

# 24. `@pathto` is more than string concatenation

**Status: IMPORTANT SEMANTIC PRINCIPLE**

One reason `@pathto` matters is that it can validate relationships during generation.

The React/Vite experiment produced a particularly good demonstration.

Given:

```html
<script
  type="module"
  src="@pathto('public/assets/app.js')">
</script>
```

Nift will not happily certify a reference to a concrete asset that does not exist.

That means on a clean build:

```text
Vite creates app.js
        ↓
Nift validates reference
        ↓
HTML generated
```

This is useful strictness, not merely inconvenience.

---

# 25. Requirement versus content dependency

**Status: IMPORTANT CURRENT SEMANTIC MODEL**

The React experiment clarified this beautifully.

For a stable bundle:

```text
public/assets/app.js
```

the **contents** of the bundle do not affect the HTML bytes Nift emits.

Only the continued existence/path matters.

So:

```text
app.js modified
→ HTML still points to app.js
→ Nift page does not need rebuilding

app.js missing
→ reference no longer valid
→ Nift must care
```

Conceptually:

```text
content dependency:
    if contents change, my generated bytes may change

requirement:
    this resource must exist for my generated output to remain valid
```

That distinction is a major strength of Nift's incremental model.

---

# 26. Hashed asset filenames flip the relationship

**Status: HYPOTHESIS/TEST OPPORTUNITY**

With a Vite manifest:

```text
manifest.json
    ↓
assets/app-B7K2pQ.js
```

the filename itself becomes data affecting Nift's generated HTML.

Now:

```text
manifest changes
→ Nift output bytes may change
→ genuine content dependency
```

This is a useful future integration experiment because it exercises the distinction between requirement and dependency with a real external toolchain.

---

# 27. Nift should not become the external-tool orchestrator

**Status: STRONG PREFERENCE / HISTORICALLY REJECTED DIRECTION**

The React experiment can be orchestrated simply:

```text
Vite understands React
Nift understands website generation
npm understands project orchestration
```

For example:

```text
npm run build:react && npm run build:nift
```

That is preferable to teaching Nift:

```text
@vite(...)
@pipeline(...)
@prebuild(...)
```

or restoring arbitrary system hooks merely to run external tools.

Explicit project-level orchestration is acceptable.

---

# Part VII — Incremental builds

## 28. Incremental correctness is one of Nift's defining strengths

**Status: STRONG EVIDENCE / CORE PRODUCT PROPERTY**

One of the things that changed our view of Nift most strongly was how good its incremental model became under testing.

Nift does not merely generate pages quickly.

It tracks relationships sufficiently precisely that many rebuilds can be extremely small.

---

# 29. Missed rebuilds are worse than unnecessary rebuilds

**Status: SETTLED ENGINEERING PRINCIPLE**

Use this priority:

```text
unnecessary rebuild
    = performance problem

missed required rebuild
    = correctness problem
```

When uncertain, prefer conservative rebuilding.

Optimize the conservative case later once correctness is proven.

---

# 30. JSON is a dependency

**Status: CURRENT SEMANTIC REQUIREMENT**

`@json` is not merely a rendering feature.

If rendered output depends on loaded JSON, changes to that JSON must participate in incremental invalidation.

Important cases include:

```text
valid JSON → changed valid JSON
valid → malformed
malformed → valid
deleted JSON
recreated JSON
shared JSON used by many pages
JSON loaded through @input
JSON used inside loops
JSON used conditionally
schema changes
```

This is exactly where language semantics and build semantics meet.

---

# 31. Dependency sidecars/state have been hardened

**Status: HISTORICAL IMPLEMENTATION CONTEXT**

Later Nift hardening included fixes around dependency-sidecar invalidation and lifecycle.

Do not casually simplify dependency metadata without checking the regression history.

---

# 32. Sub-second mtimes are deliberate

**Status: CURRENT DEFENSIVE BEHAVIOR WITH HISTORICAL REPRODUCER**

Rapid edits can occur within the same second.

Nift was hardened to avoid losing relevant timestamp precision.

At one point an apparent same-second regression turned out to be a **test harness flake**, not a Nift failure.

The correct test was made deterministic by explicitly controlling sub-second mtimes.

Two lessons came from this:

```text
1. preserve sub-second correctness;
2. don't diagnose scheduler/filesystem luck as a product bug.
```

---

# 33. Hash collisions were demonstrated, not merely theorized

**Status: HISTORICAL BUG-FIX RATIONALE**

A narrower hash was shown to be unsafe by constructing a real collision reproducer.

The process was:

```text
suspect collision risk
        ↓
construct colliding inputs
        ↓
demonstrate wrong behavior
        ↓
strengthen hashing
        ↓
retain regression
```

This is an important example of the desired Nift engineering methodology.

---

# Part VIII — State and filesystem safety

## 34. Persisted state is untrusted input

**Status: SETTLED ENGINEERING PRINCIPLE**

Files generated by Nift may later be:

```text
manually edited
truncated
partially written
copied from another version
modified by another process
structurally valid JSON with wrong types
```

Therefore:

```text
“Nift wrote this file”
```

does **not** justify unsafe assumptions.

Validate:

```text
container type
key presence
value type
semantic range
cross-field relationships
```

before use.

---

# 35. Malformed state must fail normally

**Status: SETTLED EXPECTATION**

Historically, malformed JSON/state sometimes reached RapidJSON assumptions/assertions and could abort.

That was hardened.

The desired behavior is:

```text
malformed state
    ↓
useful diagnostic
    ↓
non-zero status
    ↓
no corruption
```

not process aborts or unchecked `GetString()`-style assumptions.

---

# 36. Mutations should validate before committing

**Status: STRONG ENGINEERING PRINCIPLE**

Commands such as:

```text
track
rm
cp
mv
```

have historically exposed bugs around multi-item behavior and partial mutation.

Preferred conceptual flow:

```text
parse requested operation
        ↓
validate all names/paths
        ↓
validate collisions
        ↓
validate sources/destinations
        ↓
prepare resulting state
        ↓
commit
```

A valuable hostile fixture is:

```text
first requested item valid
second requested item invalid
```

Then verify the first was not incorrectly committed.

---

# 37. Path containment must be semantic, not string-prefix based

**Status: SETTLED SAFETY PRINCIPLE**

Filesystem safety must account for:

```text
..
symlinks
normalization
canonical paths
missing path components
platform path semantics
similar string prefixes
```

Never replace robust containment logic with:

```text
target_string.starts_with(project_root_string)
```

merely because it looks simpler or faster.

---

# 38. Collision validation matters

**Status: CORE CORRECTNESS PROPERTY**

Nift needs to prevent problematic collisions among things such as:

```text
tracked names
content paths
derived outputs
generated artifacts
```

A later major performance/memory story came directly from making these checks scale.

---

# Part IX — Performance history

## 39. Performance is genuinely important to Nift

**Status: CORE PRODUCT VALUE**

Nift has long been extremely fast.

This is not merely marketing decoration.

Fast builds materially affect development behavior:

```text
edit
→ build
→ inspect
→ repeat
```

When feedback is effectively immediate, developers and agents can validate more aggressively.

---

# 40. Stripping Nift dramatically reduced size/complexity

**Status: HISTORICAL EVIDENCE**

An early stripped build was roughly in the vicinity of **840 KB**, compared with much larger older binaries around the order of **10 MB**.

Exact current executable size should be measured from the current candidate rather than copied from historical prose.

The important historical lesson is that removing large runtime dependencies materially simplified the product and build.

---

# 41. The 10,000-page experiments were influential

**Status: HISTORICAL EVIDENCE; NUMBERS MUST BE CONTEXTUALIZED**

We repeatedly used large synthetic projects, including 10,000-page fixtures.

Historical stripped-Nift measurements included full builds in the hundreds-of-milliseconds range and very cheap no-op incrementals.

Comparisons against tools such as Hugo and Astro were also performed.

Do **not** blindly reuse any old benchmark number without recording:

```text
Nift version
fixture
machine
dependency topology
build mode
warm/cold state
what exactly was timed
```

The shape matters more than one number:

```text
small startup cost
near-linear scaling
cheap no-op incrementals
targeted rebuild proportional to affected work
controlled memory
```

---

# 42. Fan noise was an amusing but meaningful observation

**Status: ANECDOTAL OBSERVATION**

During repeated Hugo/Astro experiments, the development machine's fans became noticeably active.

Nift's builds were sufficiently short that this generally did not happen.

This is not scientific benchmark evidence, but it reinforced the experiential point:

> fast build systems feel different during iteration.

Do not turn fan noise into a formal performance claim. 😄

---

# 43. The O(n²) collision-validation discovery was important

**Status: HISTORICAL ARCHITECTURAL CONTEXT**

A later scaling audit found collision validation that behaved quadratically at large project sizes.

The first major improvement used hash-indexed validation.

That solved the scaling problem but introduced another issue.

---

# 44. Hash tables fixed CPU scaling but increased memory

**Status: HISTORICAL ARCHITECTURAL CONTEXT**

Using multiple hash-based structures dramatically improved validation complexity but consumed substantial temporary memory on large projects.

That led to another round of measurement rather than declaring victory.

---

# 45. v1.0.40 → v1.0.42 represents an important optimization arc

**Status: HISTORICAL/CURRENT CONTEXT**

The progression was roughly:

```text
quadratic validation
        ↓
hash-indexed validation
        ↓
good speed, excessive temporary memory
        ↓
compact staged vector/sort validation
```

The later implementation deliberately reduced simultaneous heavyweight structures and used compact staged validation.

This is an example where:

```text
“unordered_set is asymptotically good”
```

was not the end of the engineering question.

Memory lifetime and representation mattered.

---

# 46. Lifetime reduction became a design principle

**Status: STRONG ENGINEERING PREFERENCE**

Prefer:

```text
read raw representation
        ↓
extract needed records
        ↓
release raw representation
        ↓
validate compact data
        ↓
derive one temporary comparison structure
        ↓
release/reuse
```

over keeping:

```text
raw JSON
+ DOM
+ domain objects
+ several hash sets
+ derived paths
```

alive simultaneously.

Before allocator cleverness, examine object lifetime.

---

# 47. Performance optimization may not weaken the contract

**Status: SETTLED PRINCIPLE**

Never improve a benchmark by skipping required work such as:

```text
collision validation
dependency checks
state validation
symlink/path safety
output generation
```

The benchmark must exercise production correctness.

---

# 48. Performance thresholds should not be brittle

**Status: STRONG TESTING PREFERENCE**

Absolute timings vary by machine.

Prefer scaling/relative guards where appropriate.

For example:

```text
10k workload becomes catastrophically superlinear relative to 2k
```

is a more robust regression signal than:

```text
must finish in exactly < 0.055 seconds
```

unless a deliberately broad threshold is being used.

---

# Part X — Testing history

## 49. Nift's testing culture changed dramatically

**Status: HISTORICAL FACT / CORE PROJECT VALUE**

At the start of this work, Nift had far less systematic adversarial coverage.

The regression suite then grew through repeated rounds.

Historical milestones included counts around:

```text
146
211
245
...
```

and later much larger combined/adversarial checkpoints.

Do not treat those historical counts as current authoritative totals.

The important thing is how the suite grew.

---

# 50. Tests were not merely written from documentation

**Status: CORE METHODOLOGY**

The most valuable testing pattern became:

```text
get suite green
      ↓
read implementation anyway
      ↓
find assumption not covered
      ↓
construct hostile input
      ↓
prove whether it fails
      ↓
retain reproducer
      ↓
fix root cause
      ↓
run everything again
```

This is probably the single most important development habit to preserve.

---

# 51. Bug-family thinking matters more than raw test count

**Status: SETTLED TESTING PHILOSOPHY**

The question is not:

> How many tests do we have?

It is:

> How many distinct assumptions have we attacked?

Useful families include:

```text
lexical boundaries
grammar boundaries
scope
persistent-state corruption
filesystem containment
collision behavior
incremental invalidation
concurrency
resource scaling
transactionality
cross-component interactions
```

Ten tests exposing ten different structural assumptions may be more valuable than thousands of trivial variants.

---

# 52. Every important bug should leave a regression

**Status: SETTLED PRACTICE**

The suite should become institutional memory.

If a future developer wonders why some defensive code exists, ideally a regression should demonstrate the behavior it protects.

---

# 53. Deterministic fixtures beat timing luck

**Status: SETTLED TESTING PRACTICE**

The same-second mtime episode taught us not to trust incidental timing.

Where possible, control:

```text
timestamps
file contents
ordering
random seeds
environment
```

so a test proves the intended invariant.

---

# 54. Failure semantics are part of the contract

**Status: CURRENT CONTRACT PRINCIPLE**

Compatibility is not merely:

```text
generated HTML bytes
```

It includes things like:

```text
exit status
whether state changed
whether old outputs survived
whether unrelated files survived
whether incremental rebuild happened
whether malformed configuration was rejected
```

A command printing an error but returning zero is a real bug.

Several historical Nift fixes involved status propagation.

---

# 55. C++ implementation tests and black-box contract tests serve different purposes

**Status: SETTLED TEST ARCHITECTURE**

Current architecture separates:

```text
Nift repository
    → implementation-sensitive/focused C++ tests

nift-regression-suite
    → implementation-independent executable contract
```

This separation is extremely valuable.

The external suite should be able to test an arbitrary executable claiming to implement Nift behavior.

It should not include private C++ classes or depend on internal object structure.

---

# 56. Why the external contract matters

**Status: STRONG ARCHITECTURAL VALUE**

Conceptually:

```text
Nift behavioral contract
        │
        ├── current C++ Nift
        │
        └── hypothetical future implementation
```

A future Rust implementation has been discussed.

It is **not current work**.

But an implementation-independent suite means another implementation could someday be tested against the same behavior.

---

# 57. Contract-suite identity may eventually become independent of implementation version

**Status: FUTURE POSSIBILITY**

Longer term, something conceptually like:

```text
Nift v4 behavioral contract — checkpoint X
```

could be more useful than thinking of the suite merely as:

```text
tests for C++ Nift 1.0.42
```

This has not yet been formalized.

---

# 58. Unit tests can grow opportunistically

**Status: FUTURE/ONGOING PREFERENCE**

A broader C++ test layer would be useful around:

```text
paths
JSON
schema
tracking
dependencies
scope
hashing
parser helpers
```

But there is no need to stop all other work and create an enormous unit-testing framework.

Add focused tests where they provide substantially better localization.

---

# 59. Sanitizers remain important

**Status: STRONG VALIDATION PREFERENCE**

ASan/UBSan and, where meaningful, TSan should be used around relevant changes.

Do not claim historical sanitizer coverage that the current repository cannot substantiate.

Run the current code and establish fresh evidence.

---

# 60. Fuzzing is a future high-value layer

**Status: FUTURE POSSIBILITY**

Ideal fuzzing workflow:

```text
fuzzer finds issue
      ↓
minimize
      ↓
understand
      ↓
deterministic regression
      ↓
fix
```

The fuzzer is discovery infrastructure.

The permanent regression suite remembers the bug.

---

# Part XI — Important historical bug families

## 61. JSON/state crashes

Malformed but parseable state could previously reach unsafe assumptions.

This led to stricter structural/type validation and controlled failures.

---

# 62. Empty tracking state

Zero tracked files once exposed invalid `tracked.json` writing behavior.

That became a regression.

---

# 63. JSON escaping

Titles containing quotes exposed JSON serialization problems.

Serialization was hardened rather than relying on ad hoc stream manipulation.

---

# 64. Watch initialization

A first build after watch setup exposed inconsistent creation of watch-related JSON state, including missing per-directory tracking state.

The initialization path was made more consistent.

---

# 65. Multi-name command bugs

Operations such as `rm` exposed indexing/multi-item edge cases.

Multi-target CLI behavior deserves deliberate testing.

---

# 66. Path traversal

`track`, `cp`, `mv` and related filesystem operations were attacked with traversal cases.

Containment checks became stronger.

---

# 67. Deleted outputs

Incremental builds needed to recognize that a generated output disappearing means the project is no longer up-to-date.

This is a good example of “source unchanged” not necessarily meaning “nothing to do.”

---

# 68. Directory dependencies

Directory `@dep` behavior exposed unstable/repeated rebuild semantics and forced clearer treatment.

Do not assume all filesystem entities can be hashed/stat'ed identically without semantic decisions.

---

# 69. Hash-mode cache refresh

Repeated edits exposed cache-refresh problems in hash-based incremental modes.

Incremental correctness needs repeated-edit tests, not only one transition.

---

# 70. CLI arity and option validation

Several commands historically accepted malformed or unknown argument forms too generously.

CLI parsing became stricter.

---

# 71. Build-status propagation

Cases existed where requested builds failed semantically but process status still implied success.

Exit status was hardened.

---

# 72. Makefile header dependencies

Incremental compilation itself needed correctness work.

A developer should not have to run `make clean` for header changes to be respected.

---

# Part XII — Minify++

## 73. Historical naming

**Status: SETTLED CURRENT IDENTITY**

The minifier was historically called **Sift** during development.

It is now **Minify++**.

Current identity:

```text
Display/project: Minify++
Executable:      minify
Repository/path: minifypp where needed
C++ namespace:   minify
Public header:   <minify/Minify.h>
```

Use “Sift” only where historical accuracy requires it.

---

# 74. Minify++ is an independent project

**Status: SETTLED ARCHITECTURE**

Conceptually:

```text
Minify++
    → standalone minification tool/library

Nift
    → embeds/uses Minify++ optionally
```

Minify++ should not be conceptually swallowed by Nift.

---

# 75. Standalone Minify++ is canonical

**Status: OPERATIONAL DIRECTION**

The standalone Minify++ implementation should be treated as canonical.

Nift's embedded `minifypp` copy is synchronized from it.

Byte-for-byte synchronization of corresponding implementation files is currently an important invariant.

A deterministic sync/check mechanism is desirable.

Do not introduce a Git submodule or complicated package architecture merely for elegance.

---

# 76. Nift minification is opt-in

**Status: SETTLED PRODUCT BEHAVIOR**

Nift does **not** automatically minify every `.html`, `.css` or `.js` file.

Users explicitly configure which extensions should be minified.

This is important because semantic transformation carries risk.

Ordinary Nift builds should not be silently exposed to that risk.

---

# 77. Minification belongs at the final-output boundary

**Status: STRONG ARCHITECTURAL PREFERENCE**

Conceptually:

```text
Nift renders artifact
        ↓
optional Minify++
        ↓
final output
```

Minify++ should not infect Nift's template semantics.

---

# 78. Minify++ should remain conservative

**Status: SETTLED PROJECT PHILOSOPHY**

Priority approximately:

```text
semantic preservation
        ↓
valid output
        ↓
robust failure
        ↓
performance
        ↓
compression ratio
```

Do not save three bytes by introducing a transformation whose safety becomes extremely difficult to prove.

---

# 79. Minify++ should not become a bundler/compiler

**Status: REJECTED DIRECTION**

Do not casually add:

```text
bundling
tree shaking
module resolution
transpilation
TypeScript compilation
```

Minify++'s narrowness is a strength.

---

# Part XIII — Nift + React/Vite: a real composability experiment

## 80. Codex independently built the experiment

**Status: RECENT EMPIRICAL EVIDENCE**

After the development handover, Codex was asked to create a Nift website with React islands directly on the user's machine.

It independently chose a very good architectural split:

> Nift owns the document, navigation, SEO copy, product narrative, docs, and asset paths; React owns two stateful surfaces—a live service-health simulator and an interactive alert-policy builder.

This is important because Codex did not merely follow a canned Nift/React tutorial.

---

# 81. The architecture required zero React functionality in Nift

**Status: STRONG EMPIRICAL SUPPORT FOR COMPOSABILITY**

The project used ordinary composition:

```text
Nift
+
Vite
+
React
```

No Nift features such as:

```text
@react
@island
@hydrate
```

were necessary.

That strongly supports the “glue, not universe” philosophy.

---

# 82. React earned its place

**Status: DESIGN OBSERVATION**

React was used for genuinely stateful interfaces.

Nift retained ownership of ordinary document-oriented content.

This is a better islands demonstration than using React for decorative text or static cards.

---

# 83. The build split was intentionally boring

The example effectively had:

```text
React source
    ↓
Vite
    ↓
public/assets/app.js

Nift content/templates
    ↓
Nift
    ↓
public HTML/CSS/etc.
```

Vite was configured not to wipe Nift's output directory.

A combined build could simply be:

```text
React → Nift
```

through normal project scripts.

---

# 84. Independent watchers are not necessarily bad DX

**Status: UPDATED OPINION AFTER INVESTIGATION**

Initially Codex counted multiple processes/build order as integration friction.

After examining the semantics, it revised that assessment.

Once the stable bundle exists:

```text
React edit
→ Vite updates app.js
→ Nift does nothing

Nift content edit
→ Nift rebuilds relevant pages
→ Vite does nothing
```

The two incremental domains are genuinely independent.

One wrapper command can provide a one-command developer experience if desired.

---

# 85. This experiment improved our confidence in Nift's composition model

**Status: HYPOTHESIS STRENGTHENED BY EVIDENCE**

The interesting conclusion is not:

> Nift has React support.

It is:

> Nift may not need React support.

The same general architecture should naturally allow:

```text
Nift + React
Nift + Vue
Nift + Svelte
Nift + Preact
Nift + Solid
Nift + Web Components
Nift + vanilla JS
```

without changing Nift.

---

# Part XIV — Where Nift may be a strong choice

## 86. Bespoke document-oriented websites

**Status: STRONG OPINION WITH GROWING EVIDENCE**

Nift appears particularly attractive when the website is fundamentally composed of documents/pages but may contain interactive behavior.

Examples:

```text
company sites
product sites
marketing sites
portfolios
documentation
custom content sites
frontend shells around backend applications
selectively interactive sites
```

---

# 87. Existing HTML/CSS/JS migrations

**Status: STRONG HYPOTHESIS; NEEDS MORE EMPIRICAL TESTING**

I am particularly bullish here.

An existing site can often conceptually move toward Nift by:

```text
keep HTML
keep CSS
keep JavaScript
keep asset layout largely intact
extract repeated HTML into templates/partials
track pages
use Nift paths/dependencies
```

rather than converting the entire site into a component framework.

This deserves a realistic migration experiment.

---

# 88. Backend-driven applications

**Status: CONDITIONAL STRONG FIT**

The backend language itself does not determine whether Nift is appropriate.

The stronger pattern is:

```text
backend owns:
    authentication
    database
    business logic
    API
    runtime state

Nift owns:
    website documents
    templates
    navigation
    build-time data
    help/docs/content
    selective frontend composition
```

This can be a very clean architecture.

---

# 89. Selective islands

**Status: EMPIRICALLY VALIDATED AT SMALL SCALE**

Nift appears very attractive when:

```text
most of website = document-oriented
some surfaces = genuinely stateful
```

and React/Vue/etc. can own only those surfaces.

The small React/Vite experiment worked without Nift modification.

Larger projects should still test whether this remains clean at scale.

---

# 90. Large sites where Nift's composition model fits

**Status: STRONG BUT QUALIFIED**

Page count alone is not a reason to avoid Nift.

Nift has demonstrated excellent scaling.

However:

```text
large number of pages
```

and:

```text
rich publication semantics
```

are different problems.

Do not confuse them.

---

# Part XV — Where another tool may be better

## 91. State-heavy client applications

**Status: STRONG TOOL-CHOICE BOUNDARY**

If almost the entire frontend is one authenticated/stateful client application, something like Vite + React/Vue/Svelte may be sufficient.

If React owns the rendering/runtime architecture itself, a React-first framework may be the more natural abstraction.

Nift should be doing meaningful work if it is present.

---

# 92. Rich publication systems

**Status: IMPORTANT QUALIFICATION**

A website requiring deeply integrated:

```text
taxonomies
pagination
automatic archives
related-content queries
multilingual publication
feeds
editorial conventions
Markdown pipelines
content discovery
theme ecosystems
```

may be better served by Hugo, Astro or a specialized documentation/publication system.

Nift's raw speed does not make those semantics disappear.

---

# 93. Documentation construction versus documentation product

**Status: USEFUL PRODUCT DISTINCTION**

Nift is closer to a:

> documentation construction tool

than an all-in-one:

> documentation product

A dedicated docs system may provide many conventions automatically.

Nift gives you a smaller explicit layer from which to construct exactly the site you want.

Both can be good depending on the project.

---

# 94. External tools are not an excuse for bad boundaries

**Status: STRONG PHILOSOPHICAL QUALIFICATION**

We should not say:

> “Nift doesn't need feature X because some external tool somewhere can technically do it.”

The better question is:

> Does external composition remain cleaner than putting this capability in Nift?

If users repeatedly need an awkward six-stage pipeline for something fundamental, that may indicate the boundary is wrong.

Minimalism should be challenged by real use.

---

# Part XVI — AI developer experience

## 95. AI DX emerged as an important concept

**Status: STRONG OPINION / PARTLY EMPIRICALLY SUPPORTED**

We began informally discussing **AI DX**: the equivalent of developer experience for coding agents.

Nift appears unusually promising here because of:

```text
small conceptual surface
ordinary files
normal web technologies
clear CLI
fast builds
deterministic behavior
explicit dependencies
few hidden conventions
good error opportunities
small codebase
strong black-box tests
```

---

# 96. AI DX does not mean adding “AI features”

**Status: SETTLED PREFERENCE**

Do not add gimmicky AI functionality merely to make Nift “AI friendly.”

Excellent ordinary DX often produces excellent AI DX automatically.

---

# 97. Codex using Nift is itself evidence

**Status: RECENT EMPIRICAL EVIDENCE**

Codex was able to:

```text
inspect Nift
initialize projects
create a working website
create a React-islands website
run Nift directly
reason about requirement/dependency semantics
```

on the user's machine.

This is stronger evidence than merely claiming the syntax looks easy for AI.

---

# 98. Strictness may improve AI DX

**Status: EMERGING HYPOTHESIS**

An interesting lesson from `@pathto` is that an agent cannot as easily hallucinate an internal asset path and still get a green build.

Instead:

```text
agent assumes path exists
        ↓
Nift rejects it
        ↓
agent corrects assumption
```

Fast, precise rejection can be better AI DX than permissiveness.

An agent does not necessarily need its first guess accepted.

It needs cheap recovery.

---

# 99. AI friction should be recorded, not hidden

**Status: FUTURE EVALUATION METHOD**

Useful things to log during Codex use include:

```text
guessed wrong syntax
misunderstood tracking
could not infer command
misunderstood generated path
needed unusual orchestration
received poor diagnostic
received excellent diagnostic
```

Then classify the remedy:

```text
better docs
better diagnostic
better example
better project script
actual Nift change
reasonable learning cost
```

This is much better evidence for AI DX than giving Nift an arbitrary 10/10.

---

# 100. Do not force Codex to agree with ChatGPT's Nift enthusiasm

**Status: SETTLED EVALUATION PRINCIPLE**

Codex should form its own view.

The previous ChatGPT has more longitudinal history and therefore stronger priors.

Codex has less history but can now directly use the repository and executable.

Disagreement is useful.

If Codex eventually concludes:

```text
Nift is excellent, but in a narrower domain
```

that is valuable.

If it becomes more bullish after real use, that is valuable too.

---

# Part XVII — Human DX

## 101. Explicit composition has costs

**Status: ACKNOWLEDGED TRADEOFF**

Nift + external tools may involve:

```text
multiple processes
build ordering
generated versus authored files
separate watchers
project-level scripts
```

These costs should not be denied.

---

# 102. Classify friction before changing Nift

**Status: STRONG DECISION FRAMEWORK**

Use:

```text
A. inherent consequence of explicit composition

B. documentation/tooling deficiency

C. actual missing Nift primitive
```

Do not jump from inconvenience directly to adding a language feature.

---

# 103. One-command DX need not mean one-tool architecture

**Status: STRONG PREFERENCE**

A project can expose:

```text
npm run dev
```

while internally running:

```text
Nift watcher
Vite watcher
local server
```

That can still be excellent DX.

A single process is not automatically simpler than several well-separated processes.

---

# Part XVIII — Website and documentation

## 104. The Nift website was substantially redesigned

**Status: HISTORICAL/CURRENT PRODUCT CONTEXT**

The modern website was redesigned around the stripped/modern Nift identity.

Major themes included:

```text
templating/build core
small conceptual surface
speed
incremental generation
ordinary web technologies
AI-assisted development
composability
```

rather than prominently advertising removed scripting machinery.

---

# 105. Design direction

**Status: USER PREFERENCE / CURRENT SITE HISTORY**

The site evolved toward a clean design with:

```text
dark mode preference
green gradient identity
theme switching
clean cards/layout
minimal visual clutter
strong responsive behavior
```

Desktop Lighthouse eventually reached excellent scores, including a reported 100/100/100/100 run, with mobile subsequently improved.

Exact current scores should be rerun rather than treated as permanent facts.

---

# 106. Documentation was reorganized around modern Nift

Important documentation areas included:

```text
Getting Started
Template Language
Commands
Configuration
Workflows/Patterns
Full Web Applications
Deployment/Hosting
Migration
AI-assisted development
Why Nift
examples/templates
```

The documentation should describe current recommended behavior, not every historical capability equally.

---

# 107. Supported versus recommended

**Status: IMPORTANT DOCUMENTATION PRINCIPLE**

A compatibility feature can remain supported without receiving equal prominence.

Do not delete something merely because the current website does not emphasize it.

Likewise, do not document every legacy mechanism as though it were the preferred modern workflow.

---

# 108. The AI-opinion/comparison material should remain credible

**Status: STRONG PREFERENCE**

The site contains or has contained AI-oriented comparison material.

Do not turn this into:

> Nift wins every category.

A more credible assessment can say:

```text
excellent here
competitive here
conditional here
other tool better here
not enough evidence here
```

If evidence causes ratings to fall, that is acceptable.

---

# 109. Battle-tested messaging should emphasize methodology

**Status: STRONG MARKETING PREFERENCE**

Better:

```text
adversarial parser cases
persistent-state corruption
incremental invalidation
filesystem/path attacks
scaling
sanitizers
semantic tests
```

than:

```text
WE HAVE 20,000 TESTS
```

Test counts can support a claim.

They should not *be* the quality claim.

---

# Part XIX — Repository, branches and publication

## 110. Repository remains operational authority

**Status: SETTLED HANDOVER PRINCIPLE**

This document is institutional memory.

If it says something about a current path or command that conflicts with the checked-out current repository, investigate rather than blindly following this document.

---

# 111. Nift website branch arrangement

**Status: CURRENT REPOSITORY FACT AS PREVIOUSLY RECONCILED; VERIFY BEFORE ACTION**

Codex previously identified the Nift website arrangement approximately as:

```text
stage
→ source website

public
→ checked-out built-site state

main
→ generated deployment branch
```

This should be verified against current Git state before publishing.

Do not casually redesign it merely for consistency with other project sites.

---

# 112. Website version and executable version are not necessarily identical concepts

**Status: OPERATIONAL PRINCIPLE**

A website content update does not automatically imply a Nift executable release.

Likewise adding tests does not necessarily require a Nift version bump.

---

# 113. Historical development used ZIP checkpoints heavily

**Status: HISTORICAL FACT**

Much of the previous ChatGPT workflow was:

```text
repository/ZIP
      ↓
inspect/modify
      ↓
compile/test
      ↓
package canonical ZIP
      ↓
user receives checkpoint
```

Therefore some modern GitHub release conventions were **never formally established**.

Do not mistake the old artifact workflow for a settled future release policy.

---

# 114. Public release policy remains partly unformalized

**Status: UNRESOLVED**

Things such as exact:

```text
tag naming
release branches
binary artifact matrix
GitHub release process
```

should be recovered from current Git history where possible and explicitly defined before the next public release if still ambiguous.

Do not invent history.

---

# 115. Public/irreversible actions require explicit approval

**Status: SETTLED WORKING RELATIONSHIP**

Codex can autonomously:

```text
inspect
build
test
create reproducers
fix defects
refactor where justified
benchmark
run sanitizers
prepare docs
prepare candidate releases
```

But actions such as:

```text
pushing release tags
publishing releases
deploying websites
changing public release version
large destructive repository restructuring
intentional behavioral-contract changes
deleting uncertain historical evidence
```

should receive explicit approval.

---

# Part XX — Release philosophy

## 116. Nift is the most mature of the three related projects

**Status: CURRENT MATURITY ASSESSMENT**

Broadly:

```text
Nift
    closest to release/stability focus

Minify++
    strong but still semantic-confidence building

tscc
    active longer-horizon compiler development
```

For Nift, bias toward release blockers and correctness rather than inventing features.

---

# 117. Version bumps should correspond to meaningful project changes

**Status: STRONG PREFERENCE; EXACT POLICY UNRESOLVED**

Test additions alone need not bump Nift.

Documentation corrections alone need not bump Nift.

A shipped behavioral change/correctness fix/new language capability may justify one.

The exact versioning policy should be reconciled with current Git history.

---

# 118. Packaging must itself be tested

**Status: STRONG RELEASE PRACTICE**

A green repository does not prove a release archive works.

Preferred validation:

```text
create package
      ↓
extract fresh
      ↓
build/use package
      ↓
run external tests
      ↓
verify --version/help
      ↓
inspect expected docs/license/files
```

---

# 119. Build the Nift website with the candidate Nift binary

**Status: STRONG RELEASE PRACTICE**

This has been a useful real-world integration check.

The Nift website is a genuine Nift project.

A candidate that cannot correctly build its own website is interesting evidence.

This does not replace the hostile contract suite.

Use both.

---

# Part XXI — Cross-project philosophy

## 120. Nift, Minify++ and tscc share values, not necessarily infrastructure

**Status: STRONG PREFERENCE**

Shared values:

```text
small
fast
native
predictable
well tested
low dependency
clear CLI
AI-friendly
```

Do **not** force this into:

```text
shared mega-library
shared runtime
shared base classes
mandatory integrated product suite
```

Some duplication can be healthier than unnecessary coupling.

---

# 121. tscc should not automatically become Nift infrastructure

**Status: STRONG PREFERENCE**

Even if tscc becomes excellent, Nift should not embed it merely because both projects exist.

Use normal external tooling unless integration has a concrete architectural benefit.

---

# 122. Minify++ is different because its integration is narrow and natural

Minification is a final-artifact transformation.

Embedding the Minify++ library can preserve a self-contained Nift executable without making Minify++ conceptually part of Nift's language/runtime architecture.

That boundary is much cleaner.

---

# Part XXII — Things we do not currently want

## 123. General scripting restored

**Status: REJECTED DIRECTION**

Do not casually restore:

```text
Lua
general expression runtimes
system execution
arbitrary hooks
mutable template scripting
```

because old Nift once had them.

Their absence is part of modern Nift's design.

---

# 124. Framework-specific frontend integrations

**Status: REJECTED/UNNECESSARY DIRECTION**

Do not add:

```text
@react
@vue
@svelte
@vite
```

merely to advertise integrations.

The React experiment suggests ordinary composition is better.

---

# 125. Nift as package manager/build orchestrator

**Status: REJECTED DIRECTION**

Do not recreate npm/Make/CMake/task runners inside Nift.

---

# 126. Feature accumulation to win comparison tables

**Status: REJECTED PRODUCT STRATEGY**

If Hugo has six publication features Nift lacks, that does not mean Nift should add all six.

Ask whether each feature belongs in Nift's core model.

A clearly bounded 8/10 can be a better product than an incoherent attempt at 10/10 everywhere.

---

# 127. Giant internal parser/compiler machinery without evidence

**Status: STRONG PREFERENCE**

Modern Nift's language remains small enough that a hand-written explicit parser is appropriate.

Do not introduce a large parser framework merely because loops and conditions now exist.

Reconsider only if actual grammar complexity demands it.

---

# Part XXIII — Things that remain unresolved

## 128. How much more expressive should Nift become?

**Status: UNRESOLVED**

The current rule is:

> Add rendering power only when real website-generation use cases justify the complexity.

We have **not** declared the current language permanently complete.

But neither is richer programming machinery presumed desirable.

---

# 129. Parameter value resolution

**Status: ACTIVE DEVELOPMENT QUESTION**

The current `$[...]` parameter-resolution work is precisely one of these boundary questions.

The broad design direction is:

```text
literal text
+
$[...] values
```

rather than arbitrary nested Nift evaluation.

A separate targeted development handover should specify exact behavior.

---

# 130. Contract-suite versioning

**Status: FUTURE/UNRESOLVED**

It may eventually make sense for the Nift behavioral contract to have its own checkpoint/version independent of C++ implementation releases.

Not currently required.

---

# 131. Rust implementation

**Status: FUTURE POSSIBILITY, NOT ACTIVE PLAN**

We discussed eventually implementing the same Nift behavioral specification in Rust as an interesting architectural comparison.

Possible benefits include stronger type/ownership guarantees around paths, state and errors.

But the correct sequence would be:

```text
stable behavioral contract
      ↓
current C++ implementation remains authoritative
      ↓
optional independent Rust implementation
      ↓
same external contract suite
```

Do not begin a Rust rewrite as ordinary Nift maintenance.

---

# 132. Broader cross-platform validation

**Status: IMPORTANT VALIDATION GAP / VERIFY CURRENT CI**

Recent development has been heavily Linux-centric.

Nift is intended to be cross-platform.

Current Windows/macOS coverage should be established from repository/CI evidence rather than assumed.

---

# 133. Fresh-install/package DX

**Status: FUTURE HIGH-VALUE TEST**

Codex has so far benefited from a prepared development machine.

A fresh environment test would reveal:

```text
undocumented dependencies
install assumptions
PATH issues
compiler assumptions
project initialization friction
```

This is valuable before broad release.

---

# Part XXIV — Evidence versus belief

## 134. Strongly demonstrated

At this point there is substantial evidence that Nift can provide:

```text
very fast website generation
very cheap incremental builds
large-project scaling
small conceptual surface
dependency-aware rebuilding
ordinary HTML/CSS/JS composition
robustness under extensive adversarial testing
React/Vite islands without framework-specific Nift support
```

Exact quantitative claims should still be tied to reproducible benchmarks.

---

# 135. Strong architectural priors, but worth further testing

These include:

```text
Nift is unusually good for existing-site migration
Nift is unusually good for AI-generated bespoke websites
Nift remains clean in larger islands architectures
Nift composes cleanly with multiple external build tools
Nift remains pleasant for substantial JSON-driven sites
```

We have reasons to believe these.

We should still build realistic projects.

---

# 136. Claims that should remain conditional

Examples:

```text
best documentation generator
best choice for every large site
best choice for every full-stack frontend
10/10 AI DX universally
best React architecture
```

Those are too broad.

Project semantics matter.

---

# Part XXV — Recommended evidence-building projects

## 137. JSON/Schema-driven catalogue

This is probably the highest-information next Nift-native experiment.

Use realistically:

```text
multiple JSON datasets
JSON Schema
nested loops
object iteration
conditions
sorting
$loop.*
shared templates
shared data
multiple page types
```

Then change the requirements after it works.

Measure both maintainability and incremental behavior.

---

# 138. Large dependency-topology project

Not merely 10,000 identical pages.

Use:

```text
page-specific dependencies
shared partials
shared JSON
global layouts
schemas
independent assets
```

Then modify different layers and observe exactly what rebuilds.

This tests Nift's real architectural strength better than a flat benchmark.

---

# 139. Existing-site migration

Take a realistic ordinary HTML/CSS/JS site and migrate it without redesigning it around Nift.

Measure how much Nift-specific structure is actually required.

---

# 140. Larger React/Vite islands project

Use:

```text
multiple Nift pages
shared islands
page-specific islands
shared React code
lazy chunks
production asset strategy
build-time Nift data
```

Then maintain it.

The maintenance phase matters more than the first generated version.

---

# 141. Backend-served application frontend

Use a real backend/API with Nift-generated documents and selective runtime behavior.

This will test one of Nift's most interesting potential niches.

---

# 142. Failure exercise

Deliberately introduce:

```text
malformed JSON
schema failure
missing requirement
Minify++ failure
output collision
broken external bundle
```

Inspect:

```text
diagnostics
exit status
transactionality
old output preservation
recovery
```

---

# 143. Maintenance experiment

Return to a “finished” project later and change:

```text
content model
navigation
schema
island architecture
output layout
toolchain
page count
```

Initial generation favors AI tools.

Maintenance reveals whether the architecture is actually comprehensible.

---

# Part XXVI — Decision ledger

This is the compact part Codex should refer back to when historical discussion is ambiguous.

| Decision/question                                | Status                                | Current position                                          |
| ------------------------------------------------ | ------------------------------------- | --------------------------------------------------------- |
| Call Nift a website generator                    | **SETTLED**                           | Do not habitually call it an SSG                          |
| Keep ordinary web technologies first-class       | **SETTLED**                           | HTML/CSS/JS remain themselves                             |
| Nift should be glue, not universe                | **SETTLED**                           | Avoid owning unrelated toolchain responsibilities         |
| LuaJIT/ExprTk/general scripting direction        | **REJECTED**                          | Do not restore casually                                   |
| `@json`                                          | **CURRENT / ACCEPTED**                | Structured build-time data belongs in Nift                |
| JSON Schema                                      | **CURRENT / ACCEPTED**                | Useful validation layer                                   |
| Loops                                            | **CURRENT / ACCEPTED**                | Static iteration fits Nift                                |
| Conditions                                       | **CURRENT / ACCEPTED**                | Static conditional rendering fits Nift                    |
| Lexical scope                                    | **CONTRACTUAL**                       | Preserve across nested control flow                       |
| Stable sorting                                   | **CURRENT SEMANTIC EXPECTATION**      | Equal keys should remain deterministic                    |
| `$loop.*`                                        | **CURRENT**                           | Lexically contextual loop metadata                        |
| General mutable template scripting               | **NOT WANTED**                        | Would require strong new evidence                         |
| Full Nift evaluation inside directive parameters | **REJECTED**                          | Side-effect/composition semantics become too complex      |
| `$[...]` parameter interpolation                 | **ACTIVE/PREFERRED**                  | Literal + value resolution is the clean direction         |
| Lowercase function-name recognition              | **CURRENT/DELIBERATE**                | Small lexical invariant                                   |
| Unknown CSS-style `@...` pass-through            | **CURRENT/DELIBERATE**                | Nift should coexist with normal CSS                       |
| Backtick as Nift quote                           | **REJECTED**                          | Single/double quotes only                                 |
| `public/` output convention                      | **SETTLED CURRENT CONVENTION**        | Use in modern docs/examples                               |
| `@pathto` validation                             | **CORE SEMANTIC VALUE**               | Catch broken internal references                          |
| Requirement vs content dependency                | **CORE MODEL**                        | Preserve distinction                                      |
| Missing rebuild vs extra rebuild                 | **SETTLED PRIORITY**                  | Correctness first                                         |
| Persisted state trusted because Nift wrote it    | **REJECTED ASSUMPTION**               | Always validate                                           |
| Validate before mutation                         | **STRONG PRINCIPLE**                  | Prefer transactional behavior                             |
| String-prefix path containment                   | **REJECTED**                          | Use semantic filesystem containment                       |
| Performance as product value                     | **SETTLED**                           | But never weaken correctness for benchmarks               |
| v1.0.42 compact validation architecture          | **CURRENT RATIONALE**                 | Do not replace casually with memory-heavy hash structures |
| External black-box contract suite                | **SETTLED ARCHITECTURE**              | Keep implementation-independent                           |
| Rust Nift                                        | **FUTURE POSSIBILITY**                | Not current work                                          |
| Minify++ independent project                     | **SETTLED**                           | Nift embeds but does not own its identity                 |
| Standalone Minify++ canonical                    | **CURRENT OPERATIONAL DIRECTION**     | Sync embedded copy from standalone                        |
| Nift minification opt-in                         | **SETTLED**                           | Never silently minify recognized extensions               |
| Minification at final-output boundary            | **STRONG PRINCIPLE**                  | Do not contaminate template semantics                     |
| Nift-specific React/Vue/Svelte integration       | **NOT WANTED CURRENTLY**              | Ordinary composition works                                |
| Nift as Vite/npm/task runner                     | **REJECTED DIRECTION**                | Use surrounding ecosystem                                 |
| Nift for state-heavy SPA automatically           | **NO**                                | Vite/framework may be better                              |
| Nift for rich publication system automatically   | **NO**                                | Hugo/Astro/docs tools may be better                       |
| Nift for existing-site migrations                | **STRONG HYPOTHESIS**                 | Test more realistically                                   |
| Nift for AI-generated bespoke sites              | **STRONG HYPOTHESIS + SOME EVIDENCE** | Continue real Codex experiments                           |
| AI DX = permissive syntax                        | **NO**                                | Fast precise failure may be better                        |
| Perfect comparison scores                        | **NOT A GOAL**                        | Credibility over advocacy                                 |
| Test count as quality metric                     | **REJECTED**                          | Bug-family coverage matters more                          |
| Add features to win comparisons                  | **REJECTED**                          | Protect conceptual integrity                              |
| Website build with candidate Nift                | **STRONG RELEASE PRACTICE**           | Useful self-hosting integration check                     |
| Public actions by Codex                          | **APPROVAL REQUIRED**                 | Prepare autonomously; publish only after approval         |

---

# Part XXVII — Chronology

This is intentionally approximate and conceptual rather than pretending every internal checkpoint corresponds neatly to a public release.

```text
Older Nift
│
├── broader scripting/runtime machinery
│   ├── LuaJIT
│   ├── ExprTk
│   ├── system-style functionality
│   └── hooks/orchestration
│
▼
Major simplification
│
├── strip scripting/runtime machinery
├── focus on website generation
├── dramatically smaller executable
└── tiny template/build model becomes central
│
▼
Website/docs rethink
│
├── modern positioning
├── public/ convention
├── template-language cleanup
├── AI-assisted development material
└── “glue, not universe”
│
▼
Early regression-suite work
│
├── parser boundaries
├── quoting
├── @content edge cases
├── CSS @ pass-through
└── CLI behavior
│
▼
Watch/state hardening
│
├── malformed JSON
├── watch initialization
├── tracked.json writing
├── empty state
└── JSON escaping
│
▼
Incremental/CLI/path hardening
│
├── build status
├── deleted outputs
├── hash cache behavior
├── directory dependencies
├── traversal
└── multi-item mutations
│
▼
Ruthless source-guided adversarial testing
│
├── read implementation after green suite
├── construct hostile cases
├── retain reproducers
└── fix bug families
│
▼
Architecture rewrite / deeper cleanup
│
├── stronger state ownership
├── collision handling
├── dependency lifecycle
├── sub-second timestamps
├── stronger hashing
└── lexical JSON scope
│
▼
Modern structured rendering
│
├── @json
├── JSON Schema
├── @for
├── object iteration
├── @if
├── sorting
└── $loop.*
│
▼
Minifier work
│
├── Sift prototype/history
├── extensive semantic hardening
├── renamed Minify++
├── standalone library/tool
└── optional Nift integration
│
▼
Large-project scaling audit
│
├── discover O(n²) collision validation
├── hash-indexed solution
├── discover temporary-memory cost
└── staged compact vector/sort validation
│
▼
v1.0.42-era architecture
│
├── strong scaling
├── controlled memory
└── mature behavioral baseline
│
▼
Contract-suite separation
│
├── C++ implementation tests remain local
└── external implementation-independent suite
│
▼
Codex handover
│
├── repositories reconciled against history
├── Minify++ identity corrected
├── current loop/JSON syntax recovered
└── operational gaps identified
│
▼
Codex uses Nift directly
│
├── creates working Nift website
└── demonstrates practical AI onboarding
│
▼
React/Vite islands experiment
│
├── Nift owns document
├── React owns stateful islands
├── zero Nift React integration
├── stable bundle requirement semantics
└── independent incremental domains
│
▼
Evidence-driven tool-choice evaluation
│
├── distinguish scaling from publication semantics
├── distinguish document-oriented from state-heavy apps
├── recognize explicit-composition costs
└── continue testing rather than defending ratings
│
▼
Current question
│
└── broaden $[...] value resolution inside directive parameters
```

---

# Part XXVIII — The deepest lessons from the project

## 144. Nift repeatedly improved by becoming conceptually smaller

This is probably the most striking historical pattern.

Many projects become “more capable” by accumulating abstractions.

Nift often became better by deleting them.

That does not mean every new feature is bad.

`@json`, loops and conditions demonstrate the opposite.

The real question is whether a feature strengthens the core website-generation model or expands Nift into another category of tool.

---

# 145. Correctness and simplicity have often aligned

Examples include:

```text
lowercase-only function names
clearer quoting rules
unknown @ pass-through
lexical scope
smaller state ownership
staged compact validation
removal of general scripting
```

Robustness has not generally required turning Nift into a more complicated system.

That is worth remembering when solving future bugs.

---

# 146. The most interesting remaining bugs are likely interaction bugs

Basic primitives have now been attacked extensively.

Expect future issues at boundaries:

```text
JSON + incremental builds
loops + scope
sorting + loop metadata
schema + data changes
Minify++ + output transactionality
parallel builds + diagnostics
symlinks + cached canonicalization
external generated assets + requirements
parameter interpolation + path validation
```

This is where future adversarial effort should concentrate.

---

# 147. The project culture should allow opinions to change

We have several good examples:

```text
apparent same-second Nift bug
→ actually flaky test fixture

hash-set validation
→ great CPU fix
→ unacceptable memory tradeoff
→ better staged approach

React-first build ordering
→ initially counted as DX friction
→ investigation revealed correct requirement semantics
```

The lesson is:

> We are allowed to be wrong. Investigate until the model improves.

Do not defend previous ChatGPT opinions merely because they are in this handover.

---

# 148. Evidence should beat advocacy

The goal is not to prove:

> Nift is the best website generator.

The goal is to discover:

> For which project shapes is Nift unusually good, and where is another tool clearly better?

That produces a stronger project and more credible documentation.

---

# 149. The project is increasingly interesting because of the combination, not one feature

Nift's distinctive proposition is increasingly something like:

```text
small conceptual model
+
ordinary web technologies
+
very high performance
+
precise incremental generation
+
low dependency footprint
+
strong adversarial testing
+
composability
+
good human DX
+
promising AI DX
```

Any one of those properties exists elsewhere.

The combination is what makes Nift unusual.

---

# 150. The rule I most want preserved

If you remember only one development rule from all this history, use this:

```text
Get green.

Read the implementation anyway.

Find an assumption nobody has tested.

Construct the smallest hostile case.

Prove whether it fails.

If it fails, preserve the reproducer.

Fix the underlying family rather than the visible symptom.

Run everything again.

Measure if performance or memory may have changed.

Document the reason if a future maintainer might otherwise undo it.
```

That development style did more for Nift than any individual feature.

---

# Final instruction to Codex

Treat this document as **institutional memory**, not executable specification.

Before changing Nift:

```text
current repository
+
current implementation tests
+
current external contract suite
+
current documentation
+
relevant Git history
        ↓
reconcile with this historical reasoning
        ↓
then act
```

If this handover says:

> “This is current behavior”

and the repository demonstrably says otherwise, investigate the discrepancy.

If this handover says:

> “Strong preference”

do not silently turn it into a compatibility requirement.

If it says:

> “Future possibility”

do not put it on the implementation roadmap automatically.

If it says:

> “Rejected direction”

assume there was meaningful reasoning behind the rejection and require new evidence before reopening it.

If it says:

> “Unresolved”

feel free to gather evidence rather than trying to infer what ChatGPT would have wanted.

Most importantly, from this point onward your **direct experience with Nift should be allowed to update this mental model**.

The purpose of the handover is not to freeze the project in the previous ChatGPT's opinions.

It is to make sure you know why the project looks the way it does before you start forming better opinions of your own.

---

That is the comprehensive context handover I’d give Codex.

I deliberately **haven’t folded the `$[...]` parameter-resolution implementation brief into it**, because Codex was exactly right about the ordering: this gives it the broad mental model first, then we can give it a much tighter second handover containing the exact semantics, edge cases, type behavior, parser cautions, dependency implications, tests, and implementation boundaries for the parameter-resolution change while that detail is freshest.

## 2026-08-18 — Jsonic++ extraction

The JSON implementation shared byte-for-byte by Nift core and Minify++ was given an independent canonical home as Jsonic++. Nift now vendors Jsonic++ `include/json.h` at `jsonic/json.h`; `src/Json.h` is a compatibility include. Parser semantics should originate in Jsonic++, synchronize into Nift, and then pass Nift JSON/schema/parser/project-contract integration before a checkpoint is complete.
