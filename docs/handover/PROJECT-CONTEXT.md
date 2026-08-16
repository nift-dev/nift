# Nift project context

## Purpose

This document preserves the historical and product reasoning behind modern Nift.
It is institutional memory, not an executable specification. Current source,
tests, documentation, and Git history remain authoritative for current details.

Status vocabulary used here:

- **SETTLED**: deliberately chosen; do not revisit casually.
- **CURRENT**: present implementation/contract; may evolve deliberately.
- **STRONG PREFERENCE**: architectural direction supported by experience.
- **HYPOTHESIS**: plausible and partly evidenced; continue testing.
- **UNRESOLVED**: deliberately open.
- **REJECTED**: considered and moved away from.
- **FUTURE POSSIBILITY**: interesting, not planned work.

## Identity and positioning

**SETTLED:** Nift is a **website generator**. “Website build system” or
“dependency-aware generation layer” can also be accurate in context. Avoid using
“static site generator” as the default identity because it implies that generated
sites cannot contain runtime applications or participate in dynamic systems.

Nift can generate a marketing site, documentation, a portfolio, a frontend shell
for an authenticated backend, or documents containing React/Vue/Svelte/Web
Component islands. The browser/runtime architecture remains outside Nift.

Useful formulations include:

> Keep your HTML. Keep your tools. Stop repeating yourself.

> Nift provides the glue without trying to become the universe.

These are architectural descriptions, not merely marketing slogans.

## Historical evolution

Older Nift included or experimented with LuaJIT, ExprTk, system execution,
scripting, hooks, and broader orchestration. The major simplification removed
that machinery deliberately. The conclusion was not that those capabilities were
impossible, but that Nift was better when it did not own them.

The stripped core centered on a very small composition model including
`@content`, `@input`, `@pathto`, metadata values, and a few explicit operations.
It remained capable of building substantial sites and became dramatically
smaller and easier to reason about. This changed the project's self-understanding:
capability did not require accumulating a large Nift-specific language.

Modern structured rendering later added `@json`, JSON Schema, `@for`, object
iteration, `@if`, stable sorting, lexical scope, and `loop.*` metadata. These are
accepted because structured data plus a static template is directly within a
website generator's job. They are not an invitation to grow mutable variables,
general functions, arbitrary arithmetic, shell execution, or a template runtime.

## Language boundary

The language should remain a build/rendering language rather than general
scripting. A useful model is:

```text
operations: @input, @dep, @json, @pathto, ...
values:     $[...]
```

The parameter-interpolation design currently under development follows this
boundary: values may become usable in textual operation arguments, while
operations do not become nestable value-returning expressions.

Lexical scope is contractual rather than incidental parser state. JSON bindings,
loop bindings, nested shadowing, and `loop.*` context must restore correctly.
Stable sorting is semantically meaningful because generated output should remain
deterministic when sort keys compare equal.

Small positive lexical rules are preferred over permissive parsing plus repair
cases. Lowercase ASCII function-name recognition and single/double quote syntax
are deliberate examples. Backticks are not Nift quotes. Unknown web-language
`@` syntax such as CSS `@media`, `@supports`, and `@keyframes` should survive.

## Composition philosophy

Nift should compose with external tools through files, paths, and ordinary build
scripts rather than acquiring framework-specific directives. Nift does not need
`@react`, `@vite`, or package orchestration to work with React/Vite.

The React-islands experiment demonstrated a clean split:

```text
Nift: document, navigation, SEO/content, templates, asset paths
React: genuinely stateful interactive islands
Vite/npm: React compilation and project orchestration
```

Stable external asset paths are requirements: asset bytes can change without the
HTML URL changing, so Nift need not rebuild the page solely for those bytes. A
hashed manifest changes the relationship because manifest data determines the
generated URL and therefore the page bytes.

## Dependencies, requirements, and incrementality

A content dependency means a resource's state can affect generated output bytes.
A requirement means generated output assumes a path exists. Missing required
resources matter; changes to their bytes may not.

Incremental correctness is a defining product property. A missed rebuild is a
correctness defect; an unnecessary rebuild is a performance defect. Prefer
conservative correctness first, then optimize with evidence.

JSON sources and schemas are dependencies. Dynamic executed render paths discover
their actual dependencies during a build. Successful metadata should represent
the relationships from that successful build rather than accumulate stale paths.

## Filesystem and state philosophy

Persisted project state is untrusted input even when Nift originally wrote it.
Files can be truncated, edited, copied across versions, or structurally valid
with wrong types. Validate containers, keys, types, ranges, and relationships.
Malformed state should produce a useful non-zero failure, never an unchecked
RapidJSON assertion or partial corruption.

Mutations should validate complete requested operations before committing.
Filesystem containment must be semantic and account for normalization, `..`,
symlinks, missing components, and platform path behavior. String-prefix checks
are not containment checks.

Failed builds should preserve the last known-good output and metadata where the
current transaction model promises that behavior.

## Performance history

Performance is a product value because near-instant feedback changes how often
humans and agents validate their work. Historical 10,000-page fixtures showed
excellent full and incremental behavior, but checkpoint numbers require fixture,
machine, mode, and version context.

An important optimization arc was:

```text
quadratic collision validation
→ hash-indexed validation (fast, higher temporary memory)
→ compact staged vector/sort validation (fast and memory controlled)
```

The lesson is broader than one container choice: measure CPU, peak memory,
representation, and lifetime. Do not improve benchmarks by skipping safety work.
`PERFORMANCE.md` records current retained evidence.

## Testing culture

The most important development habit is:

```text
Get green.
Read the implementation anyway.
Find an assumption nobody tested.
Construct the smallest hostile case.
Prove whether it fails.
Preserve the reproducer.
Fix the family, not only the symptom.
Run everything again.
Measure when performance or memory may have changed.
```

Bug-family coverage matters more than raw test count. Deterministic fixtures beat
timing luck. Exit status, preserved output, state mutation, and rebuild decisions
are part of behavior—not just generated HTML bytes.

## Minify++ relationship

Minify++ is an independent focused minifier embedded through a small public API.
Standalone Minify++ is currently byte-identical to the embedded implementation
and header. Nift minification is opt-in and happens at the final-output boundary.
Minify++ should not infect template semantics or make Nift a bundler/compiler.

## Where Nift appears strongest

Evidence and strong architectural fit currently favor:

- bespoke document-oriented company/product/portfolio sites;
- documentation and custom content construction;
- existing HTML/CSS/JS migrations;
- frontend shells around backend/API applications;
- selectively interactive island architectures;
- large page counts when Nift's explicit composition model fits;
- AI-assisted generation and maintenance where fast precise failure is valuable.

These are not universal claims. State-heavy client applications may need only a
framework toolchain. Rich publication systems needing built-in taxonomies,
pagination, multilingual editorial workflows, feeds, and large theme ecosystems
may fit Hugo, Astro, or specialized documentation products better.

## Evidence versus belief

Strongly demonstrated: fast generation, cheap incrementals, scaling, ordinary
web composition, adversarial robustness, and small React/Vite islands without
framework-specific Nift support.

Still worth realistic testing: large islands maintenance, existing-site
migrations, substantial JSON/Schema-driven catalogues, complex dependency
topologies, and backend-served application frontends.

The goal is not to prove Nift wins every comparison. Evidence should identify
where it is unusually good and where another tool is clearer.

## Condensed chronology

```text
older broad Nift
→ major stripping/simplification
→ modern website/docs identity
→ regression-suite growth and source-guided adversarial testing
→ watch/state/path/incremental hardening
→ architecture rewrite and stronger ownership
→ JSON/Schema/control flow/lexical scope
→ Minify++ hardening and integration
→ 10k collision/scaling discovery
→ hash-table memory discovery
→ compact v1.0.42 validation architecture
→ independent contract-suite separation
→ Codex takeover and real generated sites
→ React/Vite islands experiment
→ parameter-level `$[...]` value-resolution design
```

Opinions are allowed to change. Apparent product bugs have turned out to be flaky
fixtures; successful CPU fixes have revealed memory costs; perceived integration
friction has turned out to encode correct requirement semantics. Investigate
until the model improves.

