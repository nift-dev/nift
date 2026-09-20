# CP36 — JSON query / transform / serialization

Status: complete for the current AST-migration boundary.

CP31–CP35 established that JSONIC++ itself is not the bottleneck: the 2.19 MB / 100k-record fixture costs about 4 ms to read and 34 ms to parse natively, while CP33 reduced Nift `inject()` from roughly 370–390 ms to roughly 70 ms by bypassing generic expression scans for valid JSON. Native traversal is about 3.5 ms and native mutation about 7.1 ms; current Nift traversal/mutation remain dominated by legacy `for`/statement execution.

The query/transform workloads have the same shape: parse/representation cost is now small relative to repeated language-level traversal, predicates, arithmetic, member/index access and mutation. Serialization is not permitted to be conflated with parse or traversal timings. The current conclusion is therefore to preserve the CP33 direct JSON parse path and defer claims about final query/transform performance until prepared `for` and mutation execution is completed in the DeepSeek CP18–CP41 runtime pass.

Acceptance decision: no JSONIC++ rewrite or alternate JSON representation is justified. The next high-leverage JSON optimization is prepared AST execution over the existing JSON representation. Cross-language publication must be regenerated after that migration rather than publishing fallback-heavy numbers as the final JSON result.
