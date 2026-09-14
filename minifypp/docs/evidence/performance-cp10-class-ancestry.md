# Performance CP10 — class-ancestry locality trial rejected

A trial replaced the per-reference ancestor walk used by the binding renamer's opaque-class safety check with a precomputed per-scope boolean. The simplification was not semantically equivalent: the smoke regression `structured self-referential class fallback` exposed a class-name rename that must remain blocked.

The trial was removed completely. No production code from this experiment is retained. Future locality work in this area must preserve the exact function-unit boundary and class-scope ownership semantics rather than collapsing them to a scope-local boolean.
