# Parameter interpolation contract

> This is the living behavioral specification and development context for `$[...]` interpolation in function parameters. Keep it aligned with current tests, implementation, and public documentation.

The maintenance point below is important enough to be an explicit **handover invariant**, not just something we assume everyone will remember.

The handover system should be treated as **living project infrastructure**. It should evolve with Nift for as long as the project exists: updated when decisions change, expanded when new bug families teach us something, trimmed when historical material stops being useful, corrected when workflows move, and checked whenever development changes architecture, testing, release procedures, cross-project ownership, or settled assumptions.

I’d tell Codex that explicitly before giving it the `$[...]` brief. Then I’d transition directly into the targeted development handover it asked for.

# Final Handover-Documentation Note + `$[...]` Parameter-Resolution Development Handover

Before the targeted development brief, there is one additional requirement for the durable handover documentation.

# 1. Handover documents are living project infrastructure

**Status: SETTLED MAINTENANCE PRINCIPLE**

The proposed:

```text
HANDOVER.md
docs/handover/PROJECT-CONTEXT.md
docs/handover/DEVELOPMENT.md
docs/handover/TESTING.md
docs/handover/RELEASES.md
docs/handover/DECISIONS.md
```

must not be treated as a one-time archival snapshot created during the Codex takeover.

They should be **maintained throughout the life of the project**.

That means they may need to be:

```text
updated
expanded
corrected
reorganized
trimmed
split
merged
or occasionally removed/replaced
```

as the project evolves.

The purpose is to preserve current institutional knowledge, not merely preserve what ChatGPT and Codex happened to believe in August 2026.

A future change is not necessarily complete until the developer has considered whether it changes any handover material.

This should be stated explicitly in the root handover documentation.

A useful formulation would be:

> **These handover documents are living project documentation. Keep them synchronized with meaningful changes to architecture, behavior, development workflow, testing strategy, release procedure, cross-project ownership, and documented decisions. Add new institutional knowledge when significant bugs, investigations, experiments, or design decisions teach something future maintainers should know. Correct or remove material that becomes obsolete. Do not preserve stale handover text merely because it is historical.**

Historical context that remains useful should of course be preserved.

The distinction is:

```text
useful history
    → preserve

obsolete operational instruction
    → update/remove

decision superseded by new evidence
    → record the change and current status

new important bug-family lesson
    → add it

new cross-project invariant
    → document it

temporary conversational detail
    → do not fossilize it
```

The same principle applies across:

```text
Nift
Nift regression suite
Nift website
Minify++
Minify++ website
tscc
tscc regression suite
tscc website
```

according to each repository's handover responsibilities.

---

# 2. Maintenance responsibility should be encoded in the handover itself

Each root `HANDOVER.md` should eventually include something similar to:

## Maintaining this handover

This handover and its linked documents are living project infrastructure.

When making a meaningful change, check whether it affects:

* architecture;
* public or contractual behavior;
* development workflow;
* build/test commands;
* validation strategy;
* release or deployment procedure;
* cross-project ownership or synchronization;
* documented settled/rejected/unresolved decisions;
* historical rationale for non-obvious implementation;
* known hazards or bug families.

Update the relevant handover material when appropriate.

Do not modify handover files merely because every code change needs documentation. Most implementation changes will not require handover updates.

Conversely, do not leave a materially incorrect handover statement in place merely because the source or tests are authoritative.

The goal is for a future developer or coding agent to receive a current, accurate mental model without needing access to historical conversations.

That principle should become part of normal project maintenance.

---

# 3. Decision evolution should be recorded rather than overwritten blindly

If a prior decision changes, preserve enough history to explain the evolution.

For example:

```text
Old status:
REJECTED

New evidence:
real project family repeatedly exposed missing capability

New status:
UNRESOLVED / ACCEPTED / SUPERSEDED
```

Do not simply delete the old rationale if knowing why the previous choice made sense remains useful.

Likewise, if an old operational instruction simply became obsolete because a build command changed, replace it normally rather than creating historical clutter.

Use judgment.

---

# 4. New bug families should feed institutional memory

The development methodology deliberately discovers assumptions that previous developers missed.

When one of those discoveries has a broader lesson, consider updating the handover/testing context.

For example:

```text
new bug:
parameter interpolation accidentally executes nested Nift operations

broader lesson:
value resolution must remain side-effect-free

durable place:
DECISIONS.md / TESTING.md
```

whereas a trivial off-by-one implementation error may need only:

```text
regression test
+
code fix
```

not a new historical essay.

The handover should capture lessons that change how future work should be approached.

---

# 5. With that clarification, the handover architecture discussion is complete

The remaining work is now repository reconciliation and implementation.

The rest of this document is the focused development handover for the upcoming `$[...]` parameter-resolution change.

---

# Part I — Purpose of the `$[...]` change

## 6. Current motivation

Nift's current value system already allows data and metadata access through:

```text
$[...]
```

This includes values originating from concepts such as:

```text
built-in page/project metadata
JSON bindings
loop bindings
other currently supported value contexts
```

The problem is that directive/function parameters have historically been much more literal.

Conceptually, a user may already be able to render:

```text
$[page.partial]
```

into output, yet cannot naturally use the same value to choose an input file:

```text
@input($[page.partial])
```

or construct a path:

```text
@input('partials/$[page.layout].html')
```

That is an artificial boundary between:

```text
value can be rendered into document text
```

and:

```text
value can participate in an operation's textual argument
```

The desired change is to close that gap **without converting Nift parameters into arbitrary nested templates or general expressions**.

---

# 7. Core design

**Status: TARGET DESIGN**

A directive parameter should support **parameter interpolation / value resolution** consisting conceptually of:

```text
literal parameter text
+
$[...] expressions
```

Example:

```text
@input('partials/$[page.layout].html')
```

should conceptually resolve:

```text
[
    Literal("partials/"),
    Value(page.layout),
    Literal(".html")
]
```

to a final string such as:

```text
partials/sidebar.html
```

before `@input` performs its normal operation.

The important conceptual sequence is:

```text
parse parameter
    ↓
resolve parameter values
    ↓
obtain final textual parameter
    ↓
perform existing directive operation
```

not:

```text
run parameter as arbitrary Nift source
    ↓
capture emitted output somehow
    ↓
use result as argument
```

---

# 8. This is value interpolation, not full templating

**Status: SETTLED DESIGN BOUNDARY**

Do not implement parameter handling by sending parameter source back through Nift's full template parser.

This is the central caution.

A full template parser can encounter operations such as:

```text
@input
@dep
@json
@pathto
@content
control flow
```

and those operations may:

```text
perform filesystem IO
record dependencies
record requirements
create bindings
change lexical scope
emit output
recursively parse templates
alter build state
```

Those are **not values**.

---

# 9. Why arbitrary nested operations are rejected

Consider:

```text
@input(@input('which-partial.txt'))
```

If arbitrary parameter templating were allowed, several questions immediately arise:

```text
What is the return value of inner @input?

Does it emit its rendered HTML?

Is that HTML treated as a filename?

When is its dependency recorded?

What happens if the outer @input fails?

Are inner side effects rolled back?

Can @content return a string?

Can @dep return a string?

Can @json return anything?
```

Likewise:

```text
@pathto(@dep('foo.txt'))
```

raises the question:

```text
what is the string value of @dep?
```

and:

```text
@json(data, @input('data-path.txt'))
```

mixes rendering, IO, dependencies and filename resolution in a way Nift has never needed to define.

Avoid opening that semantic space.

---

# 10. The intended language distinction

Use this mental model:

```text
operations
    @input(...)
    @dep(...)
    @json(...)
    @pathto(...)
    ...

values
    $[...]
```

Then:

```text
@input($[page.partial])
```

is:

```text
operation(value)
```

and:

```text
@input('partials/$[page.layout].html')
```

is:

```text
operation(interpolated textual value)
```

That is a clean language model.

---

# Part II — Intended supported forms

## 11. Entire parameter from one value

Expected class:

```text
@input($[page.partial])
```

If:

```text
page.partial = "partials/card.html"
```

then the effective parameter is:

```text
partials/card.html
```

The existing `@input` implementation should then process that path normally.

---

# 12. Interpolation inside quoted text

Expected:

```text
@input('partials/$[page.layout].html')
```

with:

```text
page.layout = "sidebar"
```

resolves to:

```text
partials/sidebar.html
```

---

# 13. Multiple values in one parameter

Expected class:

```text
@dep('generated/$[release]/$[dataset].json')
```

Example:

```text
release = "v4"
dataset = "products"
```

resolves:

```text
generated/v4/products.json
```

---

# 14. Adjacent values

The parser/resolver should correctly handle conceptual cases such as:

```text
'$[a]$[b]'
```

without inserting implicit whitespace or separators.

If:

```text
a = "foo"
b = "bar"
```

then:

```text
foobar
```

is expected.

---

# 15. Values mixed with punctuation

Examples:

```text
'$[name].html'
'data-$[version].json'
'$[dir]/$[file]'
'assets/$[theme]-$[variant].css'
```

should simply concatenate textual fragments.

---

# 16. Built-in metadata and JSON bindings should use the same value mechanism

**Status: STRONG DESIGN EXPECTATION**

The interpolation layer should not grow separate logic like:

```text
if metadata...
else if JSON...
else if loop...
```

where avoidable.

`$[...]` already has a value-resolution mechanism.

Parameter interpolation should ideally reuse the same semantic resolver used when `$[...]` appears in ordinary template output.

The goal is:

```text
same $[...] semantics
different destination
```

not:

```text
one interpretation in document output
different interpretation in parameters
```

---

# Part III — Important semantic boundary: output destination

## 17. Ordinary `$[...]` rendering versus parameter `$[...]`

A `$[...]` expression in ordinary template text conceptually does:

```text
resolve value
    ↓
emit textual representation into output
```

A `$[...]` expression inside a parameter should instead do:

```text
resolve value
    ↓
append textual representation to temporary parameter string
```

It must **not emit to document output** during parameter resolution.

That sounds obvious, but it is worth testing explicitly.

For:

```text
@input('partials/$[page.layout].html')
```

Nift should not accidentally output:

```text
sidebar
```

before rendering the included file.

---

# 18. Side effects must remain those of the outer operation

Parameter resolution should itself be side-effect-free except for whatever normal value lookup semantics already require.

For example:

```text
@input('$[partial]')
```

should have dependency/state effects equivalent to:

```text
@input('partials/card.html')
```

after resolution.

The `$[...]` lookup should not independently create a dependency merely because it was interpolated, unless existing `$[...]` semantics explicitly require such behavior for that value source.

The **outer directive** owns its normal operational semantics.

---

# Part IV — Type behavior

## 19. Do not invent coercion rules casually

**Status: IMPORTANT IMPLEMENTATION CAUTION**

`$[...]` may resolve to values with types such as:

```text
string
number
boolean
null
array
object
```

depending on current Nift JSON/value semantics.

Before implementing parameter interpolation, inspect:

```text
current $[...] rendering rules
current type errors
JSON lookup implementation
tests
documentation
```

and establish what parameter values should accept.

Do not assume historical discussion is the final contract here.

---

# 20. Historical conservative preference

Historically, we leaned toward allowing textual/string values for path-like parameters and rejecting complex JSON values rather than silently serializing them.

Conceptually:

```text
string
→ clearly usable

array/object
→ probably invalid for textual path parameter
```

We also discussed conservative behavior for:

```text
number
boolean
null
```

rather than automatically deciding that:

```text
42 → "42"
true → "true"
null → ""
```

must be correct.

However:

> **The current repository and current `$[...]` semantics must be examined before locking this rule into the contract suite.**

The important principle is consistency and predictability.

---

# 21. Separate general interpolation from directive validation

If the interpolation layer produces a text value, the outer directive should still apply all of its existing validation.

For example:

```text
@pathto('$[something]')
```

should not bypass `@pathto` validation merely because its path came from interpolation.

Likewise:

```text
@input('$[path]')
```

still needs:

```text
path validation
existence behavior
dependency registration
containment rules
normal error handling
```

Interpolation is not a privileged path.

---

# Part V — Quoting

## 22. Preserve current quote semantics

Nift currently recognizes:

```text
'...'
"..."
```

as quoted parameter forms.

Backticks are deliberately not Nift quotes.

Parameter interpolation should work consistently inside both supported quote styles.

Examples:

```text
@input('partials/$[layout].html')
@input("partials/$[layout].html")
```

should have equivalent value-resolution semantics.

---

# 23. Do not leak surrounding quotes into the resolved argument

The existing parameter parser presumably already strips/deals with quote delimiters.

Interpolation should operate on the correct logical parameter content.

For:

```text
@input('$[path]')
```

if:

```text
path = "foo.html"
```

the resulting argument should be:

```text
foo.html
```

not:

```text
'foo.html'
```

---

# 24. Whitespace behavior must preserve existing parameter rules

Nift has historical regressions around parameter whitespace such as:

```text
@input("file.html" )
```

Do not accidentally reintroduce those bugs when adding interpolation.

Test:

```text
@input( $[path] )
@input('$[path]' )
@input( '$[path]')
@input(
    '$[path]'
)
```

according to whatever spacing forms current grammar already accepts.

Do not broaden grammar merely to make tests pass.

---

# Part VI — `$[...]` parsing boundaries

## 25. Use the existing `$[...]` grammar

Do not create a new simplified parameter-only value parser unless there is a compelling implementation reason.

If ordinary output supports:

```text
$[site.name]
$[items[0].title]
$current-current-syntax
```

parameter resolution should use the same accepted value syntax.

The current repository is authoritative for exactly which expressions exist.

---

# 26. Closing bracket handling must be robust

Test malformed cases such as:

```text
'$[foo'
'$[]'
'$[foo]]'
'$[[foo]'
```

according to existing `$[...]` error semantics.

Do not accidentally treat malformed parameter expressions as literal text if normal `$[...]` rendering would reject them, unless the language explicitly requires that distinction.

Consistency matters.

---

# 27. Adjacent punctuation should not alter function parsing

Historical Nift parser bugs around boundaries such as:

```text
@content<
```

show why lexical boundaries need deliberate testing.

For parameter interpolation, test expressions adjacent to:

```text
/
.
-
_
:
?
&
=
#
]
)
'
"
```

where those characters are legal in parameter text.

---

# Part VII — Escaping/literal `$`

## 28. Inspect the existing escape contract before implementing

Nift already has rules for escaping special template characters such as:

```text
@
$
#
```

in relevant contexts.

Parameter interpolation must respect the existing literal-dollar mechanism.

For example, if current Nift allows a user to express literal:

```text
$[foo]
```

without resolving it, the same mechanism should work inside parameter text where appropriate.

Do not invent a second parameter-specific escaping syntax.

---

# 29. Literal `$` near interpolation deserves tests

Possible classes:

```text
'cost-$[price]'
'literal-dollar-...'
'$$[...]'
escaped forms according to current grammar
```

Again, derive exact expected syntax from current Nift behavior.

The task is to extend existing value behavior into parameters, not redesign escaping.

---

# Part VIII — Scope behavior

## 30. Parameter interpolation must see the current lexical scope

This is a major interaction with modern Nift.

Inside:

```text
@for(item : items){
    @input('partials/$[item.layout].html')
}
```

`item` should resolve to the current iteration binding.

Likewise inside nested control flow:

```text
outer binding
    ↓
inner loop
        ↓
parameter interpolation
```

must resolve the correct lexical binding.

---

# 31. Shadowing must behave exactly like ordinary `$[...]`

Example conceptual case:

```text
outer item
    ↓
inner loop also named item
        ↓
@input('$[item.partial]')
```

The parameter must resolve whichever `item` ordinary `$[...]` would resolve in that lexical position.

Do not invent parameter-specific scope lookup.

---

# 32. `$loop.*` should work if ordinary `$[...]` supports it there

If valid ordinary template syntax can render something such as:

```text
$loop.index/current syntax
```

then parameter interpolation should ideally inherit exactly the same behavior.

For example, a project may eventually do something conceptually like:

```text
@input('row-$[loop.index].html')
```

if the current `$[...]` grammar exposes loop metadata that way.

Do not hardcode assumptions; inspect current syntax/tests.

Nested-loop restoration should also apply.

---

# Part IX — Interaction with `@input`

## 33. `$[...]`-resolved `@input` paths are a primary use case

This is likely one of the most valuable examples.

Expected:

```text
@json(page, 'page.json')

@input($[page.partial])
```

or:

```text
@input('partials/$[page.layout].html')
```

depending on current binding syntax.

---

# 34. Dependency tracking must record the resolved file

After interpolation:

```text
@input('partials/$[page.layout].html')
```

with:

```text
page.layout = "hero"
```

should behave for dependency purposes as though the template literally said:

```text
@input('partials/hero.html')
```

If `partials/hero.html` changes, the correct page should rebuild.

---

# 35. Changing the value source must change the dependency relationship

This is a particularly important incremental test.

Suppose:

```text
page.layout = "hero"
```

initially resolves:

```text
partials/hero.html
```

Then JSON changes:

```text
page.layout = "feature"
```

Now the page should depend on:

```text
partials/feature.html
```

and should no longer behave as though the old input remains the only relevant dependency.

This tests dynamic dependency-set replacement.

---

# 36. Old resolved dependencies should not remain stale forever

After the resolved `@input` path changes, verify:

```text
new dependency recorded
old dependency removed where appropriate
```

Otherwise future changes to the old partial may incorrectly rebuild the page.

That would be a dependency-lifecycle bug.

---

# 37. Missing resolved input must use ordinary `@input` failure semantics

If:

```text
$[page.partial]
```

resolves to:

```text
partials/missing.html
```

the behavior should match literal:

```text
@input('partials/missing.html')
```

No special weaker error behavior should exist for interpolated parameters.

---

# Part X — Interaction with `@pathto`

## 38. `$[...]` should be able to select dynamic paths/names where the directive normally accepts them

Examples conceptually include:

```text
@pathto($[page.destination])
```

or:

```text
@pathto('downloads/$[name].pdf')
```

subject to the exact accepted argument semantics of current `@pathto`.

---

# 39. Interpolation must not weaken `@pathto` verification

The resolved path/name must still pass normal:

```text
tracking lookup
filesystem verification
requirement registration
containment
error handling
```

as appropriate.

---

# 40. Requirement semantics should follow the resolved target

If a parameter resolves from:

```text
public/assets/app.js
```

to another asset later, the requirement set should update to the new resolved asset.

Test that no stale requirement remains indefinitely if current sidecar/state design expects replacement.

---

# Part XI — Interaction with `@dep`

## 41. Dynamic dependencies are another natural use case

Example:

```text
@dep('generated/$[release]/$[dataset].json')
```

The resolved path should be treated exactly like a literal dependency parameter.

---

# 42. Dependency invalidation must follow value changes

If:

```text
release = "v1"
```

resolves:

```text
generated/v1/products.json
```

and later becomes:

```text
release = "v2"
```

then the dependency graph must update accordingly.

Again:

```text
new dependency in
old dependency out
```

where appropriate.

This is likely one of the more important regression families for the new feature.

---

# Part XII — Interaction with `@json`

## 43. Dynamic JSON filenames are potentially useful

A natural use case is conceptually:

```text
@json(data, 'data/$[page.dataset].json')
```

If the current grammar supports this after the feature, it should use the same interpolation layer.

---

# 44. Be careful about evaluation order

The parameter value must already exist in the current lexical scope before it can resolve.

For example:

```text
@json(config, 'config.json')
@json(data, 'data/$[config.dataset].json')
```

may be meaningful.

But:

```text
@json(data, 'data/$[data.file].json')
```

cannot use a binding that does not exist until the outer `@json` operation succeeds.

Do not create circular magical semantics.

The conceptual order is:

```text
resolve parameter using current scope
        ↓
open/parse JSON
        ↓
create new binding
```

---

# 45. Dependencies should include the resolved JSON file

If a dynamic `@json` path resolves successfully, normal JSON dependency tracking should apply to that resolved source.

If the selector value changes, the JSON source dependency should update.

---

# Part XIII — Interaction with multiple-parameter functions

## 46. Every textual parameter should be considered deliberately

Some Nift operations may have:

```text
one parameter
multiple path parameters
path + binding name
other structured parameter forms
```

Do not blindly interpolate every token in a parameter list if some positions are identifiers rather than textual values.

For example, conceptually:

```text
@json(binding, path)
```

has two semantically different arguments:

```text
path
    → textual/path value, likely interpolation candidate

binding
    → lexical identifier, probably not arbitrary string interpolation
```

This distinction is important.

---

# 47. Binding identifiers should probably remain grammar identifiers

**Status: STRONG EXPECTATION; VERIFY CURRENT GRAMMAR**

Something conceptually like:

```text
@json($[bindingName], 'file.json')
```

would turn dynamic value resolution into dynamic symbol creation.

That opens a substantially different semantic space.

Unless current task requirements explicitly say otherwise:

> apply interpolation to textual/value parameters, not identifier grammar positions.

Inspect every directive individually.

Do not implement one generic “interpolate every argument substring” pass without understanding argument semantics.

---

# Part XIV — Error behavior

## 48. Resolution errors should identify the value problem

If:

```text
@input('partials/$[page.layout].html')
```

fails because:

```text
page.layout
```

does not exist, the diagnostic should ideally distinguish:

```text
unable to resolve value
```

from:

```text
resolved path exists syntactically but file is missing
```

The exact current diagnostic style should be preserved.

Do not over-engineer new error infrastructure solely for this task.

---

# 49. Failed interpolation must not partially execute the outer operation

Conceptually:

```text
resolve all required parameter values
        ↓
if any fail:
    report failure
    do not run @input/@dep/@json/etc.
```

This preserves clean transaction boundaries.

---

# 50. Parameter fragments should not be partially committed

For:

```text
'foo/$[a]/$[missing]/$[b]'
```

the implementation may internally build a temporary buffer progressively, but no directive side effect should occur until the full parameter resolves successfully.

---

# Part XV — Performance

## 51. Do not over-optimize prematurely

Parameter interpolation should normally involve very small strings.

A straightforward implementation is preferable to complex caching unless profiling demonstrates a real cost.

---

# 52. But avoid obviously pathological repeated parsing

If every parameter containing several `$[...]` expressions repeatedly reparses the entire parameter from the beginning, there may be unnecessary complexity.

A single linear scan over the logical parameter string is the natural target.

Conceptually:

```text
scan literal
    ↓
encounter $[
    ↓
parse one value expression
    ↓
resolve
    ↓
append
    ↓
continue
```

Expected complexity should remain approximately proportional to parameter length plus value-resolution cost.

---

# 53. Avoid unnecessary duplicate buffers where easy

Given Nift's recent memory-history lessons, do not create several complete copies of:

```text
raw parameter
unquoted parameter
token vector
rendered temporary template
resolved parameter
```

unless needed.

But do not contort the implementation to save tens of bytes either.

Clarity first.

---

# Part XVI — Implementation architecture

## 54. Prefer a dedicated side-effect-free resolver

A clean architecture would likely involve something conceptually equivalent to:

```text
resolve_parameter_value_template(...)
```

or:

```text
interpolate_parameter(...)
```

that:

```text
input:
    parsed/logical parameter text
    current lexical/value scope

output:
    resolved string
    or structured error
```

with no outer directive side effects.

The actual function name/design should follow current code architecture.

---

# 55. Reuse value resolution

If current code already has something roughly like:

```text
resolve_value(expression)
```

for `$[...]`, reuse that semantic mechanism.

Do not duplicate JSON traversal, metadata lookup, scope lookup, loop lookup, error semantics, or type handling.

---

# 56. Keep parsing and operation execution separated

Ideal conceptual structure:

```text
parse outer directive
        ↓
parse parameter structure
        ↓
interpolate textual argument(s)
        ↓
validate argument(s)
        ↓
perform operation
```

This keeps the implementation comprehensible.

---

# 57. Do not recursively invoke the general Nift parser

This deserves repetition because it is the easiest tempting shortcut.

Do **not** implement:

```text
resolved = parse_as_nift_template(parameter)
```

even if that would make `$[...]` work immediately.

That would silently make every operation legal in argument context unless elaborate suppression is added.

The entire point of this feature is avoiding that language expansion.

---

# Part XVII — Contract-suite strategy

## 58. Specify behavior externally first

The independent Nift regression suite should receive behavioral cases before or alongside implementation.

Because this is user-visible language behavior, the contract suite is the right place to define the feature.

---

# 59. Keep tests implementation-independent

Tests should create normal Nift projects and run the executable.

Do not reach into parser internals.

---

# 60. Start with simple positive cases

At minimum, cover conceptual forms equivalent to:

```text
@input($[value])

@input('prefix/$[value]/suffix')

@input('$[a]$[b]')

@dep('data/$[name].json')

@pathto('$[path]')
```

adjusted to exact current Nift syntax and fixture semantics.

---

# 61. Add lexical-scope cases

Test interpolation inside:

```text
@for
@if
nested loops
object iteration
```

using current syntax.

---

# 62. Add type/error cases

Based on the contract chosen after source reconciliation, test:

```text
missing binding
wrong type
null
array
object
malformed $[...] syntax
```

as appropriate.

---

# 63. Add dependency-set transition tests

These are especially important.

For dynamic `@input`:

```text
selector initially → A
build
selector changes → B
build
change A
    → should no longer rebuild solely because of stale dynamic dependency
change B
    → should rebuild
```

where the current dependency model supports exact removal semantics.

Likewise for `@dep`, `@json`, and requirements where applicable.

---

# 64. Test no accidental output emission

Given:

```text
@input('partial-$[name].html')
```

verify the resolved value is not independently emitted into the output before the included content.

This guards against accidental use of rendering machinery as the resolver.

---

# 65. Test that nested operations remain invalid/literal according to current grammar

Explicitly guard the rejected direction.

Possible cases:

```text
@input(@input('x'))
@pathto(@dep('x'))
```

The exact expected result depends on how current Nift treats malformed/unquoted argument syntax.

The important contract is:

> this feature must not make arbitrary nested operations valid value expressions.

Add a regression that would fail if a future maintainer implements interpolation via recursive full-template evaluation.

---

# 66. Test CSS/JS transparency nearby

Because `$[...]` parsing is expanding into another context, include some non-Nift text around it to ensure ordinary content remains unaffected.

Do not turn this into a giant unrelated parser audit, but protect obvious lexical boundaries.

---

# Part XVIII — Incremental-build test matrix

## 67. Dynamic `@input`

Test:

```text
selector source changes
→ page rebuilds

resolved input changes
→ page rebuilds

old resolved input changes after selector moved
→ no stale rebuild where inappropriate

new resolved input missing
→ controlled failure
```

---

# 68. Dynamic `@dep`

Test equivalent transitions.

---

# 69. Dynamic `@json`

If supported in the task scope:

```text
selector changes JSON source
→ page rebuilds using new source

new JSON changes
→ rebuild

old JSON changes
→ no stale invalidation where dependency removed
```

Also test malformed new JSON.

---

# 70. Requirement-only path

For something like a dynamically resolved concrete `@pathto` asset:

```text
value changes to another asset
→ generated HTML may change if textual URL changes, so rebuild caused by selector source

resolved asset bytes change
→ may not require page rebuild if only existence/path matters

resolved asset disappears
→ requirement invalidated
```

The exact behavior depends on current `@pathto` categories.

Use current source/tests to distinguish tracked-name and concrete-file cases.

---

# Part XIX — Interaction with JSON source dependency

## 71. Selector source itself is a dependency

Suppose:

```text
@json(page, 'page.json')
@input('$[page.partial]')
```

There are two dependency relationships:

```text
page.json
    → changes what parameter resolves to

resolved partial
    → contributes content
```

Both matter.

If `page.json` changes from:

```text
partial = A
```

to:

```text
partial = B
```

the page should rebuild because the JSON changed, then establish the new input dependency.

This is an important end-to-end test.

---

# Part XX — Avoiding accidental language expansion

## 72. No arbitrary expressions unless already part of `$[...]`

Do not make parameter interpolation support:

```text
$[a + b]
$[foo ? bar : baz]
```

unless ordinary `$[...]` already supports that syntax as part of the current language.

This task extends **where values can be used**, not necessarily **what values can express**.

---

# 73. No function calls

Do not introduce:

```text
$[lowercase(name)]
```

or similar unless that already exists.

---

# 74. No assignment

Do not introduce:

```text
$[x = value]
```

---

# 75. No nested `@...`

Already covered, but this remains a core negative requirement.

---

# 76. No dynamic identifier creation unless explicitly specified

Do not let textual interpolation turn lexical names/binding identifiers into runtime-generated symbols accidentally.

---

# Part XXI — Compatibility considerations

## 77. Existing literal parameters must remain unchanged

This feature must preserve behavior for existing templates such as:

```text
@input('partials/header.html')
@dep('data.json')
@pathto('docs')
```

There should be essentially zero semantic cost to users who never use `$[...]` in parameters.

---

# 78. Literal `$` behavior must remain compatible

Existing parameter strings containing `$` but not valid value interpolation must behave according to current language rules.

Search the regression suite for literal-dollar cases before implementing.

---

# 79. Error messages should not regress unrelated malformed syntax

Running parameter text through a new scanner may alter which error is produced for old malformed input.

Capture existing behavior for representative malformed parameter cases before implementation.

Not every exact wording must necessarily be contractual, but success/failure behavior must not shift accidentally.

---

# Part XXII — Security/path safety

## 80. Interpolated paths are untrusted paths

A value sourced from JSON may contain:

```text
../
absolute path
unexpected separator
symlink target
collision path
empty string
```

Do not trust it merely because it came through `$[...]`.

The outer directive's normal containment/path validation must remain authoritative.

---

# 81. Add traversal regressions through interpolation

If literal:

```text
@input('../outside.html')
```

is rejected, then:

```text
@json(... path = "../outside.html" ...)
@input($[path])
```

must be rejected equivalently.

This is an important security/correctness regression.

---

# 82. Add collision/path-boundary tests only where operation semantics make sense

Do not explode the task into retesting every filesystem command.

But ensure interpolation does not bypass existing validation layers.

---

# Part XXIII — Diagnostic quality

## 83. Distinguish resolution failure from operation failure where practical

Useful conceptual distinction:

```text
@input('$[missing]')
    → value resolution failure

@input('$[path]')
path resolves to missing.html
    → @input file failure
```

This can make errors significantly more understandable.

Do not require a large diagnostic rewrite if current infrastructure cannot express this cleanly.

---

# 84. Preserve source location accuracy

If current parser diagnostics carry source positions, parameter interpolation errors should ideally point to the relevant `$[...]` expression rather than only the beginning of the directive.

Again, verify current capabilities.

---

# Part XXIV — Documentation expectations

## 85. Document this as parameter interpolation/value resolution

Preferred terminology:

```text
parameter interpolation
```

or:

```text
parameter value resolution
```

Avoid describing parameters as:

```text
mini Nift templates
```

because that suggests arbitrary Nift syntax can execute there.

---

# 86. Examples should teach the boundary

Good documentation examples:

```text
@input($[page.partial])

@input('partials/$[page.layout].html')

@dep('data/$[dataset].json')
```

Potentially include a short statement:

> Directive parameters may interpolate `$[...]` values. Other Nift directives are not evaluated inside parameters.

That communicates the model clearly.

---

# 87. Update AI-oriented docs/context

This change is especially useful for agents because it removes the temptation to hardcode dynamic paths or invent nested directive syntax.

When updating Nift's AI context/documentation, show correct examples.

---

# Part XXV — Performance validation

## 88. Main goal is “no meaningful regression”

This feature should not materially affect templates whose parameters contain no `$[...]`.

The normal fast path should remain cheap.

A reasonable architecture can begin with:

```text
does parameter contain interpolation marker?
    no → existing path
    yes → interpolation scan
```

if that fits current code.

Do not add a premature special case if the simpler general scan is already negligible, but measure if parameter parsing is hot.

---

# 89. Run representative benchmarks

Because Nift's performance is a product feature, run at least relevant existing performance checks after implementation.

Pay attention to:

```text
large number of literal directives
large number of interpolated directives
10k-page normal fixture if applicable
no-op incremental
```

Do not create a major benchmark project solely for this feature unless existing performance evidence suggests a problem.

---

# Part XXVI — Suggested implementation workflow

## 90. Step 1: repository reconnaissance

Before coding, identify:

```text
where directive parameters are parsed
where quotes are stripped/normalized
where $[...] is currently parsed
where values are resolved
how lexical scope is represented
how JSON values/types are represented
how @input/@dep/@json/@pathto consume arguments
how dependency/requirement sets are updated
```

---

# 91. Step 2: document current behavior

Create a small implementation note/report:

```text
current parameter parser
current $[...] resolver
candidate integration point
directive argument categories
current coercion rules
```

This prevents solving the wrong abstraction.

---

# 92. Step 3: specify the contract

Add the smallest clear external tests for:

```text
whole-value parameter
mixed literal/value
multiple values
scope
error
dependency transition
negative nested-directive behavior
```

Run them and confirm they fail for the intended missing feature rather than due to bad fixture assumptions.

---

# 93. Step 4: implement a side-effect-free interpolation layer

Reuse current value semantics.

Do not recursively invoke full Nift parsing.

---

# 94. Step 5: wire only semantically textual arguments

Inspect each directive's argument roles.

Do not interpolate binding identifiers or syntax-level identifiers unless explicitly part of the intended contract.

---

# 95. Step 6: focused tests

Run:

```text
new parameter-resolution tests
existing $[...] tests
existing @input tests
existing @dep tests
existing @json tests
existing @pathto tests
scope/control-flow tests
path/traversal tests
```

as relevant.

---

# 96. Step 7: full implementation-local suite

Run all Nift-local tests.

---

# 97. Step 8: external contract suite

Run the full independent Nift regression suite against the candidate binary.

---

# 98. Step 9: sanitizer validation

At minimum use the sanitizer workflows appropriate to the parser/string/state changes if available.

This feature introduces new string scanning and indexing logic, so ASan/UBSan are particularly relevant.

---

# 99. Step 10: performance validation

Run proportionate existing Nift benchmarks and compare before/after.

If nothing changes measurably, say so rather than trying to manufacture a performance story.

---

# 100. Step 11: rebuild the Nift website

Use the candidate binary.

This provides a realistic project-level regression check.

If current website templates do not use the new feature, that is still useful compatibility evidence.

---

# 101. Step 12: create a small real example

After implementation/tests are green, create or adapt a small realistic project using something like:

```text
@json(...)
@input('partials/$[page.layout].html')
```

This validates usability beyond synthetic tests.

Do not confuse this with the contract suite.

---

# Part XXVII — Things to inspect especially carefully

## 102. Existing parameter parsing may already mix syntax and semantics

If current code parses:

```text
quotes
commas
function-specific structures
```

in one path, adding interpolation in the wrong layer may create surprising behavior.

Try to identify the narrowest shared textual-parameter stage.

---

# 103. String lifetime

If value resolution returns:

```text
string_view
reference into JSON DOM
temporary string
```

be careful when appending/resolving into parameter buffers.

Avoid dangling references after:

```text
vector growth
JSON scope changes
temporary destruction
```

---

# 104. Nested bracket parsing

If `$[...]` supports nested indexing such as:

```text
$[items[0].name]
```

the interpolation scanner must not treat the first inner `]` as the end of the outer value expression incorrectly.

Reuse the established `$[...]` parser rather than searching naively for the next `]`.

This is a potentially important implementation trap.

---

# 105. Quoted content inside `$[...]`

If current expression grammar supports quoted keys or syntax containing brackets, reuse the actual parser.

Do not implement:

```cpp
find("]")
```

as interpolation parsing unless the existing grammar guarantees that is correct.

---

# 106. Error recovery

A malformed interpolated parameter should not leave parser position/state corrupted such that subsequent content is misinterpreted.

Add at least one fixture where malformed parameter syntax is followed by ordinary HTML/Nift text and verify controlled failure rather than runaway parsing.

---

# Part XXVIII — Potential future extensions that are NOT part of this task

## 107. `@getenv` as a value

Historically, one interesting boundary was environment variables.

`@getenv(...)` is operational syntax, but environment lookup is semantically much closer to a value than `@input`.

There may someday be an argument for a value-form environment lookup.

That is **not part of this parameter interpolation task unless explicitly requested**.

Do not special-case nested:

```text
@getenv
```

inside parameters just because it seems useful.

If a value-form environment facility is ever desired, design it explicitly.

---

# 108. General string functions

Not part of this task.

---

# 109. Arithmetic/expressions

Not part of this task.

---

# 110. Dynamic binding names

Not part of this task.

---

# 111. Recursive parameter templates

Explicitly not part of this task.

---

# Part XXIX — Acceptance criteria

The feature should be considered successful when all of the following are true:

* Existing Nift templates without parameter interpolation behave unchanged.
* `$[...]` can supply a full textual directive parameter where semantically appropriate.
* `$[...]` can be interpolated with literal parameter text.
* Multiple `$[...]` values can participate in one parameter.
* Parameter value semantics match ordinary `$[...]` semantics.
* Current lexical scope is respected.
* Interpolation itself emits no document output.
* Interpolation itself introduces no arbitrary directive side effects.
* Outer directives retain all existing validation/dependency/requirement semantics.
* Dynamic resolved dependencies/requirements update correctly as selector values change.
* Interpolated paths cannot bypass path-safety checks.
* Invalid/missing values fail cleanly.
* Complex/non-text value types behave according to an explicitly tested contract.
* Arbitrary nested Nift operations do not become valid parameter expressions.
* Binding-name/identifier positions are not accidentally made dynamic.
* Existing regression suite remains green.
* Relevant sanitizers remain green.
* No meaningful performance regression is introduced.
* Candidate Nift still builds the real Nift website successfully.
* Documentation accurately describes the feature as value interpolation rather than full parameter templating.

---

# Part XXX — The architectural test

When reviewing the implementation, ask:

> Does this change make Nift values more composable, or does it quietly make Nift operations composable?

The intended answer is:

```text
values become more composable
operations do not
```

That is the entire architectural point.

We want:

```text
@input('partials/$[page.layout].html')
```

because:

```text
$[page.layout]
```

is already a side-effect-free value.

We do not want:

```text
@input(@input(...))
```

because `@input` is an operation with independent filesystem, rendering and dependency semantics.

If the implementation preserves that distinction, the feature fits modern Nift extremely well.

If the implementation blurs it, stop and reconsider before expanding the language accidentally.

---

# Final instruction

Reconcile this task handover against:

```text
current Nift source
current Nift-local tests
current external contract suite
current documentation
current Git state/history where needed
```

before implementation.

If current `$[...]` semantics contradict an assumption in this handover, surface the discrepancy and preserve the existing language model unless the requested feature explicitly changes it.

Do not broaden the feature simply because the implementation makes additional behavior easy.

And while implementing it, begin applying the durable-handover principle described at the start:

> If this work establishes a meaningful new architectural rule, bug-family lesson, testing requirement, or operational procedure, add or update the appropriate handover documentation so that a future developer or coding agent does not need this conversation to understand it.

I think that gives Codex exactly what it needs now.

The maintenance point you added is actually quite important philosophically: **we don't want to “complete the handover” and then watch it slowly rot.** The goal should be that five years from now those files contain *five more years of useful institutional knowledge*—not that they remain a shrine to what we believed in August 2026.

And the `$[...]` task is a really nice first test of the whole system. It touches language design, lexical scope, dependencies, paths, incremental builds, security boundaries, tests and documentation, but it's still conceptually small enough that Codex should be able to demonstrate whether it has genuinely absorbed the project's style: **extend values without turning operations into expressions.**
