# JavaScript optimizer foundation checkpoints 1–15

This tranche prepares coordinated nested-scope mangling without enabling
captured-binding renaming. Each checkpoint is retained as an individual commit.
The representations are non-mutating unless noted, so incomplete knowledge
continues to fail closed.

## Syntax inventory

1. Statement forms are classified, including block, control, abrupt, label,
   declaration, module and empty-statement tokens.
2. Expression operators retain kind and precedence metadata.
3. Identifier, object, array, rest and default binding-pattern boundaries are
   retained independently of the existing conservative pattern rewriters.
4. Ordinary, async, generator, arrow, method-like and class contexts are
   inventoried.
5. Static imports, dynamic imports, exports and re-export clauses retain module
   roles.

These checkpoints establish source-positioned inventories rather than claiming
that Minify++ now has a complete ESTree-style parser or optimized printer.

## Scope and reference foundation

6. Every scope retains its children and canonical containing function.
7. Compatible repeated `var`/function declarations share one stable binding
   identity and all declaration occurrences are renamed transactionally.
8. Every resolved and unresolved reference has a direct token index.
9. Parameter-initializer references are distinguished and cannot resolve to
   function-body-only declarations.
10. Nested functions retain explicit captured-binding edges and capture depth.
11. Direct `eval`/`with` hazards propagate to ancestor scopes whose spelling
    can be observed.
12. The public `javascript_binding_signature` API emits a deterministic
    identifier-spelling-independent topology. Resolved declarations and
    reads/writes use binding IDs; unresolved and property-like names remain
    spelling-sensitive.

## Coordinated allocator foundation

13. Descendant declarations and unresolved references populate shared
    nested-name barriers instead of being rediscovered independently.
14. A binding-interference relation centralizes when lexical/catch names may be
    reused across disjoint scopes. It is evaluated lazily to avoid materializing
    a quadratic graph for large bundles.
15. Eligible bindings are ordered deterministically. Large allocation units use
    estimated printed savings and a stable source-frequency alphabet; smaller
    units preserve the existing stable spelling policy.

Captured bindings remain excluded. Checkpoint 16 must use the coordinated
barriers, capture edges, interference relation and topology oracle together;
merely removing the existing capture exclusion would repeat the Moment defect.

## Validation

- Product smoke, Node, module, 15,459-program generated semantics, scope,
  structured, aggressive differential, 36-output syntax/runtime bundle,
  formatting, cross-format, CLI and 70,000-case deterministic fuzz gates pass.
- ASan/UBSan smoke, CLI and 70,000-case fuzz gates pass with leak detection
  disabled for this environment.
- The TypeScript JSX/TSX oracle passes 221/221 cases.
- Test262 selected 48,011 programs: 39,747 original/transformed passes, 8,264
  classified runtime incompatibilities and zero transformed failures.

The richer structured inventory has a measurable throughput cost. A short local
sample measured structured scope analysis at 12.2 MiB/s versus 13.8 MiB/s before
this tranche, and aggressive JavaScript at 4.3 MiB/s versus 5.0 MiB/s. That cost
must be profiled before enabling checkpoint 16; correctness foundation commits
are not being misreported as performance wins.
