# D3 checkpoint 2: scope ranking

Date: 2026-09-13

The checkpoint-1 aggregate observer predates the current planner and assigns
whole function units containing `class` to a historical class category. Source
inspection against the current planner showed that this is not the active D3
barrier: current class handling is per binding/reference in heritage and opaque
class scopes.

D3 is a UMD bundle whose implementation is contained by one factory function:

```js
(function (global, factory) { /* wrapper */ }
 (this, (function (exports) { /* almost the complete bundle */ })));
```

The 555,767-byte fixture contains 72 arrow functions. The current planner's
unit scan increments `arrow_count` before excluding tokens owned by nested
function units, and marks a unit unsafe after its second arrow. Consequently
the outer D3 factory is the overwhelmingly dominant rejected unit and all
otherwise eligible bindings assigned to it are skipped together.

This explains why D3 is uniquely weak: the same emergency barrier has modest
cost in small, independently scoped code but becomes effectively bundle-wide
inside D3's factory wrapper. The repair must order coordinated allocation from
lexical parents to children and reserve resolved outer names in descendants;
simply deleting the barrier would recreate the nested-arrow collision found by
Test262.

This checkpoint is diagnostic only and does not change minifier behavior.
