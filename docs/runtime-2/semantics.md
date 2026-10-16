# Language semantics (v0 — observed behaviour of v4.12.0)

Status: v0 records behaviour probed against `ce8cd36`. CP08 turns it into v1
with source references and the decision on each "Proposed" row. Probes live in
`tests/runtime2/semantics/` (added in CP08).

| Area | Observed behaviour | Example | Proposed for Runtime 2.0 |
| --- | --- | --- | --- |
| Declaration | `name := expr` declares in the current scope; redeclaring in the same scope is an error | `r := 1; r := 2` → "binding already declared" | Keep |
| Assignment | `name = expr` rebinds an existing binding | — | Keep |
| Aliasing | Declaring from a *place* (name, member or index path) creates a location reference; mutation through it changes the owner; rebinding it does not | `a := [1,2]; b := a; b.push(3)` → `a` has 3 items; `b = [7]` leaves `a` | Keep, specify precisely in v1; implement as reference cells with COW owners |
| Copy at calls | Arguments and return values are copied; callee mutation is invisible to caller | `fn(m(x)){x.push(9)}` leaves caller's array | Keep semantics; implement with COW (O(1)) |
| Scalars | Copied | `y := x; y += 5` leaves `x` | Keep |
| Structs | Reference semantics | `q := p; q.x = 9` changes `p.x` | Keep |
| Plain objects | Member assignment rejected | `o.c = 3` → "member assignment requires a struct instance" | Keep |
| Free names | Resolved at call time through the *caller's* scopes (dynamic scoping) | `fn f(){return zz}; fn g(){zz := 5; return f()}` → 5 | Keep via `GETDYN`; emit a warning in a later strict mode (needs owner decision before becoming an error) |
| Closures | See later writes to captured names and globals declared after creation | `h := () => x; x = 2; h()` → 2 | Keep via upvalues/`GETGLOBAL` |
| Integers | int64 with checked overflow (error) | `9223372036854775807 + 1` → overflow error | Keep |
| Division | `/` yields a float when inexact | `7 / 2` → 3.5 | Keep |
| Exact numbers | Large JSON integers print exactly | `9007199254740993` | Keep (decimal kind) |
| Strings | `$[expr]` interpolates inside literals; `\x` is not an escape | `"v=$[x]"` → `v=3` | Keep; compile interpolation |
| Errors | `throw error(msg)`; `catch(e)` exposes `e.message`, `e.code` (`user.raised`) | — | Keep |
| Callable identity | Aliases compare equal; distinct lambdas do not | — | Keep (pointer identity) |
| Recursion | Fails at depth 64 | `down(64)` → depth error | Change: frame budget (~100k), same error code |
| Handles | Strings starting `\x1fnift:<kind>:` act as handles, including strings from data | JSON value invoking a closure | **Change**: typed handles; strings never coerce (removes D3) |
| Handle lifetime | Registries never reclaim; a reconstructed tag stays valid | — | **Change**: refcounted resources |
| Evaluation count | Each operand evaluated once in probed cases (calls, arrays, objects, binary ops, fs ops) | — | Keep; differential harness traces Host ops |
| Natives vs user names | Natives take precedence except `error` | — | Keep |
