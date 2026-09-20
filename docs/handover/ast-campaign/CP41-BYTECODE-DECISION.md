# CP41 — bytecode / VM decision

Decision: **do not implement bytecode or a VM in this campaign. Continue with the tree-walking AST.**

Evidence:

- CP17 AST-covered loops/arithmetic improved roughly 45–48x.
- Callgrind dropped from about 19.2B instructions to about 429–437M and the legacy string-processing signature collapsed.
- `nift::ast::evaluate` was only about 2% of the post-AST profile, so tree dispatch is not the demonstrated bottleneck.
- Startup showed no measurable regression and AST memory remained modest/flat with iteration count.
- CP37's remaining worst workloads are substantially fallback-heavy because prepared execution for CP18–CP29 has intentionally not yet been completed.
- CP31–CP36 likewise show JSONIC++ is fast and remaining JSON traversal/mutation cost is language execution/fallback, not a need for a lower-level VM.

A bytecode layer now would optimize the wrong boundary and complicate debugging, source spans, closures and cross-platform certification before full AST semantics are established. First complete prepared AST execution, eliminate hot legacy fallback, rerun the full corpus, then profile again.

Future VM trigger: write a separate proposal only if a fully AST-covered workload shows tree dispatch/node traversal as a dominant measured cost and the expected gain materially exceeds the complexity/startup/memory cost. Local-slot resolution should likewise be independently profiler-justified.
