# Reference semantics, Phase A (cand/refsem-a)

Status: **candidate**, review round 1 (the independent review rejected
`ad9d842f`; section 13 maps every finding to its fix); not integrated. Branch
`cand/refsem-a`, built on the frozen collections candidate `cand/collections`
@`18720d14` (history kept). Decision: **D-NEXT-refsem** (`../decisions.md`;
the number is assigned at integration). Owner decisions of 2026-11-01
(option 2, rulings D1-D9). Phase A is an internal milestone, not a release
boundary: arrays and objects (Phase B) need their own plan and approval.

Tests: `make test-runtime2-refsem` (`tests/runtime2/refsem_a.py`,
`tests/runtime2/refsem/`), `make test-runtime2-refsem-mutants`
(`tests/runtime2/refsem/mutants.sh`). The D-063 suite
(`make test-runtime2-collections`) still runs, re-based (section 7).

## 1. Semantics delivered

| Area | Rule | Legacy |
| --- | --- | --- |
| Sharing | maps, sets, stacks and struct instances are shared by assignment, declaration, synchronous arguments, returns, closure captures, container storage/extraction, struct fields and `for` element bindings; a mutation through any alias is seen through all | =L |
| Identity | `same()` is identity for collections and instances; `==` structural for collections, **identity** for instances (`P() == P()` false) | =L |
| Field initialisers | `v := map()` (`set()`, `stack()`) is evaluated afresh per instance | =L |
| Construction | `Name(args)` anywhere a value is (`return P()`, `[P(), P()]`, `f(P())`), outside loop regions | =L |
| Cycles | refused **before** the store, catchable `runtime.reference_cycle`; legacy's text where legacy refuses ("set/push would create a cyclic reference", "assignment would create a cyclic reference: <field>"), else "reference cycle: the stored value would contain itself"; the receiver is unchanged, nothing leaks | =L text (legacy fatal: catchability is an approved difference); cycles through arrays, objects and closures: **new limitation** (legacy accepts) |
| `copy(x)` | shallow: a new outer instance/collection, contents shared; arrays and objects are values until Phase B (the value itself) | ΔL (v4.4+ deep; v4.2 contract) |
| `deepcopy(x)` | every reachable array, object, instance and collection copied once, sharing inside the graph preserved; callables, handles, futures keep identity; budget reserved first | ΔL for a collection reached twice (legacy duplicates); =L otherwise |
| Iteration (D5) | maps, sets and stacks iterate the membership at loop entry; maps take an explicit O(n) budgeted snapshot. Arrays and objects iterate their copy-on-write value; an array mutated while its loop runs is **refused** (`reference-guard:lent`): array snapshot iteration as a reference rule lands with Phase B | =L sets/stacks; ΔL maps (D-063 #7); RL arrays mutated in their loop |
| Keys (D6) | a map/set/stack as a map key or set member (lookups too) is refused before effects (`collection-mutable-key`); bool/number/string and callables unchanged; struct keys stay refused (`collection-identity-key`) | RL |
| Threads, parallel async | arguments, captures, globals and results as one alias-preserving `TransferGraph` copy (unchanged); instances cross too | ΔL (legacy refuses) |
| Cooperative async (D7) | arguments and the globals a task reads are copied as one alias-preserving graph **at task creation**, before any task state exists | equivalent to the parallel path (section 4) |
| Arrays/objects (D2) | values until Phase B; the whole-program forward-compatibility guard refuses every unit whose output could change once they are references (`reference-guard:<route>`) | RL (section 5; cost in section 13) |
| Object keys | an object as a map key or set member is refused (`collection-object-key`) | RL (legacy: run-time error) |

## 2. Implementation (files)

Shared value layer (reviewed with care: every VM path goes through it):

- `aggregate.{h,cpp}`: arrays/objects keep copy-on-write; `unshare` no longer
  clones instances or collections; struct stores in place.
- `aggregate_collection.{h,cpp}` (split out of `aggregate.cpp`, rt-lint): the
  collection mutators in place; `collection_shallow_clone`.
- `aggregate_store.h` (new, internal): the checked-build relocation schedule
  (D9), the self-store backstop, the identity summary on stores.
- `heap.h` comments; `may_contain_identity` counts instances and collections.
- `value_cycle.{h,cpp}`: `CycleTarget::Aggregate`; the target is recognised
  before the summary shortcut; `legacy_reference_cycle` (legacy's own rule,
  for its wording).
- `compare.cpp`: struct `==`/hash by identity; `same()` for instances and
  collections.
- `copy_graph.{h,cpp}` (new): `shallow_copy`, `deep_copy`, `deep_copy_tuple`
  (two phases: discover and budget, then shells filled children-first).

VM: `vm_keyed.{h,cpp}` (cycle refusal before map/set/stack stores, no clone
budget), `vm_struct.cpp` (instance receivers by value, cycle-checked
`FieldSet`; `SlotOf` removed from `opcodes.h`/`vm.cpp`), `vm_iter.cpp` (map
snapshot), `vm_coro_ops.cpp` (D7 copy at spawn), `vm_ops.cpp` (an instance's
binding type is legacy's "string"), `natives/json_methods.cpp` (copy/deepcopy),
`natives/collection_methods.cpp` (keys, `same`), `natives/builtin.cpp`
(`runtime.reference_cycle` catchable).

Compiler: `compiler_keyed.{h,cpp}` (receiver read, not taken: a catchable
error leaves every binding intact; `collection-loop-binding` removed;
`collection-mutable-key`), `compiler_structs.cpp` (receivers are instance
values; struct ids from the inference; fresh initialisers; construction in
expressions), `compiler_globals.cpp`/`compiler_assign.cpp` (`struct-value`
narrowed), `compiler.cpp` (guard after compilation), `compiler_expr.h`
(lambda bound fix in `field_in_loop_code`, which read past `loop_called_`:
the base crashed on `f := () => p.v`).

Inference: `infer.h` (`kKindStruct`, `kKindTagged`, `struct_ids`,
`nested_aggregates`), `infer_types.{h,cpp}` (allocation sites, `collection_ty`,
`holds_aggregate_below`), `infer_collection.cpp` (per-site contents, per
string-literal map key), `infer_walk.cpp`, `infer_for.cpp`,
`infer_record.cpp`.

Guard: `reference_guard.{h,cpp}`, `reference_guard_walk.{h,cpp}` (new).

## 3. Cycles (D1)

The check is the D-040 V16 walk with a reference-kind target: it runs before
every map value/key, set member, stack element and struct field store, costs
nothing for plain data (the summary bit), and follows arrays, objects, closure
cells and resource payloads. A closure over a **top-level** binding reads it
by name, so `m.set("f", () => m.size())` at the top level closes no heap cycle
and is accepted; inside a function the map is a captured cell and the store is
refused. Refused graphs are unchanged and leak nothing (`heap/cycle_refused*.f`:
4 and 40 refused attempts end with the same live heap).

Every refusal is catchable, including a store into a captured cell (an
assignment to a captured binding, a push into a captured array, an upvalue
store from a closure: `Vm::refuse_cell_cycle` in `vm_closure.cpp`; the
review found these were fatal). The check runs before `cell_rebind`, so the
cell keeps its value; `approved/cell_cycle_*.f` show the catch and the
unchanged binding, and `heap/cell_cycle_refused{,_many}.f` against
`heap/cell_cycle_control.f` (the same program without the refused stores)
end with the same live heap: no reference count or heap accounting changes.
Mutation control R16 (fatal again) is caught.

## 4. Cooperative async equivalence (D7)

Cooperative task code (CP22 admission, unchanged) cannot call a collection
method, touch a struct or capture a cell; it can move references around,
return them and read globals. The only sharing channels with the caller are
therefore the arguments, the snapshot of globals a task reads, and results.
At `Spawn` the arguments and those globals are copied as **one**
alias-preserving graph (`deep_copy_tuple`) before any task state is created,
exactly the parallel path's seed; a result can then only hold task-private
copies or fresh objects, as a parallel result does, and awaiting twice yields
the same object in both (`future->result`). Differential fixtures
(`refsem/equiv/*.f`) run each program under both schedulers and require
identical transcripts: aliased arguments, globals, nested graphs with
internal sharing, double await, `thread()`. Budget stop: `heap/budget_spawn.f`.
Handles and callables keep identity (the existing handle restrictions apply).
Cancellation releases the task's copies with its stack.

## 5. The forward-compatibility guard (D2, arrays and objects)

`rt/reference_guard.h` states the rule; it is a flow-insensitive analysis of
the whole linked unit (imports are linked into one unit, `module_link.h`, so
module code is ordinary code here). The first guard (`ad9d842f`) reasoned per
site and was unsound (section 13); this one is whole-program.

**Why the two models can differ.** In the copy model every holder (binding,
parameter, element, loop binding, native operand) holds its own value; in
Phase B holders of the same array/object share it. Output can differ only
when an object is mutated through one holder while another holder later
observes it. Captures are not holders of their own: a closure shares the
captured binding's cell in both models.

**The rule.** For every array/object binding B mutated in place anywhere
(every binding, in a persistent session):

1. *Exclusive* (as before): every inflow fresh (deep when B is mutated below
   its top), stored contents fresh, B never retained by another holder that
   outlives an operation (declaration, assignment, return, literal item,
   container store, an escaping parameter, an expression lambda's result),
   nor its parts when mutated below the top. Retention is widened: a
   parameter any closure captures escapes (it outlives the call in its cell);
   an argument to a callable value is retained; a native given a callable
   that is not a lambda literal retains its operands' parts.
2. *Lent* (`reference-guard:lent`): B or a part of it is lent when passed to
   a call's non-escaping parameter, to a native (and its callbacks) or
   iterated by `for`. The lending holder lives exactly as long as that call
   or loop. B may not be mutated during it: no mutation of B is written
   inside the call or loop (outside the lent expression, which is evaluated
   first), and if B is referenced from any body other than its declaring one
   (another function, method or lambda, or a later session statement) the
   call or loop may not run user code (a user function, a callable value, a
   native given a non-literal callable, or any such call written inside it).
   Code outside the region in B's own body cannot run during it (a recursive
   call has its own frame), so these are all the mutations that can.
3. *Parts*: a loop binding or a native callback's parameter is a part of its
   operand's root; a closure capturing it retains that part. Over a fresh
   iterable (literal, constructing call, fresh at every depth) the loop
   binding is an exclusive value of its own. Over a map, set or stack (or a
   copy of one, or a call returning one) it is an element of an unknown
   holder (round 2): mutating it is refused (`collection-element`), and
   retaining, capturing or lending it counts as an unknown holder.
4. *Unknown callers*: a user function used as a value, or any function in a
   session, may not mutate its parameters (`argument`); in a session, the
   top-level bindings count as mutated by later statements (round 2: not a
   function's locals; a recompile's capture initializer is the existing
   cell's value, not a new inflow); aggregates held by a
   struct instance (`this`, an implicit field, a field of an instance) are
   unknown roots (`unresolved` when mutated). A store into an instance's own
   field is an instance mutation (a reference in both models), not an
   aggregate one.

With 1 no second holder outlives an operation; with 2 no mutation runs
while a short-lived holder exists; 3 and 4 close the remaining holders. So an
admitted unit observes no object through two holders across a mutation, and
both models print the same. Earlier refusals keep their reasons (the guard
runs after compilation); within the guard, the exclusivity routes are
reported before `lent`.

Evidence (`tests/runtime2/refsem/`): the review's repros as `refuse/lend_*`,
`refuse/loop_*`, `refuse/function_value_mutates_parameter.f`, three
`modules/*` cases and four piped sessions; adversarial additions (argument
order through a user function and through a native, a callback that calls a
mutator, a closure returned from a callee's callee). `phaseb/*.f` and
`modules/reader_module` are admitted programs with their Phase B transcripts
derived by hand (`NAME.phaseb`, the reason in each fixture's comment); the
suite requires the VM to run them and print exactly that, and legacy to
agree. Mutation controls R13 (guard off), R17 (lent rule off), R18 (captured
parameters do not escape) are caught.

## 6. Sound inference under sharing

Per-binding element types were sound only because copies never shared. Now
each collection type carries its allocation sites; contents (map keys, map
values, set members, stack elements) are per site, map values also per
string-literal key; every alias of an object carries its site, so a store
through any alias is seen by every read. A collection of unknown origin, or
any read in a persistent session, is "anything". Struct field types were
already per struct type. Witnesses: `refsem/refuse/alias_*_inspection.f`;
mutant R7.

## 7. Evidence changed (citing D-NEXT-refsem)

- `collections/approved/` → `collections/parity/` (now legacy-equal):
  argument_copy, assign_copy, closure_copy, container_copy, deep_nested,
  mutual_insert, nested_extract, return_copy, same_struct, same_value,
  struct_field_copy; their value-semantics goldens kept in
  `collections/superseded/` with `INDEX.tsv`.
- `self_insert` and `parity/collection_keys` superseded (replacements named in
  `INDEX.tsv`); `parity/callable_keys.f` keeps the callable-key part.
- `refuse/loop_binding_mutation.f` → `parity/` (legacy-equal).
- `heap/budget_clone.f`: no clone exists; it must now fit the same budget as
  `budget_noclone.f`.
- `collections/mutants.sh`: M1-M4 and M10 retired (their defect is now the
  contract or their code is gone); M5 moved to `aggregate_collection.cpp`.
- cp25 `classification.tsv`: D-063 rows annotated (the earlier text kept), new
  rows `collection-mutable-key`, `reference-guard:*`.
- `compatibility.md`: superseded rows marked, new approved differences and
  limitations, the cycle limitation in its own section.
- Review round 1 (each file cites D-NEXT-refsem):
  `closures-s1/EXPECT` (g8_value_semantics → `fallback:reference-guard:inflow`;
  g1_aggregate_cycle and g1_index_cycle → `fallback:reference-guard:shared`,
  their cycle-witness coverage restored by the new g1_aggregate_cycle_cell.f
  and g1_index_cycle_cell.f, which still reach the cycle check);
  `for-s2/EXPECT` (g6_call_copy → `argument`; g6_element_modify,
  g6_element_modify_late, g6_push_each, g6_push_once, g9_million_write,
  t16_same_iter_modify → `lent`), for-mutant 2's witness moved from
  g6_push_once.f to the new g6_rebind_longer.f (rebinding is no mutation, so
  the guard admits it and the mutant's extra visits show);
  `capture_origins.py` (`GUARDED`: 24 cases refused before effects, with the
  legacy oracle still checked on NIFT_VM=0/1); `for_s2_bench.py` (write_for.f
  refused; the no-copy memory check on write_while.f); `vm_structs.py`
  (copy/print of an instance and four probes run on the VM: D4);
  `rt-diff-selftest-corpus/gen/m1/main.f` (three copies made explicit with
  `deepcopy` so the module-reproducer selftest still runs on the VM);
  `refsem/parity/iteration_snapshot_set_stack_array.f` →
  `iteration_snapshot_set_stack.f` (its array case is now
  `refuse/loop_array_push.f`).

## 8. D9

- rt-lint: all twelve findings fixed (file splits, `compiler_keyed.h`,
  `vm_keyed.h`, long lines, relocated loop statements and two small inference
  helpers, comment reflow); `python3 tools/rt-lint.py src/rt`: 0 problems.
- Checked-build relocation (`aggregate_store.h`): storage of at most 256
  elements moves on every mutation. Larger storage moves on a mutation when a
  reference or pointer into it was handed out since its last move (bit 6 of
  the header tag, `kStorageLent`, set by `array_items`, `object_entries`,
  `object_get`, `collection_items`) **and** the thread's relocation credit
  covers its size; otherwise once every size/256 + 1 mutations. That is the
  guarantee: "take a reference, mutate once, use it" faults at the use while
  the credit lasts, which ordinary VM work no longer spends: the VM's
  transient reads (element reads, sizes, iteration) use non-lending views
  (`array_view`, `collection_view`, `object_view`, `object_find`,
  `collection_item_count`), and `array_mut`/`object_slot_mut` do not count
  their own mutation as a hand-out (round 2; the round-1 text said "at every
  size", which held only while the credit lasted). The hand-out moves draw on a
  per-thread credit (2^21 elements to start, +512 per mutation), so the moved
  volume stays linear even when reads and writes of a huge container
  interleave. (The first schedule's claim that a stale reference into large
  storage "still faults within that many" mutations was too weak: the
  per-thread tick is shared across containers; the review's finding.)
  Stress fixtures keep 200,000 elements and their budgets. Stale-reference
  controls (`tests/rt/value_round2.cpp`; the `test-rt-value-sanitize` loop
  runs all twelve): array, object, string, map, set, stack, struct, and
  take-mutate-once-use on a 5,000-element array, map, set and stack and a
  600-entry object. VM-path controls (round 2,
  `make test-runtime2-refsem-stale-vm`, `tests/runtime2/refsem/stale_vm.cpp`):
  a 5,000-element map, set and stack built and read through a VM session
  (each insert followed by a VM read), and a 5,000-element VM-built array,
  each with and without a 20,000-element VM read-after-push churn first (which
  spent the whole credit while VM reads counted as hand-outs); a reference is
  taken, the container is mutated once through the VM (a session statement;
  for the array, `set_index`, the SetIndex opcode's implementation, since a
  session statement replaces a session-held array by copy-on-write), and the
  reference is used: all eight abort with heap-use-after-free, and the R19
  build misses all eight.
- Local ASan+UBSan evidence (drivers over the value-layer sources, `-O1`;
  round 1): 200,000 pure inserts take 1.9 s (array), 4.5 s (map), 7.1 s
  (set), 2.3 s (stack), against an extrapolated 770 s before; the worst case,
  a reference handed out before every insert, 7.1 / 13.5 / 20.3 / 6.9 s
  (linear). All twelve stale-reference controls abort with
  heap-use-after-free; with the hand-out rule compiled out (R19,
  `-DNIFT_REFSEM_MUTANT=19`) or relocation off the five large controls go
  undetected, so their detection is the new rule's. Earlier figures: Every stale-reference control (array, object,
  map, set, stack, struct, a 1,000-element array, a 5,000-entry map) aborts
  with heap-use-after-free. With relocation compiled out
  (`-DNIFT_RT_RELOCATION_MUTANT`) the three controls that do not grow their
  storage (object replacement, large array, large map) go undetected, so
  their detection is the schedule's; the growth controls are also caught by
  the natural reallocation.
- **What the stale-reference evidence shows, and does not (review round 3).**
  Three separate statements:
  (a) the sanitizer runs pass: every control above aborts with
  heap-use-after-free under ASan on the checked build, and the refsem suite
  passes on the sanitizer build (`test-runtime2-refsem-sanitize`);
  (b) deliberately broken builds are detected, per control: R19 (hand-out
  rule compiled out) is missed by all 8 VM-path controls and by the 5 large
  value-layer controls; the review's "reads count as hand-outs" mutant (VM
  transient reads lend again) is discriminated by the map controls (plain
  and churn, 2 of 2 missed) and the stack controls (2 of 2 missed), and NOT
  by the array and set controls (plain and churn still abort, 4 of 4), so
  only 4 of the 8 VM-path controls discriminate that mutant;
  (c) the instrumentation does not guarantee that every stale-reference
  mutation is detected (storage above 256 elements moves only while the
  credit lasts, otherwise every size/256 + 1 mutations); no claim of complete
  coverage is made. The array VM-path control mutates through C++
  `set_index` (the SetIndex opcode's implementation), not through a VM
  instruction stream.

## 9. Still refused, and why

- Cycles through arrays, objects and closures (no collector; D1).
- Arrays and objects mutated while lent, shared, or reached from code the
  guard cannot see (section 5): in particular an array mutated in its own
  `for` loop (Phase B iterates live), and a global array mutated by a helper
  function and passed to any user function (the guard has no call graph).
- An object as a map key or set member (`collection-object-key`).
- An array or object reached by iterating a map, set or stack, mutated
  through the loop binding (`collection-element`).
- Collections as keys or members (D6); struct identity keys (not established).
- Array/object programs whose meaning Phase B would change (D2 guard).
- Field access through a value that may be an instance or something else
  (`struct-value`): the field type of `next := ""` is string-or-struct to a
  flow-insensitive inference.
- Struct field access in loop code (`context:struct-field`, legacy L42) and
  construction in loop regions (`context:struct-new`), unchanged.
- Cooperative mode keeps `coro-struct`, `coro-capture` and the task-code
  instruction rules.
- Mutation through a non-binding receiver (`a[0].set(...)`, `o.m.push(...)`)
  stays refused (legacy: malformed expression).
- `stringify()`, `prettify()`, `highlight()`, in a unit or session that
  declares a struct, on a value whose inferred kinds include a struct
  instance (`presentation-struct:<method>`) or a container
  (`presentation-struct-contents:<method>`), whatever the receiver
  expression (review rounds 3-4): the VM's serializer cannot render an
  instance (no field privacy at run time) and inference does not track
  struct kinds in contents. Conservative: it also refuses containers that
  hold no instance (a known over-refusal, section 16). Compatibility gaps to
  eliminate (Phase B or a later VM checkpoint: struct rendering), not
  intended behaviour.

## 9a. Phase B requirements (recorded in review round 3)

- Every cycle-capable mutation of arrays and objects goes through one shared
  protocol: validate the complete proposed mutation (every value it stores,
  against the receiver), commit the mutation, then update bookkeeping
  (reference counts, heap accounting, relocation state). Phase A's array
  mutators do this case by case (the `CellTake` pre-check, the splice's
  single store); Phase B should not repeat that per mutator.
- Phase B separates static admission from runtime dispatch: a valid program
  must not be refused only because inference is imprecise (the guard's
  `lent` refusals of in-place loop updates, `presentation-struct`,
  `presentation-struct-contents`).
  Where inference cannot decide, the VM dispatches at run time with legacy's
  behaviour rather than refusing.

- Operation admission is decided from the semantic receiver and argument
  types through one operation-level admission interface (review round 4,
  owner), not from special cases per syntactic form: Phase A's refusals
  (`presentation-struct`, `struct-no-method`, `collection-receiver`,
  `builtin-receiver`) are scattered over the compiler's call paths. Not
  implemented in Phase A.
- Splice (and every array mutator) follows evaluate all arguments, validate,
  prepare without touching the original, commit once (section 16); Phase B's
  shared protocol keeps that order.

- Re-witness args/cmd mutant 5 (`tests/runtime2/args_cmd/mutants.sh`, the
  script invocation bindings made writable) through the const-mutation error.
  In Phase A it is caught by classification only (review of `2a1c228a`, F1):
  `args.push(<array or object>)` is refused by the guard on the real build
  and as `aliasing` under the mutant. A tripwire fails if any guard-only
  mutation form of `args` is admitted. Phase B's witnesses are the preserved
  cases 1-2, `tests/runtime2/refsem/pending/args_push_after_output.f` and
  `args_sort.f`, which must then reach the const-mutation error.

## 10. Open questions

1. Mutable identity keys (JavaScript `Map` semantics) for collections and
   instances (D6 follow-up).
2. Whether struct instances should keep crossing threads as copies (an
   approved difference) or be refused like legacy.
3. Phase B scope: the guard's refusals measured by the CP25-R inventory
   (section 11) are the starting list.
4. The guard's cost (section 13): keep it, narrow arrays and objects
   further, or bring Phase B forward (owner guidance: it is a temporary
   internal measure and is not grown further here).

## 11. CP25-R inventory (classify-all, 589 shared units; execute, 608 entries)

Re-collected on the round-1 tip (`inventory.py collect`, base `18720d14`
against the tip). Nine units change, the same nine as before round 1: the
whole-program guard adds no refusal on these corpora, and no execute entry
changes (0 of 608).

| Unit | Before | Now |
| --- | --- | --- |
| `vm-corpus/cp11/replay_alias.f`, `semantics/alias_place.f` | VM (D-040 witnesses) | `reference-guard:inflow` |
| `semantics/copy_at_call.f` | VM | `reference-guard:argument` (Phase B makes `f(a)`'s mutation visible) |
| `vm-corpus/cp11/arrays.f` | VM | `reference-guard:shared` (5/4 becomes 5/5 in Phase B) |
| `semantics/struct_reference.f` | `aliasing` | **VM**, legacy-equal |
| smoke `v42_struct_private_smoke/013-copy` | `struct-value` | **VM**, legacy-equal |
| smoke `v42_struct_private_smoke/012-cross-instance` | `struct-value` | `struct-private` (unchanged refusal family) |
| smoke `v44_root_path_corruption_reproducers/003-structarr`, `004-mapstruct` | `struct-construction` | `context:struct-new` |

`classification.tsv` rows: `reference-guard:*` (route `lent` added),
`collection-object-key` (new), `collection-mutable-key`, `context:struct-new`.
`--check` still reports the six pre-existing unclassified reasons of the base
(`rt-diff-corpus/async/fileops/main.f`).

## 12. Local runs

Review round 1, through the host's heavy-job gate. The full set ran on
`828d3484` (tree `5cabd8e4`); the next commit, `49f60534` (tree `164c3260`),
changed only tests and records (no source), and every gate it touched was
rerun there (marked *). The commit after it adds this section only.

| Gate | Exit |
| --- | --- |
| `make nift`*, `make test-rt-lint`* (0 problems) | 0, 0 |
| `refsem_a.py`* (256 checks), `weekend_collections.py` (273 checks) | 0, 0 |
| `refsem/mutants.sh` (R1-R18, 18 of 18 caught), `collections/mutants.sh` | 0, 0 |
| `test-runtime2-vm-default`, `-closures-s1` (54 cases), `-closures-s1-stress` | 0, 0, 0 |
| `test-runtime2-for-s2` (59 cases), `-for-s2-mutants` (M1-M7) | 0, 0 |
| `test-runtime2-cp22`, `-cp23` | 0, 0 |
| `test-runtime2-capture-origins` (82 cases), `-cp12c-probes` | 0, 0 |
| `test-runtime2-cp14b`*, `-cp14b-witnesses`* | 0, 0 (2 on `828d3484`: const_admission, fixed in `49f60534`) |
| `test-cp25r`, `test-runtime2-d040-witnesses`, `-vm-corpus-complete` | 0, 0, 0 |
| `vm_parity.py --gen 300 --rt-diff`, `vm_adversarial.py --count 600`, `vm_subset.py`, `vm_subset_gen.py --count 500` | 0, 0, 0, 0 |
| `vm_builtins.py`*, `vm_inventory.py`* | 0, 0 (1, 1 on `828d3484`, fixed in `49f60534`) |
| `vm_known_refused.py`, `vm_engine_selection.py`, `vm_former_replay.py --sample 600`, `vm_review_cp12.py`, `vm_golden.py` | 0, 0, 0, 0, 0 |
| `vm_modules.py --gen 100`, `vm_structs.py --gen 100`, `vm_globals.py --gen 200`, `semantics_modules.py` (both dirs) | 0, 0, 0, 0 |
| earlier in the round: `test-runtime2-cp16-n1`, `-kind-bits`, `-cp12c-mutant-controls`, `-closures-s1-mutants`, `-cp13b` | 0 each |

ASan+UBSan value-layer drivers (local, `-O1`, a subset of the value sources;
section 8): `value_round2` passes; all twelve stale-reference controls abort
with heap-use-after-free; the R19 and relocation-off builds miss the five
large controls; 200,000-element stress as in section 8. Not run locally: the
full `test-rt-vm` and sanitizer lanes (Linode), and the rt-diff selftest,
whose timeout and interleave checks fail on this host for the base binary
too (its module-reproducer check needs `gen/m1`, fixed in section 7).

### Integration review: presentation methods on possibly-collection values

`e := env(); print(e.stringify().length() > 0)` runs on weekend/r2
(8b3cd02f) but was refused as `type:length` here (vm_host.py, recipe line
12): since the collections base (D-063, 18720d14) the inference routes any
receiver whose kinds include a collection bit to the collection methods and
returns "anything" for a receiver that is only possibly a collection; an
`any` type now carries the collection bits, so `stringify()` on an object of
unknown fields lost its string result. The presentation methods
(`stringify`, `prettify`, `highlight`, no arguments) keep their string
result for such receivers (`infer.cpp`); a receiver that is exactly a
collection still uses the collection rules. Regression fixtures:
`refsem/parity/presentation_*.f` (the five vm_host contexts, an object, a
map), each checked against the weekend/r2 binary where it exists there.

### Round 2 runs

Full set on `44f9a476` (tree `5788b819`), through the heavy-job gate, every
exit 0: `make nift`, `test-rt-lint` (0 problems), `refsem_a.py` (312
checks), `weekend_collections.py` (273), `refsem/mutants.sh` (R1-R18, R20,
R21: 20 of 20 caught), `collections/mutants.sh`,
`test-runtime2-refsem-stale-vm` (8 of 8, R19 misses 8 of 8),
`test-cp19-functional`, `test-cp19-session-cli`, `test-runtime2-vm-default`,
`-closures-s1`, `-closures-s1-stress`, `-for-s2`, `-for-s2-mutants`, `-cp22`,
`-cp23`, `-capture-origins`, `-cp12c-probes`, `-cp14b`, `-cp14b-witnesses`,
`test-cp25r`, `-d040-witnesses`, `-vm-corpus-complete`, `-cp16-n1`,
`-kind-bits`, `-cp12c-mutant-controls`, `-closures-s1-mutants`, `-cp13b`, and
`vm_parity.py --gen 300 --rt-diff`, `vm_adversarial.py --count 600`,
`vm_subset.py`, `vm_subset_gen.py --count 500`, `vm_builtins.py`,
`vm_known_refused.py`, `vm_engine_selection.py`, `vm_former_replay.py
--sample 600`, `vm_review_cp12.py`, `vm_golden.py`, `vm_inventory.py`,
`vm_modules.py --gen 100`, `vm_structs.py --gen 100`, `vm_globals.py --gen
200`, `semantics_modules.py` (both dirs). Static audit (1,793 `.f`): no
outcome change against the round-1 tip on any file both contain (the 30 new
files are this round's fixtures). CP25-R inventory: unchanged from round 1
(section 11), 0 of 608 execute entries changed. The next commit adds one
session regression case (`session_saved_error`) and this text, no source;
`refsem_a.py` was rerun on it. Not run locally: the full `test-rt-vm` and
sanitizer lanes; the Linode run on `964901ae` is diagnostic evidence only.

## 13. Review round 1 (review of `ad9d842f`)

| Finding | Fix | Evidence |
| --- | --- | --- |
| BLOCKER 1: the guard missed mutations while a binding is lent (globals, captured cells, modules, nested calls, sessions) and closures capturing a parameter | whole-program rule (section 5), route `lent`, captured parameters escape | the review's g01, g02, h01, h04, h07, i08, j01, j06, mod2-mod4 and a piped session refused (`refuse/lend_*`, `loop_*`, `modules/`, sessions); `phaseb/` admitted with Phase B transcripts; R13, R17, R18 caught |
| HIGH 2: cycles through captured cells were fatal | `Vm::refuse_cell_cycle`: catchable, cell unchanged | review k05, k07, k08, k09 (`approved/cell_cycle_*`); equal live heaps against a no-store control; R16 caught |
| HIGH 3: stale EXPECT files | `closures-s1/EXPECT`, `for-s2/EXPECT` cite D-NEXT-refsem; cycle witnesses restored (`g1_*_cycle_cell.f`); static audit below | `test-runtime2-closures-s1`, `test-runtime2-for-s2` (section 12) |
| MEDIUM 4: D5 for arrays claimed | records corrected (array snapshot with Phase B); in-loop mutation refused | `refuse/loop_*`, `for-s2`, `capture_origins` |
| MEDIUM 5: large-storage stale references caught only every size/256+1 mutations | hand-out bit and credit (section 8) | five large take-mutate-once-use controls caught; R19 and relocation-off miss them; 200,000 inserts linear |
| MEDIUM 6: re-inventory | section 11 | — |
| LOW 7: object key mislabelled | `collection-object-key` | `refuse/key_object.f`, `collections/refuse/object_member.f` |

**Static audit** (every `.f` under `tests/`, 1,763 files, `NIFT_VM=classify`
with the base `18720d14` and the tip): 1,017 compile on the base; 85 of them
are refused on the tip, 28 refused ones compile, 4 change reason. Of the 85,
40 are this candidate's own (new) refusal fixtures and re-based D-063
fixtures; the other 45 are `cp12c-probes` (30), `for-s2` (7), `closures-s1`
(3), `semantics` (2), `vm-corpus/cp11` (2) and `for-s2-bench` (1), every one
an array mutated while its loop or a call holds it, or a mutated alias of
it. Each is checked against its EXPECT, table or golden (section 7); the
`cp12c-probes` harness accepts refusals, `capture_origins.py` lists its 24
cases explicitly.

**Cost of the guard (owner guidance: a temporary internal measure).** On the
release corpora (section 11) the guard costs nothing beyond the D-040
witnesses. In `tests/` it refuses the loop-mutation and aliasing probes
above. Common workloads, probed directly (base / tip):

| Shape | Base | Tip |
| --- | --- | --- |
| build with `push`, then `for` to sum; `while` index updates; `filter`/`map` chains; a local passed to a reading helper; a global appended to from a helper called in a loop; rows of rows | VM | VM |
| `for (x : a) { a[i] = x * 2; i += 1 }` (update the iterated array in place) | VM | refused (`lent`) |
| a global array mutated by helper functions, then passed to a function that only reads it | VM | refused (`lent`) |
| `for (row : grid) { row.push(0) }` (the loop binding is a place: it writes through, legacy-equal) | VM | refused (`lent`) |

The last three print the same in Phase B, but the guard cannot show it
without index reasoning (which positions a loop body writes), a call graph
(which functions a call can reach) and a model of the loop binding as a
place rather than a second holder. That is new inference machinery,
so per the owner's guidance the guard is **not grown further**: the choice
between keeping it, narrowing arrays and objects further, and bringing Phase
B forward is the owner's (section 10).

## 14. Review round 2 (review of `964901ae`; the last guard round)

| Finding | Fix | Evidence |
| --- | --- | --- |
| BLOCKER: a loop over a map, set or stack bound "exclusive" values (`fresh` short-circuit) | collection loop bindings are elements of an unknown holder (section 5, rule 3) | the review's t4, m3, k3, m4, o4, o2, n5, m1, m6, m8, m9, t3, k2 (`refuse/coll_review_*`) and the owner's variations: nested iteration both ways, extraction, argument, closure capture, a loop over a collection parameter, copy/deepcopy (`refuse/coll_*`); read-only loops admitted (`phaseb/coll_*`); R20 caught |
| HIGH: a caught insert/splice cycle refusal left the captured array's cell empty | `CellTake` checks the stored value before the take (`vm_closure.cpp`; array_mutation, SetIndex, statement push, global take) | review c/cx, c6, c7, c9; `approved/cell_cycle_mutators.f` (state after each catch, later mutations); `heap/cell_mutator_*` (4 and 40 rounds against a no-store control: the same live heap, 0 at stop); R21 caught |
| MEDIUM: the stale-reference controls did not represent the VM path | non-lending internal views; VM-path controls (section 8) | `test-runtime2-refsem-stale-vm`: 8 of 8 caught, R19 misses 8 of 8; value-layer controls 12 of 12, R19 misses the 5 large; 200,000-element stress unchanged |
| LOW: `gen/m1` claimed to be generated | marked hand-maintained with the reason (header, corpus README) | — |
| Linode `test-cp19-functional` (session.cpp 277/278) | a Phase A defect: 16-bit session kind fields truncated kKindStruct, so an instance binding came back as a string and every later use was refused (the Error value was never caught); also instance/collection callable shapes. Kinds are 32-bit now | `.build/cp19/session` 405 of 405; expectations changed with citations: an instance held across a mutation sees it (D-NEXT-refsem #2), and two session-array blocks (the guard) |
| Linode `test-cp19-session-cli` (`array_callback`) | the conservative session guard; `GUARDED` with citations in `session_cli.py`, `session_callable_cli.py` (8 cases), one rewritten case in `session_refusal_cli.py` | all cp19 session CLI harnesses pass |


## 15. Review round 3 (review of `22685af2`; owner addendum: bounded correction)

| Finding | Fix | Evidence |
| --- | --- | --- |
| HIGH: a refused splice with count > 0 still removed elements | a captured-array splice with a replacement removes and inserts on a copy and stores once; the store checks the complete result against the receiver before the cell changes (`compiler_array_mutation.cpp`) | review repro-splice-cycle.f (`[1,2,3]` both times), cy/h06.f, repro-splice-cycle-closure.f, cy/sp40.f (200, was 120); `approved/cell_splice_atomic.f` (statement, expression, closure, captured cell; several replacements where only the last cycles: the receiver and its nested array render identically after each catch); `heap/cell_splice_*` (4 and 40 rounds against a no-splice control: 56/560/56, equal live heaps); R22 (removal stored first) caught |
| MEDIUM: `stringify()` on a possibly-struct receiver admitted, then exit 70 after output | refused at compile time, `presentation-mixed-struct:<method>`, at the receiver's position (`compiler_call.cpp`); unconstrained (top) values keep the generic path | review repro-stringify-struct-mixed.f, inf/i04.f, i05.f, j02.f (`refuse/presentation_mixed_struct_review_*`); struct+array, struct+map, struct+set x stringify/prettify/highlight, with and without `.length()` (18 fixtures); function, closure and module forms; every fixture prints first and the runner requires empty stdout; j01.f stays admitted (`parity/presentation_collection_array_mix.f`); `presentation_*` pass |
| LOW: stale-reference limitation wording | section 8, (a) (b) (c) | the review's `stale.log` and mutant logs |
| Phase B notes | section 9a | — |

Mutator audit (check first, then mutate): insert, statement and expression
push, element store: the stored value is checked before the take (round 2);
splice: one store of the complete result (this round); map set, stack push,
struct field store: checked before the store (R14 and the `cycle_*` parity
fixtures; a refused field store leaves the field unchanged). A top-level
array is read by name from a closure, so storing a value that refers to such
a closure into it closes no cycle (legacy-equal).

### Round 3, second part: containers holding struct instances (coordinator)

Phase A admits struct instances inside containers; on `84d67337`
`[C()].stringify()` printed earlier output and then exited 70 (weekend/r2
refused the program at the construction). Every built-in or VM operation
that walks container contents was probed with an instance inside, VM against
legacy, each program printing first:

| Operation | Before the fix | Now |
| --- | --- | --- |
| `stringify`/`prettify`/`highlight` on an array, object, map, stack, nested (array of map of instance), in a function, closure, module, session | **exit 70 after output** | refused before effect, `presentation-struct-contents:<method>` |
| the same on a set | refused (`collection-identity-key` at the add) | unchanged |
| `print(a)` of a container or instance | legacy-equal (both: "value is not directly renderable" after `before`) | unchanged |
| `"x" + a`, `"x" + C()` | refused (`type:+`) | unchanged |
| `"x${a}"` | legacy-equal (literal text) | unchanged |
| `==`/`!=` on arrays, objects, maps holding instances (nested too) | legacy-equal (identity per element) | unchanged |
| `copy`, `deepcopy`, `reverse`, `filter`, `slice`, `length`, object `keys` | legacy-equal | unchanged |
| map `keys()` (round 4 correction) | refused (`method:keys`) | unchanged |
| array `contains(c)` with an instance (round 4 correction) | refused (`collection-argument:contains`) | unchanged |
| `[C()] + [C()]` | refused (`type:+`) | unchanged |
| `env().stringify()` in a unit that declares a struct (an over-refusal of the conservative rule) | legacy-equal | refused, `presentation-struct-contents:stringify` |
| `sort`, `unique`, `join`, `max` | refused (`collection-element:*`) | unchanged |
| `indexOf` with an instance | refused (`collection-argument:indexOf`) | unchanged |
| an instance as a map key or set member, `contains` | refused (`collection-identity-key`) | unchanged |
| `includes`, `sorted`, `flat`, `values`, `to_string`, `string()`, `json_stringify`, `write_file` | refused (not VM methods, or legacy errors too) | unchanged |
| reading an instance back (`m.get`, `st.pop`, `a[0]`, `type`) | legacy-equal | unchanged |
| an array of instances captured by a `thread` | VM copies (open question 2); legacy fails in the thread body | unchanged (approved difference, not a crash) |

The only crashing path *in this table* was the presentation serializer on
a container. **Corrected in round 4 (F1):** a receiver that is exactly an
instance but not a plain name (`C().stringify()`, `a[0]`, `m.get(k)`,
`st.pop()`, a call result, a loop element, a session binding) also reached
the serializer and exited 70; see section 16. Fix (b), a
compile-time refusal, was chosen over (a), VM rendering: legacy's struct form
skips private fields, and privacy exists only in the compile-time plan, not
on the run-time instance. Rendering would mean carrying a per-type privacy
mask on every instance (heap layout, thread transfer, sessions) and matching
legacy's pretty form (a trailing comma after every field). The refusal is a
few lines at the two places presentation builtins are compiled.
Fixtures: `refuse/presentation_struct_contents_*` (array, map, set, stack,
object, nested, the three methods, `.length()`, function, closure),
`modules/presentation_struct_contents`, session `session_struct_contents`,
and `parity/presentation_struct_scalars.f` (still admitted: scalars,
equality, copies, reads).

Owner guidance (folded in): both presentation refusals of this round
(`presentation-mixed-struct`, `presentation-struct-contents`) refuse
programs legacy runs; they are compatibility gaps to eliminate (VM rendering
of struct instances), not intended behaviour, and are listed as such in
`compatibility.md` and `classification.tsv`. Supporting instances in the
serializer needs field privacy on the run-time instance (a heap-layout and
transfer change), which is outside a bounded correction, so the narrow
pre-effect refusal was chosen and no static analysis was added to compensate.

Splice identity (owner): the splice copy is a shallow copy of the array, so
every nested map, set, stack and instance in the proposed result is the same
object as in the receiver, and the receiver's cell is untouched until the
single store. `approved/cell_splice_identity.f`: after a successful splice the
elements read back are `same()` as the originals and a mutation through an
alias shows; after a refused splice the receiver has its length and elements,
each nested element is still `same()` as before with its contents unchanged.

## 16. Review round 4 (review of `2640ab48`; owner-approved F1-F3)

| Finding | Fix | Evidence |
| --- | --- | --- |
| F1 HIGH: an exact-instance receiver that is not a plain name skipped both refusals and exited 70 after output | one rule on the generic presentation path from the receiver's inferred kinds: in a unit or session that declares a struct, kinds including a struct instance refuse (`presentation-struct:<method>`, renamed from `presentation-mixed-struct`), containers refuse (`presentation-struct-contents:<method>`); position: the receiver | the review's pr/ and au/ sets, ses/t1-t5, mod/m1-m4: no internal failure; `refuse/presentation_struct_{temporary,index,map_get,stack_pop,fn_return,fn_arg,loop_element,loop_binding,closure_result,struct_scalar}.f`, `modules/presentation_struct_expression`, session `session_struct_value` |
| F2 HIGH: a throwing splice argument left a captured array whole but removed elements from other arrays | every splice evaluates the receiver and all arguments, validates (bounds, replacement kind, cycle via the take's pre-check), prepares the result on the taken value and commits with one store (`compiler_array_mutation.cpp`; `ArrayMutate` c=2 validates the replacement before the removal) | `approved/splice_argument_order.f` (plain local, captured, global, inside a closure, statement and expression: all `[1,2,3]`; the argument's own push to `log` stays); R23 (only captured splices evaluate first) caught; R22 caught |
| F3 LOW: records | over-refusal named; the program and its valid output preserved as `refsem/pending/` (checked: legacy output unchanged, VM refusal before effects); audit corrections above | — |

Splice order (coordinator ruling under the owner's correctness-over-legacy
policy, 2026-11-01, owner-approved; the owner may reverse it): 1. evaluate
the receiver and every argument; 2. validate bounds, replacement values and
cycles; 3. prepare the result without touching the original; 4. commit once.
If evaluation or validation fails, the array is unchanged, captured, global
or local. Side effects performed while an argument is evaluated are **not**
rolled back: atomicity covers the splice mutation only. Legacy removes before
it evaluates the replacement (an approved difference).

**Mutator order** (a throwing argument; plain local, captured, global, inside
a closure; VM against legacy):

| Mutator | Throwing argument | VM | Legacy |
| --- | --- | --- | --- |
| `push`, expression `push` | the value | unchanged | unchanged |
| `insert` | the index or the value | unchanged | unchanged |
| `remove` | the index | unchanged | unchanged |
| element store `a[i] = v` | the value | unchanged | unchanged |
| element store | the index | refused (`index-target`) | unchanged |
| `splice` | the start or the count | unchanged | unchanged |
| `splice` | the replacement (statement or expression) | **unchanged (was: removed, except captured)** | removed (`[3]`) |
| `sort` with a comparator | the comparator | refused (`method:sort`) | unchanged |
| map `set` | the key or the value | unchanged | unchanged |
| set `add`, stack `push` | the value | unchanged | unchanged |
| object field store `o["k"] = v` | the value | refused (`index-target`) | unchanged |

Only `splice` evaluated an argument after a partial mutation; no other
mutator changed. (The captured forms of most array mutators are refused by
the guard here, `reference-guard:shared`, as before, and `set`/`add` inside a
closure with an untyped key are refused, `collection-object-key`; mutator run-time errors
such as `insert: index out of range` are fatal on both engines.)

**Sweep for other internal failures after effects.** Presentation of every
receiver form above with structs: none left. Presentation of shared and host
handles: a `mutex()` or `channel()` value, alone or inside an array, map or
object, still stops after output with an internal failure (`unexpected
operands ... built-in stringify`). This is **pre-existing**: weekend/r2
(`8b3cd02f`) does the same, and it does not involve structs; it is reported
for a separate decision, not fixed here. Atomics, bytes, files, functions,
Errors and tasks are refused or give ordinary errors.

**Known over-refusal (F3).** `tests/runtime2/diagnostics/changes/ret_multiline_http_shape.f`
(the nift-packages/http `capabilities()` shape: a struct method returns an
object of scalars, then `.stringify()`) compiled on `84d67337` and is refused
since `bd094496` (`presentation-struct-contents:stringify`). A temporary
Phase A limitation Phase B must eliminate; the program and its valid output
are preserved in `tests/runtime2/refsem/pending/` (the original stays where
it is). `test-runtime2-diagnostics`: its `changes --expect new` part reports
this case as a fourth failure (weekend/r2 fails three there; the baseline and
`--expect old` parts fail identically on both, 31 and 9). The target is not
part of `test-rt-vm` (nor of `test-rt-vm-sanitize`) and fails on weekend/r2
already, so no expectation was changed.

## 17. Integration onto weekend/r2 (`integ/refsem-a`, merge of `73b327e5` into `9ee77096`)

The merge carries Phase A and the weekend collections base it builds on.
Conflicts (resolved without changing either side's contract): `compiler.h`
(the session symbol keeps args/cmd's `invocation` flag and takes Phase A's
32-bit KindSet `params`), `compiler_loops.cpp` (Phase A's verbatim move of
`while_loop`), `classification.tsv` (packages' and coro-module's rows,
Phase A's collection rows), `compatibility.md` and `decisions.md` (both
sides' additions). Follow-up: the coro-module decision, D-063 on weekend/r2,
is renumbered D-064 (D-063 is the collections decision that Phase A cites
throughout).

Interactions checked (combined fixtures in `tests/runtime2/refsem/`):

- **Packages.** A package's exported map, set, stack and struct instance keep
  their identity across the boundary (`same()`, mutations through the
  package's functions visible to the importer); `copy`/`deepcopy` of an
  imported map are independent of later mutations; legacy-equal, and Phase B
  keeps it (`modules/package_reference_exports`, with its `.nift/` store).
  The runner copies a module case's directories now.
- **Coroutines with modules (D7).** An async task given an imported module's
  map receives a copy made at creation (`modules/coro_module_copy_at_creation`:
  neither side sees the other's later mutation). Weekend/r2 refused the unit
  (`coro-module-capture`, no `map()` there); legacy refuses the map as
  non-transferable (an approved difference, D-NEXT-refsem #5). Passing a
  module's struct instance stays refused (`coro-module-capture`, as on
  weekend/r2). The cooperative scheduler keeps coro-module's whole-unit
  refusal for linked units (its documented unpositioned refusal).
- **`args`/`cmd` and the guard.** `a := args; a.push(x)` is refused before
  any effect (`reference-guard:inflow`, `refuse/args_copy_mutated.f`):
  legacy's `a` aliases `args` (it grows too), weekend/r2 ran it with a copy
  (`args` unchanged, unlike legacy). Reading `args` (a call argument, a loop,
  a value stored in a map, `cmd`) stays admitted and legacy-equal
  (`parity/args_read_only.f`). `args` extracted from a map and mutated is
  refused the same way.
- **Diag-positions.** Every refusal fixture of Phase A (refsem, collections,
  modules) carries a position (no "the VM gave no exact position"), at the
  refused node (spot-checked across the guard, collection and presentation
  reasons).

Pre-flight (classify, the merged binary against `9ee77096` and `73b327e5`):
in `tests/runtime2` only the four new combined fixtures differ from both
parents (each needs both sides' features); every other change matches one
parent. Release corpus (CP25-R): against `9ee77096` exactly Phase A's own
changes (4 newly refused, the D-040 witnesses; 19 newly admitted); against
`73b327e5` exactly weekend/r2's; 0 of 608 execute entries change, and no
unit or entry differs from both parents.

Observed, pre-existing on both parents, not interactions: a module function
parameter named like an importer's top-level binding is refused
(`import-scope`), and a string-literal argument to an imported function
after a first call is refused (`string-arg-type`).

### Integration test corrections (owner rulings, 2026-11-01)

- CP19 `unrelated-callback-shadow` and cp14b (`splice_throw`, the design
  mutation lane): test-only commits `fdcdd0ed`, `104f83cb` on `cand/refsem-a`,
  merged here; originals in `tests/runtime2/refsem/pending/`. The standalone
  `73b327e5` Linode run's cp19 and cp14b failures stay recorded as failures of
  that tip.
- args/cmd x the reference guard (Option 2: the guard stays frozen), four
  `test-runtime2-args-cmd` cases, test-only, on this branch (they exist only
  in the combined tree):
  1. `print("e"); args.push("z")` and 2. `args.sort()`: refused before any
     effect (`reference-guard:unresolved`) where legacy raises "cannot mutate
     const array: args" after earlier output. A temporary
     diagnostic-classification difference; **correcting the misleading
     diagnostic is a pre-release obligation**.
  3. A loop over `args` in a piped `-i -e` session (`reference-guard:shared-part`):
     a temporary Phase A over-refusal; Phase B must admit it.
  4. `b := args; b.push("q")` (`reference-guard:inflow`; legacy aliases `args`):
     Phase B must establish the final reference-and-immutability contract
     (aliasing must not bypass a genuinely immutable binding; a successful
     mutation is not assumed right).
  Originals and legacy's behaviour: `tests/runtime2/refsem/pending/args_*`.
  Coverage: `args_cmd.py` mutants 1, 2, 3 and 6 are still caught; mutant 5
  (the script bindings writable) is caught by classification
  (`args.push(args)`: refused by the guard, not as `aliasing`), with a
  tripwire over the guard-only forms (section 9a). Its only witnesses were cases 1 and
  2, and no script-unit mutation of `args` reaches the const check while the
  guard refuses every one (twelve mutation forms probed, real and mutant
  builds identical); see the integration report.
