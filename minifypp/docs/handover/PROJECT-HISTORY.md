# Project history and institutional context

> This is a living historical companion to the repository's operational handover. The live repository remains authoritative. Maintain, correct, reorganize, or supersede this material as project evidence evolves while retaining durable rationale.

# Minify++

## Comprehensive Product, Architecture, Development, Testing and Production Roadmap Handover

# 1. Identity

Current intended identity:

```text
Project: Minify++
Executable: minify
Likely repository slug: minifypp
```

Historically the project was called:

```text
Sift
```

during development.

References to Sift in old discussion/artifacts may therefore refer to Minify++.

---

# 2. Purpose

Minify++ is intended to be:

> **A small, fast, standalone native HTML, CSS and JavaScript minifier.**

The word **standalone** matters.

It is useful to Nift but should not conceptually depend on Nift.

---

# 3. Philosophy

Minify++ should remain:

```text
small
focused
fast
native
predictable
composable
easy to invoke
easy to test
```

It should resist becoming:

```text
bundler
transpiler
module resolver
package manager
tree shaker
framework
general frontend build system
```

unless the project direction is deliberately reconsidered.

---

# 4. Relationship to Nift

Nift can embed/use Minify++.

But the separation is valuable:

```text
Minify++
    standalone minification concern

Nift
    website-generation/build concern
```

This permits independent:

```text
testing
benchmarking
releases
users
maturity
```

---

# 5. Nift minification is opt-in

A critical design fact from our Nift release discussion:

> Nift does not minify all output automatically.

Users configure extensions to be minified.

Therefore:

```text
extension configured
    → minification

not configured
    → unchanged output
```

This limits blast radius.

---

# 6. Canonical ownership

The intended model discussed was likely:

```text
standalone Minify++
    = canonical source

Nift/minifypp
    = synchronized embedded copy
```

but Codex must verify current repository state before recording this as settled operational fact.

If true, automate or document synchronization strongly enough to prevent drift.

---

# 7. Most important correctness principle

**SETTLED ENGINEERING PRINCIPLE**

> Correctness is more important than squeezing out the final bytes.

A minifier is dangerous because a failure can produce output that:

```text
looks valid
is smaller
passes parsing
but means something different
```

That is worse than a clean crash in many cases.

---

# 8. Conservative behavior is acceptable

A safe transformation that produces:

```text
103 bytes
```

can be better than a fragile transformation producing:

```text
99 bytes
```

The project does not need to win every output-size benchmark.

---

# 9. HTML risks

Important families include:

```text
text-node whitespace
inline elements
preformatted contexts
textarea
script/style raw text
comments
attribute quoting
embedded languages
entities
```

Whitespace between tags is not universally meaningless.

---

# 10. CSS risks

Important families include:

```text
token boundaries
selectors
strings
URLs
comments
calc()
custom properties
numbers/units
functions
modern CSS syntax
```

Custom-property values deserve particular conservatism because their tokens can be interpreted later.

---

# 11. JavaScript risks

JavaScript is likely the highest semantic risk.

Important families include:

```text
ASI
regex versus division
comments
strings
template literals
escapes
operator adjacency
numeric literals
optional chaining
module syntax
Unicode
modern ECMAScript
```

according to intended syntax support.

---

# 12. ASI

Never treat newlines as uniformly removable.

Test classic boundaries involving:

```text
return
throw
break
continue
postfix ++
postfix --
```

and statement/expression transitions.

---

# 13. Slash ambiguity

`/` may begin:

```text
division
regex
comment
```

depending on lexical context.

Any scanner logic around slash deserves unusually strong testing.

---

# 14. Token merging

Removing whitespace can change tokenization.

Systematically test neighboring operators/identifiers/numbers.

---

# 15. Template literals

Template literals combine:

```text
raw segments
escapes
${...}
nested expressions
```

and are a high-value adversarial target.

---

# 16. Development workflow

For each meaningful Minify++ change:

```text
baseline
↓
define transformation
↓
construct safe examples
↓
construct unsafe neighboring examples
↓
write regression
↓
implement conservatively
↓
language-specific tests
↓
full suite
↓
semantic/runtime checks
↓
sanitizers
↓
real-world corpus
↓
benchmark
↓
sync into Nift if applicable
↓
Nift integration regression
↓
docs/site/handover review
↓
checkpoint
```

---

# 17. Golden tests

Golden:

```text
input
→
expected output
```

tests are useful for deterministic lexical transformations.

But they are not sufficient proof of semantics.

---

# 18. Semantic tests

For JavaScript especially:

```text
run original
run minified
compare behavior
```

is extremely valuable.

Possible comparisons include:

```text
stdout
exit status
observable values
side-effect counts
```

---

# 19. Real-world corpus

Minify++ should increasingly be tested against actual:

```text
HTML
CSS
JavaScript
```

from real projects.

A minifier that passes hand-constructed examples but fails on ordinary modern production files is not production-ready.

---

# 20. Malformed input

The project should have a deliberate policy for malformed input.

Possible outcomes include:

```text
controlled error
conservative preservation
best effort
```

but accidental undefined behavior is not acceptable.

---

# 21. Fuzzing

Minify++ is an excellent candidate for fuzzing.

Potential long-term production-hardening work:

```text
valid-seed mutation
malformed random input
sanitizer-backed fuzzing
hang detection
crash minimization
semantic differential checks where possible
```

---

# 22. Sanitizers

Before production status, meaningful native-code confidence should include at least appropriate:

```text
ASan
UBSan
```

coverage on substantial corpora/adversarial suites.

Other sanitizers/platform tools can be added where useful.

---

# 23. Performance

Minify++ exists partly because a tiny native tool can have excellent performance.

Measure dimensions such as:

```text
startup
many small files
large files
HTML throughput
CSS throughput
JS throughput
memory
output size
```

But correctness gates performance comparisons.

---

# 24. Production-ready definition

For Minify++, "production ready" should mean roughly:

> A developer can deliberately run Minify++ on ordinary supported HTML/CSS/JavaScript production assets with a well-supported expectation that it will preserve semantics, fail controllably on unsupported/problematic inputs, perform well, and behave predictably across its documented interface.

It does **not** require:

```text
perfect support for every future syntax
smallest output in every benchmark
zero future bugs
```

It does require a much higher semantic confidence level than:

> It worked on our example files.

---

# 25. Current maturity

**HISTORICAL CURRENT ASSESSMENT — VERIFY AGAINST REPOSITORY**

Our latest broad assessment was:

```text
Nift:
    release-near

Minify++:
    mid-stage / substantial implementation exists
    needs confidence-building and battle testing

tscc:
    longer-term
```

Minify++ should therefore not automatically be considered production-ready merely because it is compact.

Its small size makes exhaustive reasoning more plausible, but minifier semantics remain difficult.

---

# 26. Current production roadmap

**LIVING ROADMAP**

### Phase A — establish exact current contract

Determine:

```text
supported HTML
supported CSS
supported ECMAScript
CLI behavior
error behavior
stdin/files behavior if applicable
output behavior
```

Anything ambiguous becomes either:

```text
specified
tested
or deliberately unsupported
```

### Phase B — systematic lexical/semantic coverage

Build matrices around:

```text
HTML whitespace/raw text
CSS token boundaries
JS ASI
JS slash ambiguity
templates
strings/escapes
comments
modern syntax
```

### Phase C — adversarial hardening

Attack:

```text
malformed input
truncation
deep nesting
empty input
huge tokens
Unicode
boundary bytes
path/file errors
```

### Phase D — corpus testing

Run representative real-world assets.

Any discovered bug becomes a minimized regression.

### Phase E — semantic differential validation

Where practical:

```text
original JS
vs
minified JS
```

and potentially compare against established minifiers for discovery—not blindly as specification.

### Phase F — native safety

```text
ASan
UBSan
fuzzing
warnings
```

### Phase G — performance

Benchmark only after correctness is stable enough to make comparisons meaningful.

### Phase H — Nift integration

Synchronize canonical implementation and run Nift's minification/integration tests.

### Phase I — documentation/site

Ensure supported/unsupported behavior is described accurately.

### Phase J — release candidate

Perform clean build, full suite, corpora, sanitizer and integration validation from release-like state.

Then decide whether evidence justifies production status.

---

# 27. Roadmap must evolve

This should be written explicitly into Minify++'s handover:

> The production roadmap is a living risk assessment. Every substantial checkpoint must review whether newly discovered syntax interactions, corpus failures, sanitizer findings, performance results, Nift integration behavior or user requirements change the remaining path to production. Add new phases when evidence exposes a new risk family; remove or reduce work when evidence shows it is unnecessary.

This is particularly important for a minifier because testing may reveal entire lexical families we did not initially appreciate.

---

# 28. After production

Production status should transition the roadmap from:

```text
prove baseline safety
```

toward:

```text
maintain safety
expand syntax deliberately
increase corpus diversity
monitor performance
improve diagnostics
support platforms
respond to language evolution
```

---

# 29. Release relationship with Nift

Because Nift minification is opt-in, Nift does not necessarily need to wait until Minify++ has achieved every standalone ambition.

But:

> Any Minify++ version embedded in a production Nift release should still meet an appropriate correctness threshold for the behavior Nift exposes.

Document the exact embedded version/synchronization state.

---

# 30. Do not accidentally

```text
optimize past semantic safety
turn Minify++ into a bundler
treat parser success as semantic correctness
let embedded and standalone source drift
benchmark broken candidates
assume modern JS is simple lexical whitespace removal
declare production based on line count
```

---

---


# 31. Competitive benchmark tooling and external-context checkpoint (2026-08-18)

The benchmark story was split into explicit layers instead of forcing every number into one chart. The existing generated-fixture API benchmark remains a self-regression gate. A real-CSS API benchmark and a same-host CLI comparison harness were added for Bootstrap/Animate/Tailwind style workloads, and a reference adapter was prepared for running Nift in the external `privatenumber/minification-benchmarks` JavaScript suite.

The checkpoint reinforced a durable communication rule: **compare output sizes directly when the exact fixture/result is fixed, but compare speeds only when host and invocation boundary are comparable.** Published Lightning CSS/esbuild timings from another environment are useful context, not a denominator for a Minify++ speedup claim. The public website gained a Benchmarks page built around that distinction.

The follow-up definitive checkpoint ran current stable CSS competitors on the
same i7-12700H host and joined the current upstream JavaScript suite with Nift's
real file-oriented adapter. It confirmed the intended tradeoff rather than a
simple victory: Minify++ led the tested CSS CLI medians, while Lightning CSS
made every CSS fixture smaller; Nift passed all upstream JavaScript integrity
checks but placed last under the suite's size-heavy score. Raw samples/results,
tool versions, failures, provenance and environment were retained so the website
charts remain interpretations rather than the only evidence.

## 2026-08-18 — Jsonic++ parser ownership

The private `src/Json.h` used by Minify++ was recognized as the same byte-identical parser used by Nift core and extracted into standalone Jsonic++. Minify++ continues to vendor the header privately to preserve its dependency-free distribution, but parser semantics now originate in Jsonic++; Minify++ owns minification behavior and must rerun its full semantic corpus after parser synchronization.
