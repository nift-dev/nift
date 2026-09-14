# Performance CP6 — reference resolution cache trial

CP6 profiled repeated lexical reference lookup and tested a per-scope `(name, parameter-initializer)` resolution cache.

The trial was **rejected** rather than retained. Reference resolution is visible in the stage profile, but allocating per-scope hash maps adds fixed cost and the representative synthetic workloads did not show a stable end-to-end improvement. The broad, redeclaration-heavy, and repeated-outer-reference probes were noisy on the shared host; importantly, none established the required multi-fixture win and the repeated-reference case did not improve consistently.

The production resolver therefore remains unchanged at the CP5 implementation. Future work should avoid a cache-per-scope design. Better candidates are a compact resolved-binding ID attached during one linear identifier walk, or selectively memoizing only scopes/names proven hot by real-bundle profiles.

This is an intentional application of the campaign rule: a plausible optimization is not accepted without measured evidence.
