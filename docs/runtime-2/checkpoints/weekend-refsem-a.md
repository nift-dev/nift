# Reference semantics, Phase A (cand/refsem-a)

Status: **candidate** for independent review; not integrated. Branch
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
| Iteration (D5) | arrays, maps, sets and stacks iterate the membership at loop entry; maps take an explicit O(n) budgeted snapshot | =L arrays/sets/stacks; ΔL maps (D-063 #7) |
| Keys (D6) | a map/set/stack as a map key or set member (lookups too) is refused before effects (`collection-mutable-key`); bool/number/string and callables unchanged; struct keys stay refused (`collection-identity-key`) | RL |
| Threads, parallel async | arguments, captures, globals and results as one alias-preserving `TransferGraph` copy (unchanged); instances cross too | ΔL (legacy refuses) |
| Cooperative async (D7) | arguments and the globals a task reads are copied as one alias-preserving graph **at task creation**, before any task state exists | equivalent to the parallel path (section 4) |
| Arrays/objects (D2) | values until Phase B; the forward-compatibility guard refuses every unit whose output could change once they are references (`reference-guard:<route>`) | RL (section 5) |

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

`rt/reference_guard.h` states the rule. In short, a binding that may hold an
array or object and is mutated in place must be exclusive: never copied out
(declaration, assignment, return, literal item, container store, an escaping
parameter, an expression-bodied lambda's result), its parts never copied out
when it is mutated below the top, and every inflow fresh (literal,
constructing built-in or method, user function whose returns are fresh,
`deepcopy`, `copy` for top-level mutation); a mutated parameter needs fresh
arguments at every call site or the `x = f(x)` idiom. Runs after
compilation, so earlier refusals keep their reasons. Routes: `inflow`,
`argument`, `shared`, `shared-part`, `contents`, `lambda-parameter`,
`unresolved`. The six D-040 witnesses are refused now (`d040_witnesses.py`
note; `vm-corpus/cp11/EXPECT`, `vm_review_cp12.py`, `vm_engine_selection.py`
updated; `tools/rt-diff/policies/d040-vm.json` retired from `vm_parity.py`,
the file kept).

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

## 8. D9

- rt-lint: all twelve findings fixed (file splits, `compiler_keyed.h`,
  `vm_keyed.h`, long lines, relocated loop statements and two small inference
  helpers, comment reflow); `python3 tools/rt-lint.py src/rt`: 0 problems.
- Checked-build relocation (`aggregate_store.h`): storage of at most 256
  elements moves on every mutation (deterministic stale-reference detection
  at the next use); larger storage once every size/256 + 1 mutations (a stale
  reference still faults within that many). Stress fixtures keep 200,000
  elements and their budgets. Stale-reference controls extended
  (`tests/rt/value_round2.cpp`: map, set, stack, struct, large array; the
  `test-rt-value-sanitize` loop runs all eight).
- Local ASan+UBSan evidence (a driver over the value-layer sources, `-O1`):
  200,000 inserts take 1.5 s (map), 8.5 s (set), 3.2 s (stack), against an
  extrapolated 770 s before. Every stale-reference control (array, object,
  map, set, stack, struct, a 1,000-element array, a 5,000-entry map) aborts
  with heap-use-after-free. With relocation compiled out
  (`-DNIFT_RT_RELOCATION_MUTANT`) the three controls that do not grow their
  storage (object replacement, large array, large map) go undetected, so
  their detection is the schedule's; the growth controls are also caught by
  the natural reallocation.

## 9. Still refused, and why

- Cycles through arrays, objects and closures (no collector; D1).
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

## 10. Open questions

1. Mutable identity keys (JavaScript `Map` semantics) for collections and
   instances (D6 follow-up).
2. Whether struct instances should keep crossing threads as copies (an
   approved difference) or be refused like legacy.
3. Phase B scope: the guard's refusals measured by the CP25-R inventory
   (section 11) are the starting list.
4. Imported (module) bindings holding arrays: the guard treats an
   unresolved holder as shared whenever the unit mutates an array or object
   in place (conservative); a module-aware rule is Phase B work.

## 11. CP25-R inventory (classify-all, 589 shared units; execute, 608 entries)

Against the base candidate `18720d14`, ten units change:

| Unit | Before | Now |
| --- | --- | --- |
| `vm-corpus/cp11/replay_alias.f`, `semantics/alias_place.f` | VM (D-040 witnesses) | `reference-guard:inflow` |
| `semantics/copy_at_call.f` | VM | `reference-guard:argument` (Phase B makes `f(a)`'s mutation visible; predicted) |
| `vm-corpus/cp11/arrays.f` | VM | `reference-guard:shared` (5/4 becomes 5/5 in Phase B; predicted) |
| `semantics/struct_reference.f` | `aliasing` | **VM**, legacy-equal |
| smoke `v42_struct_private_smoke/013-copy` | `struct-value` | **VM**, legacy-equal |
| smoke `v42_struct_private_smoke/012-cross-instance` | `struct-value` | `struct-private` (unchanged refusal family) |
| smoke `v44_root_path_corruption_reproducers/003-structarr`, `004-mapstruct` | `struct-construction` | `context:struct-new` (construction in a loop region; new row) |

`collection-mutable-key` is not seen on the corpora. `--check` still reports
the six pre-existing unclassified reasons of the base
(`rt-diff-corpus/async/fileops/main.f`: `coro-blocking-host:file`/`rm`,
`method:open/write/save/close`); `context:struct-new` is now classified.

## 12. Local runs

Recorded in the candidate report (exit codes of `make nift`, `test-rt-lint`,
the suites and mutants, `test-runtime2-vm-default`,
`test-runtime2-closures-s1-stress`, `test-runtime2-cp22`,
`test-runtime2-cp23`). The full `test-rt-vm` and the sanitizer lanes run on
Linode on the frozen tip.
