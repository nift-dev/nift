# CP39 — binding/scope resolution decision

Resolved local slots/environment cells are deliberately **not implemented yet**.

CP17's prepared tree walker already reduced the representative loop from ~1.5–1.6 s to ~35–36 ms and Callgrind from ~19.2B to ~429–437M instructions. `nift::ast::evaluate` was only about 2% of the resulting profile. That evidence says name resolution is not yet the dominant post-AST cost, while much of CP18–CP29 still awaits prepared runtime execution.

Prematurely replacing binding-name resolution with slots would complicate shadowing, closure capture-by-binding, imports/exports and rebinding before those semantics have completed differential certification. Keep `Binding(name)` live-resolution semantics for the migration. After full prepared execution, re-profile; introduce `LocalSlot`/`ClosureCell` only if lookup becomes a measured hotspot.

This is an explicit evidence-based no-change checkpoint, not an abandoned optimization.
