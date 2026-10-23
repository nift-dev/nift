# CP12C Slice 1 — first-class functions and lexical closures

**Base:** `runtime-2` `0474377ac8d1dae7c9da2accc4236b9f571b588a`.
**Status:** review R2 corrections applied locally; no push/integration.

## Implemented

- Dedicated internal `Kind::Cell`/`CellObject` for captured bindings. Cells
  hold a Value and the binding type; they are never reachable from scripts,
  natives or Host. `CellLoad` copies the value (D-040 COW); `CellStore` checks
  the binding type before writing.
- Static capture analysis: only bindings actually captured by a nested lambda
  become cells. Captures use ordered cells/upvalues and deferred Lambda Protos;
  `LoadFunction` is canonical per named Proto, `Closure` is fresh per creation.
- First-class function values, lambda expression/block bodies, named functions
  as values, calls through bindings/parameters/returns and object-field
  callables. Calls through locals, parameters and object fields use
  `CallValue`; object method/native names keep legacy precedence. Array-index
  calls remain refused (legacy refuses them too).
- Exact diagnostics for lambda/named/member arity, non-callable bindings and
  members, expression-bodied lambda errors (`lambda body error: `), recursion
  and string-plus-callable. D-009#5 remains the approved function-return
  diagnostic policy.
- Closure state is restored on every normal return; nested closure calls and
  expression-lambda error prefixes are covered. `CallValue` formats
  diagnostics only on failure.
- D-040 cycle rule (Option A): `CellStore`/`SetUpvalue` call the existing
  `would_close_cycle` with the real cell layout before writing. Direct,
  mutual, nested and aggregate-mediated cyclic stores fail atomically with
  `runtime.reference_cycle` and leave the previous value intact. No collector
  or second memory manager was added.
- Refused in this slice: `for`, async/`@fn`/methods, collection callbacks,
  FFI callbacks and threads; variadic lambdas (`lambda-variadic`) and spread
  calls (existing `spread`) are refused.

## D-038 / D-040 notes

- D-038 lexical scoping: caller-local dynamic lookup is still refused before
  execution (`free-name`; inventory `free-name:dynamic-caller`).
- D-040 approved difference: a store such as `g := () => null; g = () => g`
  is legal in legacy but the VM refuses it with `runtime.reference_cycle`.
  The direct, mutual, aggregate (`c02`), index (`c03`), push/upvalue (`c09`)
  and nested-`SetUpvalue` fixtures record this difference. Ordinary function
  recursion through globals, escaping closures, shared captures, independent
  instances and nested captures remain supported.
- `i05_param_mixed` reports the same failure at a different source position
  and without legacy's `return:` prefix (D-009#5).
- Predeclared local cycles are refused as D-040; no cycle collector exists.

## Tests

- `tests/runtime2/closures-s1/`: 52 fixtures, including `NIFT_VM=require`
  execution, exact diagnostics, D-038 and conservative inference fallbacks,
  module lambdas, self/mutual/object/index/push/`SetUpvalue` cycle refusal,
  cell non-observability, A2 shadowing, A3 narrow late-local refusals, A4
  print/push checked paths, A5 binding-type wording, D1/D2 reproducers, B3
  object-callable flow, D4 return-prefix policy and running-closure
  reassignment. Legacy goldens are the oracle.
- `tests/runtime2/closures_s1.py` uses `NIFT_VM=require` for fixtures expected
  to execute; remaining fallbacks are the documented D-038/A3/A4/g8 cases.
  Its mutant mode treats missing statistics, crashes and timeout as harness
  failures (never detections), and requires each mutant's designated fixture
  plus a post-statistics crash negative control.
- `tests/rt/closure_cycle_stress.cpp`: 1,000,000 rejected cyclic cell stores,
  checks refusal, cell atomicity and `live_heap_bytes()` returning to
  baseline after every attempt; plain data takes the O(1) cycle gate.
- Stress scripts: 1,000,000 acyclic capturing closures under an 8 MB VM heap
  budget, and an escaping shared-state closure surviving 1,000,000 unrelated
  closures. `heap_peak`/`heap_live` are additive VM statistics. Function
  objects (including upvalue storage) are charged in both closure opcodes;
  `h_wide.f` and `h_nocap_200k.f` are refused at ~7.99 MB accounted heap, and
  `h_live_simple.f` proves a 1,000-capture lower bound.
- Negative controls: M1 store-through-one-closure invisibility, M2 one cell
  per creation site (file-static mutant state, no class-layout change), M3
  cycle-check bypass, M4 closure-state restore failure, M5 internal cell
  exposure, M6 missing `CallValue` arity check, M7 Cell/Function lifetime
  leak, M8 missing closure-allocation accounting. Each mutant compiles; M1-M6
  are rejected semantically with statistics, M7/M8 by stress accounting, and
  a post-statistics crash is a harness failure.

## Evidence and limitations

- `CallValue` overhead in a 1,000,000-call loop: approximately **1.24×**
  direct `Call` (median local measurements).
- Known limitations: callable-flow inference is a conservative over-approximation;
  some sound callable flows may remain refused rather than guessed. Empty
  kinds in lambda units are treated as Unknown (A4/B3); existing non-lambda
  empty-kind behaviour is unchanged. The
  `__nift_module_field` arity wording is a legacy artifact matched
  deliberately. `lambda body error:` is retained verbatim; nested `return:`
  prefixes follow D-009#5. No Linode was required for this implementation
  candidate; evidence is in the deliverable `phaseB/` directory.

## Review R2 corrections (D1-D4, M2/M6/M8, A3 narrowing)

- A1: `heap_retally` now covers every `FunctionObject` in `LoadFunction` and
  `Closure`, including upvalues; the stress suite asserts charged refusal and
  a 128 B/closure live lower bound. M8 disables this accounting and is
  detected by `h_live_simple` as well as the two refusal scripts.
- A2: a main-program capture skips only the actual program-level global, not a
  block-local shadow (`g15_shadow_toplevel_block`).
- A3: late function-locals are not emitted as `Undefined`; they are refused
  before execution with a named reason (`g16_late_local`, `g16_lambda_late`).
- A4/D1: `print` and `push` never trust an unresolved kind; Unknown/empty
  print defers only to the VM's checked native print, which raises legacy's
  ordinary Error (not internal). Push does the same. `i10` runs with legacy's
  `lambda body error`; `i16` is a named refusal; `i25` now runs with legacy's
  exact `unknown value … v.push(1)` diagnostic.
- A5: function values use legacy's callable type tag (`string`), so A5
  binding-type diagnostics match (`g14_binding_function_type`).
- D2: `CheckGlobal`/`GetGlobalChecked` cover every global access form:
  read, plain store, compound, increment, push, element assignment and
  index/member reads. Diagnostics match the review probes (`m08`, `m14`,
  `m16`, `m17`, `m19`, `m20`, `m21`). `global_read_value` is the reusable
  checked read for loop code.
- D3: g8 runs on the VM again; its copy-out half now mutates by element
  assignment, preserving value semantics without the empty-receiver push
  refusal. The shared inventory has 0 changed first reasons.
- M2: mutant state moved out of `class Vm` into file-static code, so the
  mutant layout is consistent; the harness requires statistics and rejects
  crashes/timeouts. M6's lambda and named arity cases are restored.
- D4: `vm_policies` has a narrow `D-009#5 return prefix` policy for the
  print/binding diagnostics, with positive and negative self-tests. Other
  return-prefixed diagnostics still require D-018c's source proof, so the doc
  and tool agree.
- A3: unresolved names declared nowhere in the unit keep legacy's run-time
  `Undefined` (u03 executes); late declarations are still refused.
- C: B3 is a named `path-in-call-argument` refusal; `SetUpvalue` has a nested
  cycle fixture; `g4_global_reassign` exercises a closure reassigning its own
  global under ASan; VM cell-store cycle fixtures cover the VM path. `c12`
  remains an `import-scope` fallback (module-import refusal, not a closure
  path).

## API / signature notes (R2)

- New `UnitCompiler::global_read_value(slot, reg, name)` (checked value
  read) and `global_check_defined(slot, name)` (checked store precondition).
- `global_take(binding, reg, name = {})` gained an optional diagnostic name;
  call sites pass the legacy source place.
- New opcodes `CheckGlobal` (same opcode stream as `GetGlobalChecked`), and
  `Vm::op_check_global`, `Vm::op_get_global_checked`.
- `push_value(Value&, const Value&, Fault&, const Note* = nullptr)` now
  reports legacy's ordinary Error (needed by the checked push path).
- `print_argument_ok(NodeId, const Node&)` centralizes the D1 print
  admission rule.
- DeepSeek 3's `for` source read should use `global_read_value` (or emit
  `GetGlobalChecked` with the source text) instead of unchecked `global_read`;
  no loop implementation was changed here.
