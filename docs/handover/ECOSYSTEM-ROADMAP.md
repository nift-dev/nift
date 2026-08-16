# Ecosystem production roadmap

> This is the living cross-project production-readiness roadmap. Current repository evidence and maintained project-local roadmaps supersede stale details.

# Production Status and Living Roadmaps

I would include the following in the handover architecture somewhere because this is important enough to apply to all three actual products.

## 1. "Production ready" is not a version number

For:

```text
Nift
Minify++
tscc
```

production status should be an **evidence-based judgment**, not:

```text
we reached v1.0
```

or:

```text
the feature list looks complete
```

---

# 2. Different projects need different evidence

## Nift

Production confidence primarily comes from:

```text
behavioral regression
incremental correctness
dependency/requirement correctness
filesystem safety
watch correctness
real-world website builds
performance stability
sanitizers
documentation accuracy
release reproducibility
```

## Minify++

Production confidence primarily comes from:

```text
semantic preservation
lexical edge coverage
real-world corpus
malformed-input safety
runtime comparison
fuzzing/sanitizers
performance
Nift integration
```

## tscc

Production confidence primarily comes from:

```text
explicit language contract
complete semantic slices
runtime correctness
scope/evaluation order
negative diagnostics
reference comparison
real-world programs
malformed-input safety
sanitizers
performance
documentation truthfulness
```

One generic "production checklist" would therefore be a mistake.

---

# 3. Current relative roadmap

My present inherited assessment is:

```text
                    production readiness

Nift        ███████████████████░
            nearest

Minify++    ████████████░░░░░░░░
            significant hardening remains

tscc        ███████░░░░░░░░░░░░░
            longer compiler-validation path
```

**Those bars are conceptual, not measurements.**

I would not put percentages on any of them.

More importantly, Codex should revise this assessment after inspecting the repositories and running current validation.

---

# 4. Nift's immediate path

The current Nift roadmap appears approximately:

```text
current trusted checkpoint
        ↓
$[...] parameter interpolation
        ↓
focused + independent contract coverage
        ↓
dynamic dependency/requirement interaction audit
        ↓
full regression
        ↓
adversarial/ruthless pass
        ↓
sanitizers
        ↓
candidate builds Nift website/templates
        ↓
performance regression check
        ↓
documentation/handover reconciliation
        ↓
release-process validation
        ↓
production release decision
```

This is why I continue to regard Nift as **release-near rather than fundamentally unfinished**.

If testing uncovers a deep architectural problem, that assessment changes.

---

# 5. Minify++'s immediate path

Approximately:

```text
current implementation
        ↓
formalize supported syntax/contract
        ↓
systematic HTML/CSS/JS edge matrices
        ↓
semantic/runtime testing
        ↓
real-world corpus
        ↓
adversarial malformed input
        ↓
sanitizers/fuzzing
        ↓
performance/output-quality benchmarking
        ↓
Nift synchronization/integration
        ↓
docs/site
        ↓
release candidate
        ↓
production decision
```

The primary uncertainty is not whether the program can minify ordinary files.

It is:

> **How much evidence do we have that it preserves meaning across the syntax it claims to support?**

---

# 6. tscc's immediate path

Approximately:

```text
current compiler
        ↓
define exact compatibility target
        ↓
map architecture
        ↓
map feature/support matrix
        ↓
identify incomplete semantic slices
        ↓
complete highest-value slices
        ↓
runtime/differential regression expansion
        ↓
real-world project corpus
        ↓
parser/malformed/adversarial hardening
        ↓
sanitizers
        ↓
performance work
        ↓
website/support reconciliation
        ↓
candidate validation
        ↓
production decision for defined supported scope
```

The critical first step is defining what "production tscc" actually promises.

---

# 7. Development checkpoints modify the roadmap

This is the piece I would most strongly add to what Codex already knows about our checkpoint methodology.

A checkpoint is **not merely**:

```text
implement
test
save good state
```

It should now explicitly include:

```text
IMPLEMENTATION REVIEW
Did architecture change?

BEHAVIOR REVIEW
Did public behavior change?

TEST REVIEW
What new failure family did we learn about?

WEBSITE/DOCS REVIEW
Did public explanation change?

HANDOVER REVIEW
Did durable institutional knowledge change?

PRODUCTION-READINESS REVIEW
Did this checkpoint change our confidence?

ROADMAP REVIEW
Does the evidence change what we should do next?
```

That last part is important.

---

# 8. Example of roadmap evolution

Suppose Minify++'s roadmap says:

```text
next:
    benchmark JS performance
```

but the current checkpoint discovers a regex/division semantic bug.

The roadmap should become:

```text
next:
    characterize slash lexical family
    add adversarial regressions
    audit scanner
    corpus-test fix
    THEN return to performance
```

Do not keep benchmarking simply because the old roadmap said benchmarking was next.

---

# 9. Nift example

Suppose `$[...]` parameter interpolation passes all direct tests but dynamic:

```text
@input($[selector])
```

reveals stale dependency sidecars after the selector changes.

Then the feature checkpoint has taught us that:

```text
parameter interpolation
```

is actually entangled with:

```text
dynamic dependency lifecycle
```

The production roadmap must expand before release.

That is useful evidence, not schedule failure.

---

# 10. tscc example

Suppose five new syntax features are planned, but implementing the first reveals that the existing binding model cannot reliably distinguish shadowed bindings during lowering.

The roadmap should stop saying:

```text
feature 2
feature 3
feature 4
```

and instead prioritize:

```text
binding architecture
scope regression
transform hygiene
```

because otherwise every subsequent feature inherits the defect.

---

# 11. Roadmaps should shrink too

"Living" does not mean endlessly adding tasks.

If evidence shows something is unnecessary, remove it.

For example:

```text
hypothesized major compatibility problem
    ↓
investigation demonstrates architecture already handles it
    ↓
focused regression added
    ↓
roadmap item closed
```

The goal is an accurate map, not an ever-growing backlog.

---

# 12. Production status itself remains maintained

Even after a production release:

```text
production-ready
```

is not a permanent historical medal.

A serious regression can temporarily make the latest development candidate unsuitable for release.

The process should always distinguish:

```text
latest known-good release
latest trusted checkpoint
current working tree
```

Those are not necessarily the same thing.

---

# 13. Suggested standard section for all three product handovers

I would have Codex include something like this in Nift, Minify++, and tscc:

> ## Production-readiness roadmap
>
> This roadmap records the project's current best assessment of the work and evidence required to reach or maintain production-ready status.
>
> It is a living roadmap, not a fixed launch checklist.
>
> Review it at every substantial development checkpoint. New implementation evidence, regression failures, adversarial testing, sanitizer results, benchmarks, real-world usage, documentation work, compatibility findings, or architectural discoveries may change priorities or redefine remaining risks.
>
> Update the roadmap when that happens. Add newly discovered work, reorder priorities, narrow or expand scope, and remove work that evidence shows is unnecessary.
>
> Once production status is reached, continue maintaining this section as the roadmap for preserving production quality and developing the project safely.
>
> Never continue following an obsolete roadmap merely because it was previously documented.

I would consider that a **settled maintenance principle** across all three projects.

---

# 14. And a corresponding checkpoint section

> ## Roadmap review at checkpoints
>
> Before declaring a substantial checkpoint complete, ask:
>
> * What did this checkpoint teach us?
> * Did it expose a new bug family or architectural risk?
> * Did it strengthen or weaken production confidence?
> * Did it change the expected scope of production readiness?
> * Did it invalidate a planned task?
> * Did it reveal a more important next task?
> * Did documentation, website, tests, release procedure, or handover material become stale?
> * Does the production roadmap need to change?
>
> Record durable conclusions in the appropriate project documentation.

That turns the roadmap into part of the development method rather than a document written once before release.

---

# Final note to Codex

These seven handovers should ultimately become **seven independently useful repository contexts**, not one giant ecosystem document copied seven times.

Their canonical concerns should be approximately:

```text
Nift website
    → Nift's public explanation and real-world dogfooding

Nift regression suite
    → Nift's independent behavioral contract

Minify++
    → minifier architecture, semantics, correctness and production roadmap

Minify++ website
    → truthful communication of Minify++ behavior/evidence

tscc
    → compiler architecture, semantics, compatibility and production roadmap

tscc regression suite
    → independent executable compiler contract

tscc website
    → truthful communication of tscc's actual supported compiler
```

Cross-project facts should be summarized only as necessary and point toward their canonical owner.

And the three product roadmaps should be treated exactly like the handover documents themselves:

```text
living
evidence-driven
revisable
correctable
expandable
reducible
never sacred
```

The development style we established with Nift was not:

> Make a giant plan and execute it until every checkbox disappears.

It was much closer to:

```text
form current model
      ↓
choose highest-value next step
      ↓
implement
      ↓
test aggressively
      ↓
learn
      ↓
update model
      ↓
update roadmap
      ↓
establish trusted checkpoint
      ↓
repeat
```

That is the development process I would want Codex to inherit for **Nift, Minify++, and tscc for the remainder of their lives**, including after each has reached production status.

And once Codex has inspected the actual repositories, I would specifically ask it to **rewrite the "current roadmap" portions of these handovers based on repository evidence rather than copying my assessment verbatim**. Nift being release-near, Minify++ needing a substantial correctness-hardening phase, and tscc having the longest semantic-validation path are my current best inherited assessments—but the whole point of the checkpoint methodology is that **evidence gets to change them**.
