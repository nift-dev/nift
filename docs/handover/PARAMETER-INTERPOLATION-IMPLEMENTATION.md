# Parameter interpolation implementation

> This is the living source-aware implementation companion to `PARAMETER-INTERPOLATION.md`. Verify remembered symbols and file locations against the current tree, and revise it as the parser architecture changes.

This document is explicitly **implementation-oriented and source-aware**, rather than another language-design handover.

The most important caveat is that while I did work directly on the Nift codebase through the architecture rewrite/hardening iterations, I do **not** want Codex to treat function/file names I remember as infallible. Some of the code moved substantially during those passes. So below I’ll distinguish between things I’m confident describe the implementation architecture we ended up with and things Codex should locate afresh in the current v1.0.42-era tree.

# Nift `$[...]` Parameter Resolution

## Implementation-Oriented Handover

This document supplements the two existing handovers:

```text
1. Nift Project Context Handover
2. `$[...]` Parameter-Resolution Behavioral Handover
```

Those documents establish:

```text
why the feature belongs in Nift
what semantics it should have
what it must not become
```

This document instead addresses:

```text
how I would approach implementing it
where I would look in the current Nift source
which existing parser/value paths I would try to reuse
which implementation shortcuts I would avoid
how I would test the implementation itself
how I would test the externally observable behavior
how I would validate incremental/dependency semantics
```

The repository remains authoritative.

Treat all exact file/function names below as:

```text
high-confidence historical implementation knowledge
        +
implementation guidance
        +
things to verify against the current tree
```

rather than as a substitute for reading the current source.

---

# 1. My starting architectural assumption

The implementation I would aim for is conceptually very small:

```text
existing directive parser
        ↓
extract logical parameter text
        ↓
NEW: resolve/interpolate `$[...]`
        ↓
existing directive-specific validation
        ↓
existing directive implementation
```

I would **not** redesign directive parsing around this feature.

I would try to introduce one reusable operation approximately equivalent to:

```cpp
resolve_parameter(...)
```

or:

```cpp
interpolate_value_expressions(...)
```

that consumes the already-parsed logical string argument and returns:

```text
resolved string
or
controlled parse/value error
```

Then existing directive code continues exactly as before.

That is the central implementation strategy.

---

# 2. First reconnaissance pass

Before changing anything, I would map five current source paths.

Codex should answer these questions from the repository:

```text
A. How is @input(...) recognized and parsed?

B. At what exact point does its quoted/unquoted parameter become a
   normal std::string / string-like value?

C. Where is `$[...]` parsed when it occurs in ordinary template output?

D. Where does `$[...]` resolve metadata / JSON / lexical bindings?

E. Where do @input/@dep/@json/@pathto register their dependency or
   requirement side effects?
```

Do not begin implementation until those five paths are understood.

The ideal implementation point is likely somewhere **after B but before E**.

---

# 3. Historical parser architecture

The Nift parser historically lived primarily around a central parser object/class.

During the rewrite this became substantially simpler than old Nift.

Depending on the exact current checkpoint, look for files/classes named around:

```text
Parser
Parser.h
Parser.cpp
```

or equivalent parser/rendering code.

Other historically important areas include classes/files around:

```text
ProjectInfo
Project
Tracking
WatchList
Json / JSON
Path / filesystem helpers
```

But for this feature I would try very hard to keep the primary implementation localized to the parser/value-resolution layer.

If you find yourself making large changes to:

```text
ProjectInfo
WatchList
build scheduling
filesystem mutation
```

merely to support `$[...]` in arguments, stop and reconsider.

The feature should primarily change **how textual directive arguments are obtained**, not how Nift executes those directives.

---

# 4. Historical directive parsing flow

The general parser model has conceptually been something like:

```text
source character stream
        ↓
ordinary text emitted
        ↓
special marker encountered
        ↓
identify Nift construct
        ↓
parse construct-specific syntax
        ↓
perform construct operation
        ↓
continue
```

For `@...` constructs there has historically been logic equivalent to:

```text
recognize @
read function name
identify known function
parse (...)
dispatch operation
```

An important previous fix made function names consist only of lowercase letters.

That was deliberate because the old parser could consume adjacent HTML syntax.

Do not disturb that lexical boundary as part of this work.

---

# 5. Find the common textual-argument stage

The first thing I would search for is duplicated code resembling:

```cpp
std::string param = ...;
```

around:

```text
input
dep
json
pathto
pathtofile
getenv
ent
```

Questions to answer:

```text
Do they all use one parameter parser?

Does each function parse arguments independently?

Is quote stripping centralized?

Are commas split centrally?

Does the parser return a vector<string>?

Are raw positions returned and interpreted later?
```

The safest integration point depends heavily on this.

---

# 6. Best-case current architecture

The ideal current code would already resemble:

```cpp
auto params = parse_params(...);
```

and then:

```cpp
if (fn == "input") {
    input(params[0]);
}
```

If so, I would likely introduce something conceptually like:

```cpp
auto resolved = resolve_string_param(params[0], context);
```

before calling the operation.

Even better would be argument metadata:

```cpp
resolve_string_argument(...)
parse_identifier_argument(...)
```

so `@json(path, binding)` can treat its two argument positions differently.

---

# 7. More likely architecture

Given Nift’s historical hand-written parser style, there may instead be parser functions that both parse and act:

```cpp
parse_input(...)
parse_dep(...)
parse_json(...)
parse_pathto(...)
```

or one dispatch function containing directive-specific branches.

If so, I would still try to identify a shared helper just after the argument has become logical text.

For example:

```cpp
std::string path = parse_quoted_param(...);

auto resolved = resolve_param_values(path, context);
if (!resolved)
    return error;

return parse_input_resolved(*resolved);
```

Do not restructure the entire parser solely to make all directives share a giant generic dispatch mechanism.

---

# 8. Quote removal should happen before interpolation

My default preferred flow is:

```text
raw source:
    'partials/$[page.layout].html'

parse quoting:
    partials/$[page.layout].html

interpolate:
    partials/sidebar.html

directive validation:
    resolve/check/open partials/sidebar.html
```

not:

```text
interpolate including quote syntax
        ↓
strip quotes later
```

Why?

Because quote delimiters are **Nift parameter syntax**, not part of the logical string value.

The interpolation resolver should ideally receive the same logical string that the old literal-only directive implementation previously received.

That minimizes behavioral change.

---

# 9. Preserve the old quote parser

There were historical bugs involving whitespace around closing quotes:

```text
@input("file.html" )
```

and similar constructs.

Those were fixed.

I would avoid rewriting the quote/argument parser while implementing this feature.

The safest approach is:

```text
old argument parser
    ↓ unchanged
logical parameter string
    ↓
new interpolation layer
```

That dramatically reduces regression surface.

---

# 10. Ordinary `$[...]` parsing is the semantic source of truth

This is perhaps the most important implementation point.

Do not reimplement:

```text
metadata lookup
JSON property lookup
array indexing
loop variable lookup
lexical binding lookup
$loop metadata lookup
type handling
missing-value behavior
```

inside the parameter resolver.

Find the code currently responsible for rendering:

```text
$[...]
```

in normal output.

Then separate, expose or reuse the portion that means:

```text
parse expression
+
resolve value
```

from the portion that means:

```text
append result to rendered output
```

---

# 11. Likely current `$[...]` flow

Conceptually I would expect something close to:

```text
parser sees '$'
    ↓
recognizes '['
    ↓
parses value expression
    ↓
resolves against current context
    ↓
converts result to output text
    ↓
appends to output
```

What we want is to reuse:

```text
parse value expression
        +
resolve against current context
        +
convert according to chosen scalar rules
```

but substitute:

```text
append to parameter buffer
```

for:

```text
append to page output
```

---

# 12. If output rendering and resolution are currently fused

This is one place where a small refactor may be justified.

Suppose current code is essentially:

```cpp
bool Parser::parse_value(...) {
    ...
    output += value;
    ...
}
```

Then I would consider extracting:

```cpp
Result<Value> resolve_value(...);
```

or:

```cpp
Result<std::string> resolve_value_text(...);
```

and changing ordinary rendering to:

```cpp
auto value = resolve_value_text(...);
output += value;
```

Then parameter interpolation can use the same helper:

```cpp
auto value = resolve_value_text(...);
param += value;
```

This would be a **good refactor** because it exposes an existing semantic concept:

```text
resolve value
```

independently from:

```text
where that value is written
```

That matches the language design.

---

# 13. Do not over-generalize the extracted API

I would not turn this into:

```cpp
TemplateExpressionEngine
ValueEvaluator
Runtime
ExecutionContext
```

unless those abstractions already exist naturally.

Something small like:

```cpp
bool resolve_value(..., std::string &out)
```

may fit Nift much better.

The project values local comprehensibility over abstraction theater.

---

# 14. Value representation

Modern Nift now supports:

```text
metadata
JSON
JSON arrays/objects
lexical bindings
loop variables
loop metadata
```

so somewhere there must be a representation or lookup path capable of distinguishing value types.

Codex should identify:

```text
Does Nift use RapidJSON Value references directly?

Does it wrap values?

Are metadata strings handled separately?

Are lexical bindings maps of strings to JSON/value objects?

How are loop object key/value pairs represented?
```

This matters for parameter type rules.

---

# 15. JSON internals

Historically Nift used RapidJSON heavily.

There was a `Json.h`/JSON-related implementation, and later hardening addressed:

```text
duplicate object keys
grammar errors
Unicode
type validation
scope
```

The current repository should be inspected to see whether `$[...]` directly traverses RapidJSON structures or whether the parser now has a higher-level wrapper.

Do not introduce another JSON path traversal implementation.

---

# 16. Lexical scope internals

Later Nift work explicitly fixed lexical JSON scope inside control flow.

That tells us the parser has some current concept of scope stacking/restoration.

Find the structures that implement:

```text
outer bindings
loop binding
nested bindings
scope push
scope pop
```

Parameter interpolation must use exactly the same lookup path as output `$[...]`.

It should not maintain its own binding map.

---

# 17. I would not pass scope around twice

A bad design would be:

```text
normal $[]:
    lookup in parser scope stack

parameter $[]:
    construct separate temporary environment map
```

That creates inevitable drift.

The resolver should ask the same parser/context object:

```text
resolve this expression now
```

regardless of destination.

---

# 18. Likely implementation shape

The implementation I would personally try first is conceptually:

```cpp
bool Parser::resolve_param_values(
    const std::string &input,
    std::string &resolved
)
{
    resolved.clear();

    for (size_t i = 0; i < input.size();) {
        if (starts_value_expression(input, i)) {
            auto expr = parse_value_expression(input, i);

            if (!expr.ok())
                return false;

            std::string value;
            if (!resolve_value_to_text(expr, value))
                return false;

            resolved += value;
            i = expr.end;
        }
        else {
            resolved += input[i++];
        }
    }

    return true;
}
```

This is illustrative only.

The actual implementation should reuse the current expression parser rather than literally working this way if Nift's grammar requires more context.

---

# 19. Better if existing parser can parse from arbitrary source

If `$[...]` parsing already accepts something like:

```cpp
parse_value(source, pos, ...)
```

then reuse that.

Parameter interpolation can maintain:

```text
parameter-local source
parameter-local index
current lexical context
```

without invoking the general `@...` parser.

---

# 20. Do not implement value parsing with `find("]")`

This is one of the strongest warnings I would give.

Modern `$[...]` may involve forms with nested brackets, such as:

```text
$[items[0].name]
```

A naïve:

```cpp
end = input.find(']', start);
```

would stop at the array index's `]`.

If current syntax supports:

```text
nested indexing
quoted keys
escaped content
other bracket-bearing expressions
```

the failure gets worse.

Reuse the real `$[...]` parser.

---

# 21. Parameter interpolation itself should be a single linear pass

Once expression parsing is reused, the outer interpolation scanner can remain simple:

```text
literal span
value expression
literal span
value expression
...
```

I would append contiguous literal spans where convenient rather than character-by-character if the implementation naturally allows it, but clarity matters more than micro-optimization.

---

# 22. Fast path

Nift is performance-sensitive enough that I would consider a fast path:

```cpp
if (param.find("$[") == std::string::npos)
    return existing_literal_path;
```

But I would only add this if it naturally fits.

For tiny parameters, scanning once is almost certainly negligible.

Still, many pages can contain many directives, so avoiding allocation for the overwhelmingly common literal-only case could be worthwhile.

---

# 23. Ideally no allocation for unchanged parameters

A nice architecture would allow:

```text
literal-only argument
→ pass existing string unchanged

interpolated argument
→ create resolved buffer
```

rather than copying every parameter into a new string.

But again:

```text
clear implementation
>
premature micro-optimization
```

Benchmark afterwards.

---

# 24. Directive argument taxonomy

I would explicitly inventory every modern directive and classify each argument position.

For example, conceptually:

| Directive     | Argument                   | Kind              | Interpolate?                             |
| ------------- | -------------------------- | ----------------- | ---------------------------------------- |
| `@input`      | path                       | textual/path      | yes                                      |
| `@dep`        | path                       | textual/path      | yes                                      |
| `@pathto`     | name/path                  | textual           | yes                                      |
| `@pathtofile` | path                       | textual/path      | probably yes if still applicable         |
| `@json`       | source path                | textual/path      | yes                                      |
| `@json`       | binding name               | identifier        | no                                       |
| `@getenv`     | variable name              | textual?          | decide from task scope/current semantics |
| `@ent`        | entity text/name           | textual?          | inspect current semantics                |
| control flow  | expression/binding grammar | structured syntax | not via generic string interpolation     |

The exact table must come from current source.

This is a crucial reconnaissance artifact.

---

# 25. Do not globally transform every parenthesized body

This feature should not become:

```text
before any @directive:
    interpolate entire contents of (...)
```

because arguments can contain grammar, identifiers and separators.

`@for(...)`, `@if(...)`, `@json(..., binding)` are not simply one string.

Interpolation belongs in **semantically textual argument positions**.

---

# 26. `@json` is the clearest example

Conceptually:

```text
@json('data/$[dataset].json', data)
```

contains:

```text
argument 1:
    path string
    → interpolate

argument 2:
    lexical binding identifier
    → parse as identifier
```

Do not turn:

```text
data
```

into a runtime-computed binding name.

Dynamic symbol creation is not part of this task.

---

# 27. `@for` should probably not be touched

Loop grammar already contains:

```text
binding
:
collection expression
by ...
```

That is its own parser.

If values inside its expression grammar already support `$[...]` or value references, preserve that grammar.

Do not route the whole loop header through parameter interpolation.

---

# 28. `@if` similarly should remain its own grammar

Same reasoning.

This task is about textual parameters to operations, not arbitrary rewriting of control-flow syntax.

---

# 29. Dependency registration timing

One thing I would inspect carefully is **when** directives commit dependency/requirement state.

The ideal flow should be:

```text
resolve argument
    ↓
validate operation
    ↓
perform operation
    ↓
record appropriate dependency/requirement as part of normal operation
```

Avoid recording a partial dependency before interpolation has fully succeeded.

---

# 30. Existing transaction model should remain authoritative

Nift has had bugs around dependency sidecars and stale lifecycle state.

Do not create a second “dynamic dependency” mechanism for interpolated paths.

The result:

```text
partials/$[layout].html
        ↓ resolves
partials/sidebar.html
```

should simply enter the **existing literal dependency path**.

This is the cleanest implementation.

---

# 31. Important principle: dynamic source, ordinary resolved dependency

The graph does not need to remember:

```text
this dependency came from interpolation
```

unless current diagnostics/debug information makes that useful.

At build time the actual relationship is:

```text
page
→ partials/sidebar.html
```

The fact that the filename was selected from JSON is relevant because the JSON itself is also a dependency.

So the graph naturally becomes:

```text
page
├── page-data.json
└── partials/sidebar.html
```

If JSON later selects another partial:

```text
page
├── page-data.json
└── partials/hero.html
```

The normal dependency-set refresh should handle it.

---

# 32. This is why dependency-sidecar replacement behavior matters

I would inspect how Nift currently saves dependencies after a successful build.

Ideally it effectively replaces the page's dependency set with the dependencies discovered during that build.

If instead it accumulates forever, dynamic parameter paths will expose stale-dependency bugs quickly.

Previous dependency-sidecar hardening likely already addressed this family.

Still test it explicitly.

---

# 33. Requirements likewise should refresh

For `@pathto`-style requirements:

```text
asset selector = app-a.js
        ↓
requirement: app-a.js
```

then later:

```text
selector = app-b.js
        ↓
requirement should become app-b.js
```

Old requirement retention could produce false rebuild/failure behavior.

---

# 34. Failure transaction

Suppose:

```text
@input('partials/$[layout].html')
```

resolves to a missing file.

The page build should fail under ordinary `@input` semantics.

Question for source inspection:

```text
Does Nift only commit the new dependency set after a successful build?
```

It ideally should.

Otherwise a failed dynamic resolution/build might corrupt the previous known-good dependency state.

This is worth verifying.

---

# 35. Output preservation

Nift's recent hardening favors non-destructive/transactional output behavior.

Test whether a failed build due to a newly resolved missing input leaves the previous successful output intact, according to current contract.

Do not make interpolation-specific output behavior.

---

# 36. Error architecture

I would reuse current error reporting rather than inventing a hierarchy.

There are two logically different failures:

```text
value resolution failure
```

and:

```text
resolved directive argument fails operation
```

For example:

```text
$[page.partial] does not exist
```

versus:

```text
$[page.partial] = "missing.html"
but missing.html does not exist
```

If the current parser can distinguish these cleanly, great.

If not, prioritize correctness over a diagnostic-system refactor.

---

# 37. Source position

One possible implementation challenge is that the interpolation resolver may operate on a copied/unquoted parameter string and therefore lose original source offsets.

If current diagnostics care about exact location, consider passing:

```text
original source offset
```

along with the logical parameter.

Then:

```text
parameter-local offset
+
source base offset
```

can reconstruct positions.

But only do this if current diagnostics already use positions meaningfully.

---

# 38. Escaping

Inspect existing special-character escaping carefully.

Nift historically supports escaping characters such as:

```text
@
$
#
```

in template contexts.

If the logical parameter parser already interprets escaping before the new resolver sees the string, determine whether that is correct.

Potential issue:

```text
user wants literal `$[foo]`
```

inside a parameter.

The resolver must be able to distinguish:

```text
escaped literal
```

from:

```text
value expression
```

using existing syntax.

Do not invent a parameter-specific escape mechanism.

---

# 39. Where escaping should occur

This depends on current parser architecture.

Two possible flows are:

```text
raw
→ unescape
→ interpolate
```

or:

```text
raw
→ interpolate while respecting escapes
→ unescape literals
```

Only one may preserve current semantics correctly.

Codex should inspect how ordinary `$[...]` escaping works and reuse that interpretation.

This is one of the places I would not trust memory.

---

# 40. Built-in metadata

Historically `$[...]` handled page metadata such as things conceptually like:

```text
title
name
output-path
```

Current exact names and forms should come from source/docs.

Parameter interpolation should use the same resolver.

No separate metadata substitution pass.

---

# 41. JSON strings

JSON string values are the most obvious valid interpolation values.

Examples:

```text
layout = "sidebar"
partial = "partials/card.html"
```

These should become ordinary textual fragments.

---

# 42. Non-string JSON values

This is one area I would make Codex explicitly resolve before implementation.

Historical design preference was conservative:

```text
string
→ valid textual parameter

number
boolean
null
array
object
→ likely reject for path-like textual interpolation
```

But current ordinary `$[...]` rendering may already define conversions.

So I would compare:

```text
normal output behavior:
    $[number]
    $[boolean]
    $[null]
```

against desired parameter behavior.

Then write the contract intentionally.

Do not silently let `RapidJSON` serialization decide language semantics.

---

# 43. My preference if current semantics leave this open

If the repository does **not** already establish coercion behavior, I would favor:

```text
strings
→ allowed

built-in textual metadata
→ allowed

number
boolean
null
array
object
→ error in textual directive parameter
```

at least initially.

Why?

Paths, names and dependency strings are clearer when values are explicitly textual.

It also prevents accidental output such as:

```text
data/[object Object].json
```

or implicit serialization rules that later become compatibility obligations.

But this is a design choice Codex should reconcile against the existing `$[...]` behavior and task contract before locking it in.

---

# 44. Empty strings

Explicitly test.

If:

```text
$[path] = ""
```

then the interpolation resolver may succeed as a string, but the outer directive may reject the empty path.

That is good layering:

```text
value resolution:
    valid empty string

@input validation:
    invalid/missing path
```

unless the current outer directive has different semantics.

---

# 45. Strings containing path syntax

Values might contain:

```text
../
/
\
.
spaces
quotes
Unicode
```

Interpolation should not sanitize or normalize them independently.

The **outer path operation** should apply normal path validation.

Otherwise interpolated paths risk taking a different security path than literal paths.

---

# 46. Strings containing Nift syntax

Interesting case:

```json
{
  "partial": "@input('evil.html')"
}
```

Then:

```text
@input($[partial])
```

should resolve the textual parameter to:

```text
@input('evil.html')
```

and then the outer `@input` should interpret that as a filename/string value according to normal path semantics.

It must **not recursively parse the interpolated string as Nift**.

This is an extremely important negative test.

---

# 47. Strings containing `$[...]`

Likewise:

```json
{
  "path": "$[other]"
}
```

and:

```text
@input($[path])
```

should probably resolve **once**:

```text
$[other]
```

as literal resulting text, not recursively interpolate until fixed point.

That preserves simple one-pass semantics.

Unless the behavioral contract explicitly says otherwise:

```text
parameter interpolation = one pass
```

is the clean design.

I strongly recommend testing this.

---

# 48. Why recursive interpolation is dangerous

Otherwise you get questions like:

```text
$[a] → "$[b]"
$[b] → "$[a]"
```

and suddenly need:

```text
recursion depth
cycle detection
multi-pass semantics
```

None of that belongs in this feature.

So:

> Values produce text. Produced text is not reparsed as Nift/value syntax.

This is a very important implementation invariant.

---

# 49. Strings containing quote characters

If JSON value:

```text
foo'bar
```

is inserted into a parameter that was originally quoted:

```text
@input('$[value]')
```

the quote existed only in the source grammar and should already have been consumed.

Therefore the resolved logical parameter can safely contain `'` as data.

Do **not** reparse the resulting parameter as quoted Nift syntax.

This is another reason interpolation belongs after quote parsing.

---

# 50. This provides a clean safety model

The stages become:

```text
source grammar
    ↓
logical parameter string
    ↓
value substitution
    ↓
plain final string
    ↓
outer operation
```

No stage sends generated text back upward into Nift grammar.

That is exactly what we want.

---

# Part II — How I Would Test the Implementation

This section is intentionally extensive because the testing strategy is as important as the implementation.

---

# 51. Testing should have four layers

I would use:

```text
1. focused implementation-level C++ tests
2. black-box contract/regression tests
3. interaction/incremental tests
4. whole-project / sanitizer / benchmark validation
```

Not every edge case needs all four layers.

The layers catch different failures.

---

# 52. Layer 1 — direct resolver tests

If the implementation introduces or exposes a helper such as:

```cpp
resolve_param_values(...)
```

I would add focused C++ tests for it.

These tests should not create entire temporary Nift projects if unnecessary.

Test the small semantic unit.

Examples:

```text
literal only
single entire-value interpolation
prefix + value
value + suffix
multiple values
adjacent values
Unicode text
empty literal spans
escaped dollar
malformed expression
missing value
wrong type
```

This gives excellent failure locality.

---

# 53. Direct test: literal unchanged

Input:

```text
partials/header.html
```

Expected:

```text
partials/header.html
```

Also verify the fast/non-interpolated path if implementation exposes that distinction.

---

# 54. Direct test: entire value

Given:

```text
page.partial = "partials/card.html"
```

Input:

```text
$[page.partial]
```

Expected:

```text
partials/card.html
```

---

# 55. Direct test: mixed

Input:

```text
partials/$[page.layout].html
```

Value:

```text
sidebar
```

Expected:

```text
partials/sidebar.html
```

---

# 56. Direct test: multiple values

Input:

```text
generated/$[release]/$[dataset].json
```

Expected:

```text
generated/v4/products.json
```

---

# 57. Direct test: adjacent expressions

Input:

```text
$[a]$[b]
```

Expected:

```text
foobar
```

No hidden whitespace.

---

# 58. Direct test: one-pass semantics

Given:

```text
a = "$[b]"
b = "thing"
```

Input:

```text
$[a]
```

Expected final parameter:

```text
$[b]
```

not:

```text
thing
```

unless the current contract explicitly chooses recursion.

I would strongly prefer the one-pass expectation.

---

# 59. Direct test: injected `@` syntax remains data

Given:

```text
a = "@input('something')"
```

Resolve:

```text
$[a]
```

Expected textual result:

```text
@input('something')
```

No parser operation should execute.

This directly protects the values-versus-operations boundary.

---

# 60. Direct test: nested indexing

Use the most complex currently valid ordinary `$[...]` expression involving nested array/object access.

For example conceptually:

```text
$[site.sections[0].template]
```

Ensure the interpolation scanner hands the whole expression to the existing value parser.

This catches naïve closing-bracket logic.

---

# 61. Direct test: lexical scope

Create current-scope bindings and verify interpolation resolves the innermost binding exactly as normal output `$[...]` would.

---

# 62. Direct test: `$loop.*`

If implementation-level harness can cheaply construct loop scope, test nested loop metadata.

Otherwise leave this for black-box integration tests.

---

# 63. Direct test: malformed value syntax

Examples according to current grammar:

```text
$[
$[]
$[foo
$[foo]]
```

Verify:

```text
controlled failure
no out-of-bounds access
no partial output
```

---

# 64. Direct test: literal escape

Use current syntax for escaping `$`.

Verify escaped `$[...]` remains literal.

---

# 65. Direct test: non-string values

Once contract is established, test each supported type explicitly:

```text
string
number
boolean
null
array
object
```

Do not rely on one generic “wrong type” test.

This prevents later accidental coercion changes.

---

# 66. Direct test: no side effects

This may be difficult at the helper layer.

If feasible, verify resolver does not alter:

```text
dependency set
requirement set
output buffer
scope stack
```

Resolving a parameter should be pure with respect to those structures.

---

# 67. Layer 2 — black-box contract tests

These should go in the implementation-independent Nift regression suite.

The suite should invoke a Nift executable against real project files.

These tests define user-visible behavior.

---

# 68. Contract test project pattern

Create a minimal project using normal Nift commands/config.

Prefer small fixture setup.

A test should answer one thing clearly.

Avoid one enormous “parameter interpolation works” test that exercises 25 cases at once.

---

# 69. Contract: `@input` whole-value

Data:

```json
{
  "partial": "partials/hello.html"
}
```

Template:

```text
@json(...)
@input($[data.partial])
```

Expected generated content includes partial output.

Use exact current JSON syntax.

---

# 70. Contract: mixed `@input`

```text
@input('partials/$[data.layout].html')
```

Expected correct included file.

---

# 71. Contract: multiple expressions

Use a path requiring two values.

---

# 72. Contract: loop binding

Something conceptually:

```text
@for(item : items){
    @input('partials/$[item.kind].html')
}
```

Ensure each iteration can resolve a different partial.

This is a particularly valuable real use case.

---

# 73. Contract: nested loop shadowing

Outer loop and inner loop reuse the same binding name or otherwise exercise lexical shadowing.

Verify the parameter resolves exactly like ordinary `$[...]`.

---

# 74. Contract: condition scope

If bindings can be introduced within conditions/current syntax, test resolution in correct branch/scope.

---

# 75. Contract: dynamic `@dep`

Resolve a dependency path from a value.

Build should succeed.

Changing the resolved dependency should trigger rebuild.

---

# 76. Contract: dynamic `@json`

If included in task scope:

```text
selector
→ chooses JSON source
```

Verify loaded data comes from resolved file.

---

# 77. Contract: dynamic `@pathto`

Verify textual path/name generation and requirement semantics.

Use current exact semantics.

---

# 78. Contract: traversal through interpolation

Value:

```text
../outside.html
```

Outer operation:

```text
@input($[path])
```

Expected result must match literal traversal rejection.

This proves interpolation does not bypass path safety.

---

# 79. Contract: nonexistent resolved input

Expected normal controlled failure.

Also verify exit status.

---

# 80. Contract: missing value

Expected value-resolution error/failure.

---

# 81. Contract: wrong type

According to chosen semantics.

---

# 82. Contract: inserted Nift syntax is not executed

This is vital.

JSON:

```json
{
  "path": "@input('secret.html')"
}
```

Use:

```text
@input($[data.path])
```

The outer directive should treat the resolved string as its filename.

It must not execute nested `@input`.

Depending on path rules, the build should likely fail because that filename does not exist.

The critical assertion is that `secret.html` is not included by recursively parsing the value.

---

# 83. Contract: inserted `$[...]` is not recursively resolved

Same concept.

This is worth permanently protecting.

---

# 84. Contract: literal existing parameters unchanged

Keep representative old tests around:

```text
@input('header.html')
@dep('data.json')
@pathto('docs')
```

This is mostly covered by the existing suite, but new focused comparison tests can make feature regressions easier to diagnose.

---

# 85. Layer 3 — dependency/incremental interaction tests

This is the most important test layer beyond basic syntax.

A feature can render correctly on a clean build and still break Nift's incremental model.

---

# 86. Dynamic `@input`: initial A

Set selector:

```text
partial = "a.html"
```

Build.

Expected output uses A.

Record baseline.

---

# 87. Change A contents

Build updated.

Expected page rebuilds and output reflects changed A.

This proves resolved input is recorded normally.

---

# 88. Change selector A → B

Modify JSON/value source:

```text
partial = "b.html"
```

Build updated.

Expected:

```text
page rebuilds
output now uses B
```

---

# 89. Change old A after selector moved to B

Now edit:

```text
a.html
```

Run updated build.

Expected:

```text
page should not rebuild solely because A remains stale in dependency state
```

This is an extremely important regression.

It tests dependency-set replacement.

---

# 90. Change current B

Expected page rebuild.

---

# 91. Delete current B

Expected page becomes invalid/fails under normal input semantics.

Verify prior successful output behavior according to current transactional contract.

---

# 92. Recreate B

Expected recovery.

---

# 93. Selector changes to missing C

Expected rebuild attempt and controlled failure.

Important question:

```text
Does failed build preserve previous dependency state/output correctly?
```

Test according to existing contract.

---

# 94. Fix selector back to B

Expected clean recovery without needing manual state deletion.

---

# 95. Dynamic `@dep` lifecycle

Repeat the A/B transition pattern for explicit dependency paths.

The output may not change directly, so assertions should focus on rebuild decisions.

---

# 96. Dynamic requirement lifecycle

For `@pathto`/concrete requirement:

```text
selector = asset-a.js
build
selector = asset-b.js
build
delete asset-a.js
```

If A is no longer required, its deletion should not invalidate the page.

Then delete current B and ensure it does.

This is the requirement analogue of stale dependency testing.

---

# 97. JSON source selector lifecycle

If dynamic `@json` is supported:

```text
selector source
    ↓
JSON A
```

then switch to B.

Verify:

```text
A change no longer matters
B change matters
selector source itself remains dependency
```

---

# 98. Shared dependency scenario

Use many pages that resolve to the same dynamic partial.

Then change that partial.

Verify all appropriate pages rebuild.

This tests integration with broad dependency invalidation.

---

# 99. Divergent selector scenario

Many pages use:

```text
$[page.layout]
```

with different layouts.

Change one layout partial.

Only pages resolving to that partial should rebuild where the dependency model allows that precision.

This could be an excellent demonstration of Nift's incremental architecture.

---

# 100. Same-second selector edit

Because Nift has historical same-second/sub-second concerns, consider one deterministic test where the selector JSON/value source changes within the same second using controlled mtimes.

Do this only if the relevant update mode depends on mtime and existing test helpers already support it.

Reuse deterministic mtime helpers rather than sleeps.

---

# 101. Hash mode

Run dynamic parameter dependency transitions in:

```text
modified
hash
hybrid
```

modes where those remain current supported concepts.

You do not necessarily need the full matrix for every feature case, but at least one dynamic dependency lifecycle should be checked across incremental modes.

---

# 102. Watch mode

A particularly valuable scenario:

```text
watch/build-auto running
selector JSON changes A → B
```

Verify Nift recognizes the selector source change, rebuilds, and updates the resolved dependency set.

Then modify B.

This exercises the feature under Nift's real continuous workflow.

---

# 103. Layer 4 — parser adversarial tests

The feature creates a new context in which `$[...]` is recognized.

That deserves boundary testing.

---

# 104. Very beginning/end of parameter

```text
'$[x]'
'$[x]suffix'
'prefix$[x]'
```

---

# 105. Adjacent punctuation

```text
'$[x].html'
'$[dir]/$[file]'
'x-$[a]-$[b]'
```

---

# 106. Unicode

Value contains Unicode.

Literal surrounding text contains Unicode.

No corruption.

---

# 107. Long value

Use a reasonably long string to catch accidental fixed buffers/truncation.

Not enormous unless fuzz/stress testing suggests a need.

---

# 108. Many expressions

A parameter with many interpolations can catch indexing mistakes:

```text
$[a]-$[b]-$[c]-...
```

---

# 109. Escaped interpolation marker

Use exact current Nift escape syntax.

---

# 110. Invalid expression followed by valid Nift content

Ensure parser fails cleanly and does not corrupt its position/state.

---

# 111. Valid parameter followed immediately by HTML syntax

Given historical `@content<` bugs, test something like:

```text
@input('$[partial]')<div>...
```

according to current formatting rules.

The new scanner should not alter outer function termination.

---

# 112. CSS/JS nearby

Ensure ordinary `$`/`@` syntax in adjacent emitted CSS/JS remains unaffected.

Do not over-expand scope, but add a couple of regressions.

---

# 113. C++ sanitizer testing

After implementation, compile/run Nift with:

```text
ASan
UBSan
```

using whatever exact repository-supported commands/flags current source establishes.

The interpolation implementation is especially susceptible to:

```text
off-by-one
out-of-bounds
dangling string_view
use-after-reallocation
incorrect substring length
```

so sanitizers are very relevant.

---

# 114. TSan

Not necessary merely because interpolation exists.

Use TSan if the implementation changes shared parser/build state or if concurrent builds expose the resolver through shared mutable structures.

Ideally it should reuse immutable/per-parser scope and require no new shared state.

---

# 115. Fuzzing

If Nift already has or later gains parser fuzzing, add parameter interpolation to the corpus.

Useful properties:

```text
never crash
never hang
bounded memory
controlled malformed-input error
```

A dedicated future fuzz target might call the interpolation helper directly.

Not required to land the initial feature unless fuzz infrastructure is already easy to use.

---

# 116. Differential equivalence tests

This feature has a useful equivalence property:

```text
dynamic template
```

after resolution should behave the same as:

```text
literal template with resolved value substituted manually
```

This can generate strong tests.

For example:

Dynamic:

```text
@input('partials/$[layout].html')
```

with:

```text
layout = sidebar
```

should produce identical result/dependency behavior to:

```text
@input('partials/sidebar.html')
```

except that the dynamic case additionally depends on the source of `layout`.

This is a powerful testing oracle.

---

# 117. Use equivalence aggressively

For each directive:

```text
dynamic argument
        versus
same argument hardcoded after resolution
```

Compare:

```text
output
failure status
resolved target
outer directive behavior
```

This reduces ambiguity.

---

# 118. Performance testing

I would benchmark before and after implementation on:

```text
normal current Nift benchmark suite
10k fixture if present
no-op incremental
```

Then possibly create a synthetic parameter-heavy fixture if the change appears measurable.

---

# 119. Literal-only fast-path benchmark

Important because most existing projects may not use the feature immediately.

We want:

```text
existing literal parameter workload
```

to remain effectively unchanged.

If every literal directive now incurs expensive scope/expression parsing, that is a poor implementation.

---

# 120. Interpolated workload benchmark

Create enough directives to measure interpolation cost without pretending it represents normal sites.

The goal is to catch accidental:

```text
O(parameter_length²)
```

behavior or repeated whole-string reparsing.

---

# 121. Memory benchmark

This feature should not meaningfully affect Nift's project-level peak RSS.

If it does, investigate why.

A few temporary parameter strings should be transient and tiny.

---

# 122. Website self-build

Build the actual Nift website with the candidate binary.

This catches compatibility issues.

Then optionally introduce one genuine parameter-interpolation use into a branch/example only if documentation naturally benefits from using the feature.

Do not gratuitously rewrite the whole website to dogfood it.

---

# 123. Example project

After technical validation, create a realistic example such as:

```text
page JSON:
{
    "layout": "feature"
}
```

and:

```text
@input('partials/$[page.layout].html')
```

This is useful for documentation and AI-context testing.

---

# 124. AI-DX test

Give Codex/current agent a small task using the new feature **from documentation only**, without explaining internal behavior in the prompt.

See whether it uses the syntax correctly.

If it guesses nested `@...` operations, inspect whether docs/errors help it recover.

This is optional but interesting evidence.

---

# Part III — Files I Would Expect to Inspect

Exact current names must be verified.

Historically/reasonably, I would search in this order:

```text
Parser.h / Parser.cpp
```

for:

```text
@ dispatch
argument parsing
$[...] handling
scope
control flow
```

Then:

```text
Json.h / JSON-related files
```

for:

```text
value representation
lookup
type handling
```

Then project/build state files such as:

```text
ProjectInfo*
Project*
```

for:

```text
dependency registration
requirement registration
sidecar/state refresh
```

Then tests:

```text
tests/
```

especially files/modules with names involving:

```text
parser
json
control
scope
dependency
input
pathto
dep
incremental
```

Then external suite:

```text
nift-regression-suite/
legacy/
contract/
benchmarks/
```

using actual current layout.

---

# 125. Search strategy I would use

Rather than assuming filenames, grep for literal directive names:

```text
"input"
"pathto"
"dep"
"json"
```

and parser markers:

```text
"$["
```

Then map call sites.

Also search for historical helper patterns such as:

```text
parse_fn
parse_fn_name
parse_param
get_param
resolve
binding
scope
dependencies
requirements
```

Exact symbol names may differ.

---

# 126. Find the normal `$[...]` output path first

I would specifically trace one tiny fixture mentally through code:

```text
<title>$[title]</title>
```

Find:

```text
where '$' is recognized
where title lookup occurs
where string conversion occurs
where output append happens
```

Then trace:

```text
@input('header.html')
```

Find:

```text
where parameter logical string emerges
where input execution begins
```

The intersection between these two paths is almost certainly the implementation point.

---

# 127. I would probably prototype in one directive first

Before wiring every textual parameter position, I might implement/reuse the resolver for:

```text
@input
```

only in a local branch/working change.

Why `@input`?

Because it gives immediate validation of:

```text
string resolution
filesystem target
dependency recording
incremental lifecycle
```

Once architecture proves clean, generalize to other textual directives.

Do not ship `@input` alone if the intended contract covers the broader parameter class.

This is an implementation tactic, not a user-facing partial feature.

---

# 128. Then generalize through a shared resolver

Once `@input` proves the approach:

```text
@dep
@pathto
@json source path
other textual arguments
```

should call the same value interpolation helper.

No copy/paste parser loops.

---

# 129. Avoid a giant generic directive descriptor system

Do not respond to the argument taxonomy problem by creating something like:

```cpp
DirectiveDefinition {
    vector<ArgumentSpec>,
    callbacks,
    coercion rules,
    runtime handlers,
    ...
}
```

unless current architecture already points there.

A few explicit calls:

```cpp
resolve_text_param(path);
```

inside each directive handler may be much clearer.

---

# 130. Expected change size

My expectation is that a good implementation should be fairly modest.

Likely:

```text
one reusable interpolation/value-resolution helper
small refactor exposing ordinary $[] resolution if needed
small changes at textual directive argument call sites
focused C++ tests
contract tests
incremental lifecycle tests
documentation
```

If the feature requires a large parser rewrite, I would stop and investigate why.

---

# 131. Tempting shortcut #1: recursively parse parameter as Nift

Do not.

This is the biggest architectural trap.

---

# 132. Tempting shortcut #2: regex substitution

Something like:

```cpp
std::regex(R"(\$\[(.*?)\])")
```

would likely fail around:

```text
nested array indexing
escaping
grammar evolution
performance
diagnostics
```

Do not create a second parser with regex.

---

# 133. Tempting shortcut #3: naïve `find("$[")` + `find("]")`

Same issue.

Finding the opening marker is fine for a fast scan.

Parsing the expression boundary must use the real grammar.

---

# 134. Tempting shortcut #4: stringify every JSON type

This silently creates language semantics.

Decide type behavior intentionally.

---

# 135. Tempting shortcut #5: interpolate every directive argument automatically

Binding identifiers and control-flow grammar are not string parameters.

Classify arguments.

---

# 136. Tempting shortcut #6: reparse interpolated output

Do not recursively interpret produced text.

One pass.

Values are data.

---

# 137. Tempting shortcut #7: bypass existing path validation

The final string must enter the same literal operation path.

---

# 138. Tempting shortcut #8: special-case dynamic dependency storage

Do not.

Resolved targets should be normal dependencies/requirements.

---

# 139. Tempting shortcut #9: commit dependency state before successful build

Preserve current transaction semantics.

---

# 140. Tempting shortcut #10: simultaneously clean up the whole parser

This is a focused feature.

Unless a small extraction is necessary, avoid broad unrelated parser refactoring.

---

# Part IV — Historical Things I Am Confident Matter

## 141. Function-name parser

Lowercase-only recognition is deliberate.

Do not alter it.

---

# 142. Backtick behavior

Backticks are deliberately not Nift quotes.

Do not expand quoting.

---

# 143. CSS `@` pass-through

Unknown ordinary `@...` syntax must continue surviving.

---

# 144. JSON lexical scope

Control-flow scope correctness has already required fixes.

Parameter interpolation must use the existing scope mechanism.

---

# 145. Dependency sidecar lifecycle

This area has already had subtle bugs.

Dynamic target transitions make it an especially important regression surface.

---

# 146. Sub-second timestamps

Do not weaken current time precision.

---

# 147. 64-bit hashing

Do not alter hashing as part of this feature.

---

# 148. Path traversal/collision protection

Interpolated paths must not bypass it.

---

# 149. Non-destructive mutation/output behavior

Preserve existing failure transaction rules.

---

# 150. Performance architecture

Do not introduce large long-lived caches/maps merely for string interpolation.

---

# Part V — Things I Am Less Certain About

Codex should explicitly verify these rather than relying on this handover.

## 151. Exact parser function names

I remember the parser architecture and several historical symbols, but current v1.0.42 names may have changed during the rewrite.

---

# 152. Exact JSON value wrapper

I do not want to assert whether modern `$[...]` now resolves directly from RapidJSON values or through a wrapper without inspecting the tree.

---

# 153. Exact dependency/requirement container names

The semantics are well established; current class/member names should come from source.

---

# 154. Exact scalar coercion rules

This must be derived from current `$[...]` behavior and deliberately specified.

---

# 155. Which directives currently share argument parsing

Inspect source.

---

# 156. Exact sanitizer commands

Use current Makefile/scripts.

Historically we ran normal C++ builds and substantial adversarial tests, but I do not want to invent a command like:

```text
make asan
```

unless the repository actually has it now.

---

# 157. Exact benchmark commands

Likewise use the current `benchmarks/`/suite tooling.

Historical 10k scripts changed over time.

---

# 158. Exact website build command

Codex already reconciled the current website branch/project architecture better than conversational memory.

Use current repository instructions.

---

# Part VI — Proposed Development Sequence

## 159. Phase A — understand

Produce a short implementation map:

```text
Parameter parsing:
    file/function

Value parsing:
    file/function

Value resolution:
    file/function

Scope:
    structure/function

@input:
    file/function

@dep:
    file/function

@json path:
    file/function

@pathto:
    file/function

Dependency registration:
    file/function

Requirement registration:
    file/function
```

This is the single best first artifact.

---

# 160. Phase B — freeze baseline

Run:

```text
clean build
local tests
external contract suite
```

and record results.

Do not begin feature debugging with an unknown baseline.

---

# 161. Phase C — capture existing type semantics

Create/inspect tests for ordinary output:

```text
$[string]
$[number]
$[boolean]
$[null]
$[array]
$[object]
```

and missing values.

This lets us decide parameter semantics from evidence.

---

# 162. Phase D — add failing contract examples

Start with:

```text
whole parameter
mixed parameter
multiple values
scope
```

Confirm they fail because the feature is absent.

---

# 163. Phase E — implement side-effect-free resolver

Prefer smallest extraction/refactor needed to reuse `$[...]`.

---

# 164. Phase F — implement `@input` locally

Use as architecture proving ground.

Run focused tests.

---

# 165. Phase G — wire all intended textual argument positions

Based on directive taxonomy.

---

# 166. Phase H — dependency lifecycle

Run A → B dynamic target transitions.

Do this before declaring syntax complete.

---

# 167. Phase I — adversarial negative cases

Especially:

```text
inserted @ directive
inserted $[] recursion
traversal
wrong type
malformed expression
escaped marker
```

---

# 168. Phase J — full validation

```text
all local tests
external contract suite
sanitizers
website
performance
memory if relevant
```

---

# 169. Phase K — documentation and handovers

Update:

```text
template-language docs
examples
AI context if relevant
HANDOVER/DECISIONS/TESTING where the new architectural rule belongs
```

Remember the earlier maintenance requirement:

> Handover documents are living project infrastructure.

This feature should update the decision ledger to make the values-versus-operations distinction durable if that rule is not already represented.

---

# 170. Likely decision-ledger addition

I would expect something like:

## Parameter interpolation

**Status:** SETTLED after implementation

Directive textual parameters may interpolate existing `$[...]` values.

Interpolation is a single, side-effect-free value-resolution pass.

Interpolated text is not reparsed as Nift.

Arbitrary `@...` operations are not valid parameter expressions.

### Rationale

This allows existing Nift values to compose with operations without turning operations themselves into value-returning expressions or introducing general nested template execution.

### Revisit if

A substantial real-world use case cannot be expressed cleanly using the value model and demonstrates the need for a richer, still-coherent expression system.

That would preserve the architectural lesson extremely well.

---

# 171. Testing-document addition

Likely historical lesson:

```text
Parameter interpolation
→ values may become more composable without making operations composable
→ tests must explicitly ensure interpolated strings are not reparsed as Nift
```

This is exactly the kind of bug-family lesson worth keeping.

---

# Part VII — What “Done” Means to Me

I would not consider this feature complete merely because:

```text
@input('$[path]')
```

works once.

I would consider it complete when:

```text
syntax works
scope works
types are deliberately specified
escaping works
one-pass semantics are explicit
nested operations remain impossible
paths remain safe
dependencies update dynamically
requirements update dynamically
old dependencies disappear
incremental modes remain correct
watch remains correct where relevant
failures preserve state/output correctly
ordinary literal params remain unchanged
contract suite specifies the behavior
sanitizers are clean
performance is effectively unchanged
Nift website still builds
docs explain the model accurately
handover/decision docs capture the new durable rule
```

That is the standard I would apply.

---

# Final implementation recommendation

If I were personally sitting in front of the current Nift source, the first code change I would *hope* to make is not:

```cpp
@input-specific string replacement
```

It would be the smallest extraction necessary to express:

```text
resolve this existing `$[...]` expression to a value/text
without emitting it anywhere
```

Once Nift has that primitive, parameter interpolation becomes straightforward:

```text
scan logical parameter
        ↓
literal → append
$[...] → existing resolver → append
        ↓
resolved string
        ↓
existing directive
```

That architecture mirrors the language model exactly:

```text
values
    can be resolved in more places

operations
    remain operations
```

If the current source already exposes value resolution independently from output emission, even better: this feature may be surprisingly small.

If it does not, a modest extraction there is the refactor I would consider justified.

Everything else—the dependency graph, `@input`, `@dep`, `@json`, `@pathto`, path validation, transaction handling—should ideally see only the final resolved string and behave exactly as it already does.

That is both the simplest implementation and the one least likely to undermine the architecture we spent so much time hardening.

That is the implementation handover I’d want Codex to have.

The **single most important additional test I’d emphasize beyond the earlier behavioral handover is the one-pass/non-recursive case**. If a JSON value literally contains `@input(...)` or `$[other]`, parameter substitution should produce those characters as data—not send them back through Nift. That test very directly prevents a future implementation from accidentally drifting toward the “mini-template inside every parameter” model we rejected.

And the second thing I'd make Codex prove before calling it finished is the **A → B dependency lifecycle**:

```text
selector → partial A
build

selector → partial B
build

change A
→ page should no longer care

change B
→ page should care
```

If that works across the relevant incremental modes, then we're not just adding convenient string interpolation; we're adding it in a way that genuinely respects Nift's build graph.

I think Codex now has all three layers it asked for:

```text
complete Nift mental model
        +
exact behavioral contract
        +
implementation/testing approach
```

The right thing for it to do now is read the actual current parser and tell us **where its repository evidence agrees or disagrees with my implementation recollection before it changes anything**.
