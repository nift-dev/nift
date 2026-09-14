# Minify++ project context

## Identity and history

Minify++ began under the working name Sift and was renamed without changing its
focused role. The display name evokes C++ and “more minification,” while the CLI
remains the obvious `minify`.

It originated alongside Nift but is deliberately standalone. Independent source,
tests, API, CLI, benchmarks, website, and release maturity make both projects
cleaner. Shared engineering values do not make it a Nift plugin.

## Product philosophy

Minify++ accepts web artifacts and emits conservative smaller artifacts. It
should remain small, native, predictable, composable, and easy to invoke. Do not
expand toward bundling, compilation, source transformation pipelines, or package
orchestration without a deliberate product decision and compelling evidence.

Minifier defects are unusually dangerous: output can look valid and smaller while
silently changing meaning. Correctness therefore outranks heroic byte savings.

## Format-specific risks

### HTML

HTML contains markup, text nodes, attributes, comments, raw-text elements,
preformatted contexts, entities, and embedded languages. Whitespace between
inline elements can be visible. `pre`, `textarea`, `script`, and `style` require
context-sensitive handling. A generic tag-whitespace stripper is not adequate.

### CSS

Whitespace/comments can affect token boundaries, selectors, strings, URLs,
functions, numbers/units, `calc()`, and custom properties. Custom-property values
are interpreted later and deserve conservative treatment. Modern CSS corpus
coverage matters.

### JavaScript

High-risk areas include automatic semicolon insertion, token joining, regular
expression versus division, comments, strings, template literals, escapes,
numeric syntax, operators, optional chaining, modules, Unicode, and evolving
ECMAScript syntax. Newline removal is never globally safe. Side effects and
runtime comparison are stronger than plausible-looking output.

### JSX/TSX syntax

Angle brackets can describe markup, generic arrows, assertions, and type syntax.
The retained fix distinguishing `<T,>(x:T) => ...` from JSX roots illustrates why
context and adversarial neighbors matter. Minify++ is not a TypeScript compiler;
it conservatively protects/minifies syntax it recognizes.

### JSON

JSON is structurally validatable. Tests can compare parsed structure, require
malformed input rejection, and exercise strings/numbers/nesting/Unicode.

### XML/SVG

The current claim is deliberately conservative: Minify++ protects syntax it
understands and is not a complete validating XML parser. Do not let tests or
website wording imply a stronger contract accidentally.

## Public behavior boundaries

The library accepts strings and produces result/error text. File naming,
non-destructive sibling `.min.*` output, and explicit `--in-place` overwrite
belong to the CLI/caller boundary. Destructive behavior must remain explicit.

Malformed-input behavior should be deliberate by format: controlled error,
conservative preservation, or documented best effort. Undefined behavior, hangs,
or silent unsafe rewriting are never acceptable.

## Evidence culture

Use a mixture of focused golden cases, runtime semantic programs, generated
matrices, idempotence where contractually appropriate, malformed cases,
real-world corpora, sanitizers, and performance/output-size measurements. Another
minifier is useful for discovery, not automatically the specification.

The strongest question for every change is:

> Can this transformation change meaning while producing output that still looks
> valid?

## Performance

A native tool should provide low startup cost, strong throughput, and controlled
memory across many small files and large assets. Compare equal workloads and
include output size/semantic aggressiveness. A faster unsafe candidate or a
smaller semantically broken candidate is not an improvement.

