# CP12C slice 2 — default `for` on the VM

Status: implementation candidate, local commits only. No push, merge, tag,
release or integration.

## Semantics (owner decisions A–D, 2026-10-22)

The default `for` implements legacy's iteration model:

- **Live element values, fixed entry length.** A plain local, global, captured
  cell or upvalue source is **re-read at every step**; modifications of
  existing elements are visible, appended elements are not visited, and
  reassigning the source is observed (a shorter new array reports the vanished
  element at its first read, a longer one is read only to the entry length).
- **Member paths and other expressions are evaluated once** and held; mutating
  the parent does not change the loop (legacy's member-path copy).
- **Aliases follow D-040.** `b := a` values copy on write, so mutating `b`
  never changes a loop over `a`. (Aggregate alias *declarations* are still the
  pre-existing `aliasing` refusal; see limitations.)
- **The loop variable is a per-iteration value binding.** One cell is created
  per iteration only when a closure captures it (D-038). Assigning to it does
  not write the element.
- **Object iteration** uses the entry key list in insertion order; values are
  read from the current source each step, so a reassigned object's values are
  seen and a missing key is the legacy `reference target no longer exists`
  error.
- **`loop` metadata**: `index` (1-based), `index0`, `first`, `last`, `length`,
  built only when the body names `loop`.
- **No extra owning reference to the source** is held across the body for a
  plain binding: local registers are read directly, cell/global/upvalue reads
  use a temporary that is cleared before the body. The iterator itself is an
  index plus the entry count and (for objects) a heap-accounted key array.
- **Pair binding** is the object form. Pair over an array is the legacy error
  `array @for syntax is @for(item : array){...}`; single over an object is the
  object-syntax error; non-iterables use `@for can only iterate over JSON
  arrays or objects`. These are compiled and raised on the VM.
- **Vanished element/key diagnostics**: the missing lookup is recorded and the
  error is raised before the first body statement that reads the variable, so
  preceding statements keep their side effects (legacy's lazy location
  reference). The error is `reference target no longer exists: <name>` at the
  first read's source position.
- **Break/continue/return**: `break`/`continue` use the normal loop stack;
  `continue` jumps to the increment, `break` to the exit; `return` unwinds the
  frame (heap registers are cleared by the return path).
- **Maps, sets and stacks** remain refused with `callable:map`/`set`/`stack`:
  those natives are not on the VM (handover rule).

## Implementation

New files:

- `src/rt/compiler_loops.cpp` — orchestration, source/header setup, epilogue.
- `src/rt/compiler_loops_step.cpp` — bindings, per-iteration step, body.
- `src/rt/compiler_loops_bind.cpp` — source reads, binding/value/metadata emission.
- `src/rt/compiler_loops_scan.cpp` — read/write/capture scans of the body.
- `src/rt/compiler_loops*.h` — state and pass headers.
- `src/rt/vm_iter.cpp`/`.h` — iterator opcodes.

Hooks: `NodeKind::For` in `compiler.cpp`; `Loop::continues` for the For back
edge; `NodeKind::For` in `infer_walk.cpp` and `UnitKinds::elements`; the six
appended opcodes `IterCheck`, `ArrayElem`, `ObjectKeys`, `ObjectLookup`,
`IterMissing` and the grouped dispatch in `vm.cpp`.

Inference types the loop variable as the array element (or object value) kind
and records the element kinds per iterable node. Sources whose kinds are
unknown or both array and object are refused before execution; a source that
is statically known to be the wrong form/non-iterable is compiled as an
error-only loop, so the legacy error is raised on the VM (the body is not
compiled because it is unreachable).

## Tests

`tests/runtime2/for_s2.py` runs 53 fixtures in `tests/runtime2/for-s2/` with
`NIFT_VM=require` for every executable case and compares stdout, stderr and
exit status with the legacy oracle. 37 run on the VM, 9 are VM errors, 7 are
named pre-existing fallbacks (`callable:map/set/stack`, `jump-not-last`,
`binding-name-reserved`, `jump-outside-loop`, `unresolved-name`).

`tests/runtime2/for_s2_bench.py` measures the 1,000,000-element cases.
`tests/runtime2/for-s2-bench/` holds the scripts.

Negative controls (`-DNIFT_FOR_MUTANT=N`, `NIFT_FOR_MUTANT` in
`compiler_loops*`/`vm_iter`):

| N | Defect | Detected by |
| --- | --- | --- |
| 1 | snapshot instead of live values (M1) | `g6_element_modify`, `g6_element_modify_late`, `g6_object_values` |
| 2 | dynamic current length instead of entry length (M1b) | `g6_push_once` (`1 2 3 4`) |
| 3 | one shared cell for the whole loop (M2) | `g7_closure_loop_var` (`3 3 3`) |
| 4 | `continue` does not advance (M3) | `g3_continue` (timeout, intended) |
| 5 | break skips iterator cleanup (M4) | `g6_break_object` (`heap_live` +128 bytes) |
| 6 | pair order swapped (M5) | `g1_object`, `g2_object_order` |
| 7 | object order reversed (M6) | `g2_object_order` |

The harness requires a compiling mutant binary and real per-case evidence; a
crash, missing statistics or unrelated timeout is a harness failure, not a
detection. M4 compares the mutant's `heap_live` with the normal build.

## Memory and performance

`Value` is 16 bytes; a 1,000,000-element array is **16,777,280 bytes** of
accounted heap before any iterator state. The literal 8 MB budget is therefore
impossible for that fixture (the array reaches only 524,288 elements under
8 MB). The measurement uses the owner-approved baseline-relative form:

- 1M array built once with `range(1000000)`; baseline `heap_peak` 16,777,280.
- A write loop (`a[i] = a[i] + 1`) succeeds under 17/18/24 MB with
  `heap_peak` unchanged at 16,777,280 — a second owning copy would add ~16.8 MB
  and fail 18 MB. `heap_live` returns to the baseline after the loop.
- Peak RSS ~22 MB. 8 MB still stops at array construction, as expected.

Per-iteration cost (GCC 13.3 Linode, median of 7, construction subtracted):

| Body | for | while | ratio |
| --- | --- | --- | --- |
| minimal scalar | 25.2 ms | 60.2 ms | 0.42 |
| arithmetic | 37.8 ms | 44.9 ms | 0.84 |
| modify existing element | 79.6 ms | 73.3 ms | 1.09 |

All three are within the owner's 10 % objective on GCC 13.3. The modifying
body carries the For's own induction variable in addition to the body's `i`;
the value-only opcodes (`ArrayElemValue`/`ObjectLookupValue`) are used when the
body cannot change the source, which removed the found flag, the missing check
and the element copy from the common case.

## Inventory

Baseline (closure tree, `cp12c-s1-closures/phaseB/inventory`) → after this
candidate:

- shared 788: 147 → 162 compiled, 641 → 626 refused, `statement:For`
  142 → 0 first reasons.
- shared-curated 588: 142 → 145 compiled, 446 → 443 refused.
- rt-diff generator 200: 5 → 17 compiled, 195 → 183 refused.
- projects 86: 1 → 1 compiled, 85 → 85 refused.
- generator 300: 72 → 72 compiled, 228 → 228 refused.
- shared+projects: 148 → 163 compiled, 726 → 711 refused.
- **0 first-reason mismatches** in every corpus; 0 regressions (no script
  compiled before is refused now). Newly exposed first refusals are later
  blockers (`callable:getenv/file/thread`, `free-name`,
  `binding-name-reserved`, `interpolation:unknown-type`, `index-target`,
  `statement:Try`, `type:for-source`, `type:index`).

## Limitations / pre-existing refusals

These are not changed by this slice; the loop suite records the named
fallbacks where they are exercised:

- `pop`/`clear`/array method callbacks and object element assignment
  (`o["k"] = v`) are not on the VM; removal is tested through source
  reassignment (`a = []`, `a = [7]`), which exercises the same live/error
  path.
- Aggregate alias declarations (`b := a`) are still `aliasing` (no D-040
  alias lowering yet); the call-copy mutation case is tested instead.
- `const` declarations, loop type-error wording and built-in shadowing keep
  their existing pre-slice refusals (`declare-const`,
  `context:assignment-error`, `fn-name-reserved`).
- A missing-element error is raised before the first *statement* that reads
  the variable. For a read inside a branch that is not taken, the VM's error
  can be earlier than legacy's; statements before that point keep their side
  effects. This is recorded for the reviewer; a full lazy location-reference
  binding is CP13/L7 work.
- `loop` is built per iteration only when named; it is not allocated for
  read-only loops that do not use it.

## Known closure-dependency failures (not loop defects)

The independent closure review (`review-cp12c-s1-phaseB.md`) confirmed A1-A5.
`tests/runtime2/for_s2_known_deps.py` records loop/closure interactions and
reports them as KNOWN-DEP, never as passes; they must become PASS on the
corrected closure base. On the original base:

- **A1** `a1_loop_closures_budget.f`: capture-free closures created in a loop
  are not charged by the closure allocation path; under 8 MB the VM still
  succeeds (`heap_peak` 4,194,432) instead of refusing.
- **A2** `a2_loop_block_shadow.f` / `a2_toplevel_block_shadow.f`: a loop/body
  block binding shadowing an outer name captures the outer value
  (legacy 11/12 vs VM 1/1; legacy 2 vs VM 1).
- **A3** `a3_late_local.f` / `a3_lambda_in_lambda_late.f`: late enclosing
  declarations become `lambda body error`/unknown.
- **A4** `a4_print_unknown.f` / `a4_push_unknown.f`: VM internal failure where
  legacy reports an ordinary diagnostic.
- **A5** `a5_binding_type_name.f`: VM says `null` where legacy says `string`
  in the binding-type error.

The loop compiler does not work around these; the harness exists separately
from the loop pass gate and is rerun after rebasing on the corrected closure
candidate. The A1/loop budget test and the memory no-copy tests must also be
rerun there with closure objects counted.

## Test-gate updates (feature-driven)

Because `for` now compiles, the CP11 corpus case `fallback_for.f` is `vm`; the
subset suites move `for` from unsupported to supported; `vm_inventory.py` and
`vm_inventory_links.py` use `try` as the still-refused first blocker in the
"later-main-blocker" case; `tests/rt/vm_unit.cpp` uses `statement:Try` in the
refusal list. No semantic expectation was weakened.
