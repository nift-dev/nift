# B2: the reference representation (cand/b2-refrep)

Status: **candidate**, not reviewed, not integrated. Branch `cand/b2-refrep`, built on
`weekend/r2` @`919b5669` (B1 integrated). Plan: Phase B plan §3, §5.1-5.4, §5.8 and §6 "B2";
owner decisions of 2026-11-02/03 (B2 requirements A-F); coordinator rulings of 2026-11-03
(below). One checkpoint in ordered milestone commits (none certifies the checkpoint; heavy
validation is on the Linode, owner ruling of 2026-11-03):

| Commit | Content | Sections |
| --- | --- | --- |
| `e30bd6c9`, `a85e1f4a` | commit 1: representation switch, A, C, cycle check, validation order, F | 1-12 |
| `e65d4b64`, `f01622cb`, `12f486bc` | 1b: test migration (and two late ports) | 13 |
| `67527994` | test infrastructure: CP18 sanitizer source list | 13.6a |
| `497e145f`, `4c08861a`, `ec7c885a`, `ea082292` | commit 2: B, D, probe isolation (and fixes) | 14 |
| `1b152eaa` | record: commit-2 gates, configurations | 13.8, 14.5 |
| `8599dbcb` | rulings 5 and 6: identity and probe coverage | 15 |
| `44734273` | commit 3: E, sort protection | 16 |
| `94bfce4b` | record: commit table, component map (**reviewed tip: REJECTED**, narrow) | - |
| `4a9df426`, `8835c252`, `534f9af8` | review repair: H1/H2, M1, M2, L4-L8; record (re-review: ACCEPT-WITH-CONDITIONS) | 17 |
| the commits after `534f9af8` | re-review conditions C1-C3 and the LOW item; record | 18 |
| the commit after `eba6ca1f` | re-check R1 (sort key on the commits), R2 (test-access lint), R3 carried; record | 19 |

### Record map by component (owner layout)

| Component | Sections |
| --- | --- |
| Representation switch | 1 (contract), 2 (clone-site audit), 3 (deleted machinery), 8 (expectation changes), 12 (B5 notes) |
| Session atomicity (A) | 4 |
| Immutability (C) | 5 |
| Validation order and cycles | 6, 7 |
| Test migration (1b) | 13.1 (retirement table), 13.2 (allocation-failure coverage), 13.6b, 13.8 (configurations) |
| Single mutation path (B) | 14.1 (structure, compile-fail and its locale fix, lint, M-B2), 17.2 |
| Throw safety (D) | 14.2 |
| Self-splice and aliasing | 13.3 |
| Sort protection (E) | 16, 17.3 (typed errors) |
| Review of `94bfce4b` (REJECT) and repair | 17 (constants re-audit and case table 17.1, publication boundary 17.2, carry-forward 17.5, evidence by tree 17.6) |
| Probe isolation | 14.3, 15 (probe coverage), 13.8 |
| Performance (F) | 9 (commit 1), 13.6 (1b), 14.5 (commit 2) |
| Gates, mutants, R19, pre-existing failures | 10, 13.4, 13.5, 13.6, 13.6a, 14.5, 16.1 |

The Phase A guard stays on, legacy (`NIFT_VM=0`) is unchanged, `const` stays binding-only.

## Coordinator rulings (2026-11-03), recorded as binding

1. **Loops (finding 1).** B2 adds the D5 snapshot only where copy-on-write gave one implicitly: a
   loop over an expression's result and the callback natives. A loop over a binding keeps
   re-reading the binding live (legacy's model): the full array-loop snapshot, deleting the live
   path and lazy element locations, and the changed for-s2 expectations (`t26_read_after_shrink_
   first_iter`, `g6_rebind_longer`, for-mutant M1) move to **B5**, the semantic switch.
2. **Comparator sort (finding 2).** Stays in place in B2 (its drain-on-error output is kept); E's
   protection is added in commit 3. The owner's Q7 rule (sort a copy, commit once, catchable error
   on receiver mutation) is implemented in **B5**.
3. **`same()` on arrays and objects**: identity, owner-ruled (plan §3). The two fixtures change
   (§8).
4. **The compound-assignment take stays** (`g += x` on a global string appends in place); every
   array/object take is gone; the CP13 hazard note is narrowed to compound operators; a test keeps
   global string `+=` linear.
5. **Header bit 4 is the sort-protection bit** (commit 3). The ruling said "with lazy release"; the implementation releases deterministically on every exit (section 16), which the review accepted (L7).
6. **Structure**: one checkpoint, three commits, gates after each, report after commit 1.
7. **Array/object constants are immutable** (defensive), provided no reachable behaviour changes. Superseded after the review (H1, section 17): a user-visible array/object constant is a template loaded as a fresh deep copy (`LoadKFresh`) and is not marked; only internal constants (`sort_by`'s directions) are immutable.

## 1. Contract (commit 1)

Arrays and objects are shared references inside an isolate, like structs and collections since
Phase A: copying a `Value` shares the object, and a mutation changes the shared object in place,
visibly through every alias. Copy-on-write (`unshare`) and the take/put lowering are deleted. With
the guard on, no admitted program can observe the difference (its soundness claim), so every
existing expectation is unchanged except the ones listed in §8.

- **Mutation.** Every mutator writes the receiver in its holder (register, global slot, cell,
  element); nothing is moved out, so no holder is ever null and a refused mutation (ordinary error
  or caught cycle) leaves every holder intact (owner A).
- **Validation order** (plan §5.3, owner F3): kind -> immutable -> bounds/arguments -> cycle for
  each stored value -> budget, for every mutator, `set_index` included (§6).
- **Cycles.** Stage 2.4 runs for array and object receivers too: `a.push(a)`, a store of a value
  from which the receiver is reachable (through arrays, objects, collections, fields, closures'
  cells, payloads) is refused with the catchable `runtime.reference_cycle` before anything changes.
  The full walk is kept (no unique-root shortcut, plan §5.4 rule ii: not implemented). The O(1)
  exits are a scalar/string, and a value whose summary bit says it holds no reference and that is
  not the receiver itself.
- **Summary bit** (plan §5.1): bit 7 now means "may hold an identity-bearing value **or a nested
  array or object**", so a flat array of scalars keeps it clear and storing one walks nothing; the
  walk keeps the bit set while any direct child is a reference and visits an array or object child
  whenever the target is an array or object (it may be the target even with its own bit clear).
- **Immutability** (owner C/F3): `kImmutable` refuses every mutation through every alias (the B1
  refcount-1 rule is gone; no copy-on-write clone can make an immutable value mutable). `args` is
  immutable in scripts and modules too (it was only in sessions: the script runner rebuilt it with
  `NewArray`; now followed by the new `MakeImmutable`), and keeps the bit across a thread/parallel
  transfer (wire flag) and the cooperative spawn copy (`deep_copy_tuple(..., keep_immutable)`).
  Only `copy()` / `deepcopy()` make mutable copies. Internal array constants (`sort_by` directions) are immutable; a JSON-spelled object literal is a fresh deep copy at every evaluation (section 17, review H1; this line said "array/object constants are immutable" before the review).
- **Copies.** `copy(x)` of an array or object is a new outer object sharing the contents;
  `deepcopy` unchanged; `same()` is identity for arrays and objects; `merge` / `merge_deep` build a
  new object (never the receiver).
- **Snapshots** (ruling 1): a loop over an expression's result (`IterCheck c = 1`) and every
  callback native (`CollectionStep` phase 0) iterate an O(n) shallow snapshot taken at entry when
  anything else holds the array (budget reserved first; a uniquely held value needs none).
- **Sessions** stop cloning: a statement mutates the bound object itself.
- **Cooperative spawn** (D7) now copies an argument/global graph that holds only arrays/objects
  (it copied only when an identity-bearing value was present: arrays were values).

## 2. Clone-site audit (every former copy-on-write reliance)

Every place a `Value` copy acted as a defensive copy, found by deleting `unshare`, reading every
caller of the storage primitives (`array_push`, `array_set`, `array_mut`, `object_set`,
`*_slot_mut`, `struct_add_field`, `map_set`, `set_add`, `stack_push`) and every register that holds
an array across user code:

| Site | Relied on | B2 |
| --- | --- | --- |
| `natives/object_methods.cpp` `merge` (`unshare(result)`) | clone of the receiver | `shallow_object(args[0])`: a new object, the receiver untouched (R-B5) |
| `object_methods.cpp` `merge_deep` (`unshare(merged)` per level) | clone of `left` | `shallow_object(left)` per level |
| `copy_graph.cpp` `shallow_copy` | returned arrays/objects themselves (COW) | always a new outer array/object (R-B4) |
| `mutation.h` `array_reserve_bytes` (the clone term) | budget for the COW clone | removed: only growth beyond capacity is reserved |
| `mutation.cpp` `sort_storage`, `commit_permutation`, `commit_array` (`array_mut`) | `array_mut` cloned a shared receiver | in place |
| `collection_helpers.cpp` grouping (`append_group`, `object_slot_mut` + `array_push`), `s[Result]`, `s[Keys]`, `seen` | — | fresh results/scratch built by the native: unchanged (audited) |
| `collection_helpers.cpp` phase 0, `s[Source]` (map/filter/each/reduce/...) | the source register was a value | entry snapshot when shared (R-B3b) |
| `vm_iter.cpp` / `compiler_loops.cpp`, a loop over an expression result (`state.held`) | the held register was a value | entry snapshot when shared, `IterCheck c = 1` (R-B3) |
| loops over a binding (live re-read) | — | unchanged (ruling 1; B5) |
| `cell.cpp` `cell_cached_read` (`Value descriptor = object->value; array_set(descriptor, ...)`) | **COW: a "private copy" of the selector descriptor** | explicit `array_shallow_clone` (without it the cell's own descriptor was rewritten) |
| `cell.cpp` `copy_cell_location` (two cells sharing one descriptor) | the first cache update separated them | the copy clones the descriptor |
| `cell.cpp` `replace_path` (`replacement = *previous`, then nested `set`) | clone of each path level | in place, same final graph (the deepest level is validated first) |
| `vm_coro_ops.cpp` D7 spawn (`shared` only if identity-bearing) | arrays were values | an array/object seed also triggers the graph copy; the copy keeps `kImmutable` |
| `isolate_snapshot.cpp` constants summary | summary meant identity only | counts arrays/objects (plan 5.1) |
| constants (`codegen.cpp` pool), struct field initialisers | **corrected after the review (H1/H2)**: the original row said "only `sort_by`'s internal directions exist", which was wrong: a JSON-spelled object literal (`compiler_expr.cpp` `object_literal`) was one pool constant loaded by `LoadK`, shared by every evaluation (under copy-on-write a value; under references shared mutable state, and `same()` true for two evaluations) | `LoadKFresh` (a budget-reserved `deep_copy` of the template) for every array/object constant a program can see; the blanket `mark_immutable` in `FunctionBuilder::constant` is removed and kept only on `sort_by`'s directions (`compiler_collection.cpp`); field initialisers stay scalars or fresh collections (`structs.cpp`). Full re-audit: section 17 |
| `compiler_invocation.cpp` script `args` (`NewArray`) | — | `MakeImmutable` after it (owner F3) |
| `ArrayBuilder`, `isolate_task`, `parallel_task_start`, `vm_coro_parallel` seeds, `legacy_bridge`, `json`, `host_env`, `host_fs_nav`, `vm_report` `NewArray`/`ObjectSet` (literals, loop metadata), `vm_iter` key arrays, `vm_array_mutation` splice result, decoders, `session_invocation` | — | fresh values only (audited) |
| natives whose result is the receiver (`reverse`, `sort`, `push` results) | — | identity unchanged; the guard refuses observing them (`b := a.reverse()` is `reference-guard:shared`, probed) |
| `vm_ops.cpp` string `+=` (`refcount() == 1`) | the compound take | unchanged (ruling 4) |

## 3. Deleted machinery

- `unshare()` (`aggregate.{h,cpp}`) and its clone (`CloneDeleter`); `array_mut`, `array_push`,
  `array_set`, `object_mut` relocate (checked builds) and write in place.
- The array take/put: `OpCode::CellTake` (opcode, `vm_closure.cpp` handler with its pre-take cell
  cycle check, `cell_take`, coroutine admission lists), `read(take)` / `put` in
  `compiler_array_mutation.cpp`, the take paths of `compiler_assign.cpp` (element store) and
  `compiler_stmt_expr.cpp` (push), `global_take`'s cell branch and `check` operand. A read after
  the operands (as the take did) replaces each take; the write-back remains only for location
  bindings (loop elements, selectors), which write the element itself.
- The B1 transition rules: `writes_in_place` (the refcount-1 immutability condition) and the
  struct/collection-only `cycle_applies`.
- The cycle walk's `values_` bypass (B1's array/object targets walked every nested array).
- Kept (ruling 4): `global_take`/`global_put` for compound assignment, `global_taken_` and the
  exception check. Kept (ruling 1): the live loop path and lazy element locations. Kept (ruling 2):
  the comparator sort's move of the receiver through `CollectionSort` (commit 3).

New: `array_shallow_clone`, `object_shallow_clone`, `OpCode::MakeImmutable`,
`TransferGraph::Node::immutable`, `deep_copy_tuple(..., keep_immutable)`, `Vm::entry_snapshot`,
`IterCheck`'s `c` operand, `CompileLimits::reference_probe` (a non-contractual test switch: no
guard, no `aliasing` refusal; set only by `tests/rt/refrep_vm.cpp`).

## 4. Owner requirement A: session mutation atomicity (mandatory gate)

Root cause (B1 review F1): a session binding is a cell; an array mutator took the array out of the
cell (`CellTake` / `global_take`) and an ordinary error (pop on empty, insert/remove/index out of
range, invalid splice) skipped the put, leaving the binding null. B2 deletes the take: the
receiver stays in its cell, the instruction mutates it in place, and nothing has to be restored.

Evidence (both lanes run in `test-rt-vm`):

- `tests/runtime2/refrep/refrep_cli.py` (`test-runtime2-refrep-cli`, real `nift -i`, guard on):
  seven failures in a row on one binding (pop on empty x3, insert, remove, invalid splice, index
  store) with a closure capturing it; the closure then sees the binding, a later push succeeds,
  the value persists across statements; a second binding repeats the pattern. Witness: on the
  `919b5669` binary the same input prints `1` and then fails with "unknown value or malformed
  expression: a.pop()" and "unexpected operands (compiler defect): element assignment" (the null
  binding); on B2 every expected line and error is produced.
- `tests/rt/refrep_vm.cpp` `session_atomicity` (`VmSession`, guard on): repeated failures, a
  captured reference, later success, persistence, and that a statement mutates the bound object
  itself (sessions stop cloning).
- Mutant: R-B2 (copy-on-write resurrected, refsem 24) fails `refrep_vm` (alias-array).

## 5. Owner requirement C: immutability

- B1's transition rules are gone (§3): `kImmutable` refuses through every alias; arrays and
  objects get stage 2.4.
- Scripts and modules: the invocation binding is now immutable on the VM path too
  (`MakeImmutable`); before B2 only the session value carried the bit.
- Isolate crossings keep it: the transfer wire node carries `immutable` (thread(), parallel
  async, isolate snapshots, results), applied after the decoder fills the object; the cooperative
  spawn copy passes `keep_immutable`. `deepcopy()` and `copy()` stay mutable.
- Tests through real execution (`refrep_vm.cpp`, compiled with the reference probe, since the
  guard and the `aliasing` refusal stop every alias of `args` in product paths): an alias
  (`b := args`) refuses push, element store, reverse, remove, a store out of range (validation
  order), a parameter refuses pop, a closure capture refuses insert; `copy(args)` and
  `deepcopy(args)` are mutable and leave `args` unchanged; reads and loops work. Transfer through
  the real `TransferGraph` (aliasing kept, immutable kept, a mutable neighbour stays mutable) and
  the spawn copy, at the C++ level. The name-based const check still exists (until B5), so
  `args.push` itself is refused at compile time as before.
- Mutants: R-B6 (refsem 29, the wire flag dropped) fails `refrep_vm`; M-B1b/M-B1d (protocol
  immutability) fail `mutation_protocol`.
- Not reachable from scripts in B2 (guard/name checks): every alias of `args`; module and thread
  routes are covered at the C++ level only. Recorded for B5's fixtures.

## 6. Validation order (owner F3)

Every mutator now validates kind -> immutable -> bounds/arguments -> cycle (each stored value)
-> budget. The only mutator that differed was `set_index` (`SetIndex`): bounds, then immutable
(B1). **Reachability**: an element store whose receiver is immutable needs an immutable array in a
mutable place; `args[i] = v` is refused at compile time (`invocation-assign:args` / declare-const),
every alias of `args` is refused by the guard (`aliasing`, `reference-guard:*`), and an immutable
constant (only `sort_by`'s internal directions, section 17) is never stored into a place that can be
indexed. So the reorder changes no reachable behaviour:
the classify diff and the whole suite are unchanged (§9). Through the reference probe, `b := args;
b[9] = "z"` now reports "cannot mutate const array: b" (not "index out of range"), and
`b.remove(9)` likewise (`refrep_vm` args-set-order, args-pop-order).

## 7. Cycle check (plan §5.4)

- Stage 2.4 is `mutation::closes(receiver, stored, graph)`: exits for a scalar/string (one kind
  compare: the `array_push` hot path pays only that), and for a value whose summary says it holds
  no reference and is not the receiver itself; then `closes_cycle`: the receiver itself
  (`a.push(a)`), else the walk with the receiver as an aggregate target and the VM's cycle graph
  (captures through cells, resource payloads). Splice checks each replacement element (the
  replacement array itself, even the receiver, is not stored). `push`/`set` (inline), `insert`,
  `splice`, `object_put`, and the VM's `Push`, `SetIndex`, `ArrayMutate` (insert, splice c = 2 and
  c = 1) run it; the VM raises the catchable `runtime.reference_cycle` with the existing text.
- **Rule (ii), the unique-root shortcut, is not implemented** (owner: only with soundness evidence
  and adversarial mutants). The walk is linear in the reference nodes of the stored graph; flat
  data is never walked (summary bit), and the B1 `values_` bypass that walked every nested array is
  gone. R-B1 is therefore not applicable.
- The pre-take cell check (`CellTake`) is gone: a cycle through a captured array is now found by
  the protocol from the array target through the cell (`cell_cycle_*` fixtures unchanged; R21
  re-homed to the protocol as "array/object receivers skip the check" and caught by `refsem` and
  `refrep_vm`).
- Under the guard the new array/object refusals are reachable from scripts only where they were
  before (captured arrays); `refrep_vm` covers direct, nested, element store, insert, splice,
  through a map and through a cell, and an acyclic shared graph that must be accepted.

## 8. Expectation changes (every one)

| Change | Why | Authority |
| --- | --- | --- |
| `collections/approved/same_array.f`, `same_array_self.f` moved to `parity/` (VM now prints legacy's `false` / `true`; `.vm`/`.legacy` goldens deleted) | `same()` is array identity | ruling 3, plan §3 |
| `collections/parity/same_object.f` moved to `approved/` with `.vm` `true` and `.legacy` (legacy: "same: operands must be identity-bearing values") | `same()` is object identity; legacy errors, so it becomes an approved difference | ruling 3 ("same() on arrays and objects"), plan §3 |
| `tests/runtime2/cp19/session.cpp` lines 91, 176 (C++): a C++-held copy of a session array now sees the statement's mutation (was: unchanged, the session's copy-on-write clone) | sessions stop cloning | plan §6 B2 ("Sessions stop cloning on statement mutation"); not script-observable |
| `tests/rt/mutation_protocol.cpp` (C++ unit): the B1 transition-rule checks (refcount-1 immutability escape, `!cycle_applies(array)`, the clone budget) now check B2's rules | the transition rules are removed | owner F3, plan §6 B2 |
| session receiver loss (no fixture existed; new `refrep_cli.py` witness) | the old behaviour was the defect | owner A |

No script-level expectation changed otherwise (refsem 463, collections 271, every other suite with
its counts unchanged; classify diff §9). Test infrastructure changed: cp14b design mutant 5's
injection (the defect "mutation lands on a copy" re-expressed for references), refsem `mutants.sh`
R21's file, the stale-reference array control (through the VM), new lanes in `test-rt-vm`.

## 9. Owner requirement F: performance (local callgrind, not authoritative; Linode at certification)

Same toolchain, same host, `vm_compare.py --groups ops,mutation` (benchmark definitions
unchanged): marginal VM instructions per op (N = 4,000 vs 8,000). `d209febd` is the pre-B1 base
binary, `919b5669` the integrated B1 (B1-perf code, `7742a21b`), B2 = `e30bd6c9`.

| Workload | `d209febd` | `919b5669` | B2 | B2 vs `d209febd` | gate |
| --- | --- | --- | --- | --- | --- |
| `ops/array_push` | 382 | 384 | **366** | -16 (-4.2 %) | <= 382: **pass** |
| `mutation/array_set` | 388 | 413 | **380** | -8 (-2.1 %) | <= 388: **pass** |
| `mutation/array_push_pop` | 1,440 | 1,422 | 1,286 | -10.7 % | pass |
| `mutation/array_insert_pop` | 2,462 | 2,421 | 2,195 | -10.8 % | pass |
| `mutation/map_set` | 1,788 | 1,472 | 1,463 | -18.2 % | pass |
| `mutation/set_add` | 1,366 | 1,341 | 1,337 | -2.1 % | pass |
| `mutation/stack_push_pop` | 2,240 | 2,123 | 2,121 | -5.3 % | pass |
| `ops/loop`, `add_local`, `if_branch` | 117, 188, 249 | same | 117, 188, 248 | 0 | pass |
| `ops/call`, `array_index`, `object_member`, `string_concat` | 567, 434, 761, 3,309 | same | 565, 436, 763, 3,311 | +-2 Ir (code layout) | pass |

B2 additions (`scratchpad/b2/ops.py`, same marginal method; not new benchmark definitions):

| Workload (plan §7 family) | `d209febd` | `919b5669` | B2 | vs `d209febd` |
| --- | --- | --- | --- | --- |
| `for (x : a)` over a binding (iteration; live path, no snapshot) | 629.3 | 631.3 | 613.3 | -2.5 % |
| `for (x : range(0, N))` (iteration; held result, snapshot check) | 203.4 | 203.4 | 203.4 | 0 (budget +10 %) |
| `a.map(..).filter(..)` (callback natives; entry snapshot of a shared receiver) | 2,329.4 | 2,331.4 | 2,269.5 | -2.6 % (budget +10 %) |
| `g.push([i, i])` (nested store: cycle check on an array receiver) | 1,977.5 | 1,980.5 | 1,904.5 | -3.7 % |
| `g[0] = [i, i]` (nested element store) | 1,435.0 | 1,465.0 | 1,374.0 | -4.2 % |

Where the instructions went: deleting `unshare` removed its call and refcount test from every
array/object mutation (B1-perf measured 29 Ir per push); the cycle stage costs one kind compare for
a scalar and one summary-bit test for a flat array. `array_set` first measured 395 with B2's
`set_index` re-running the kind, immutability and bounds checks before `mutation::set`; `set_index`
now lets `mutation::set` run the stages once and maps only its refusals to legacy's texts (380).
The held-loop snapshot first cost +15 % on `for (x : range(...))` because a call's window
register kept a second reference to the fresh result; the loop now clears that dead temporary
(one `LoadNull` at loop entry), so a fresh result is held once and is never copied (203.4).

- `bench-rt-value` (callgrind, guard): copy 4,356,363 (-6.4 %), push 9,933,829 (-24.9 %), object
  set 9,665,170 (-29.8 %), object get 5,123,174 (+0.04 %): **PASS** (locked baseline).
- `bench-rt-value-cycle` (wall, shared host): store_plain +0.51 ns, store_callable +11.37 ns,
  store_handle +7.31 ns, store_nested_identity +32.94 ns: **PASS** (all within budget).
- Website build (`run.py --groups site`, `profile_300_instr_per_page`, callgrind, deterministic):
  `d209febd` 886,887, `919b5669` 886,881, B2 886,908 Ir per page (+0.002 %): **no regression**
  (templates never reach the VM; the 2k wall-clock rows are single runs on a shared host and are
  noise, recorded in `perf/site.jsonl`).
- 200,000-element stress: the collection/array stress fixtures and the sanitizer lanes ran
  unchanged in `test-rt-vm` / `-sanitize` (§10); no workload became superlinear.

## 10. Mutants and gates (commit 1, code commit `e30bd6c9`)

New mutants (`make test-rt-refrep-mutants`, each rebuilds one object, must fail `refrep_vm`):

| Mutant | Change | Caught by |
| --- | --- | --- |
| R-B2 (refsem 24, `aggregate.cpp`) | copy-on-write resurrected: a shared array is cloned on write | alias-array (and every sharing case) |
| R-B3 (refsem 27, `vm_iter.cpp`) | a loop over an expression result iterates live | loop-held |
| R-B3b (refsem 28, `collection_helpers.cpp`) | a callback native iterates its live receiver | map-snapshot |
| R-B4 (refsem 25, `copy_graph.cpp`) | `copy()` returns the same object | copy-new-outer |
| R-B5 (refsem 26, `natives/object_methods.cpp`) | `merge` mutates its receiver | merge-new |
| R-B6 (refsem 29, `isolate_transfer_encode.cpp`) | `kImmutable` lost on an isolate transfer | transfer_immutability |
| R21 (re-homed to `mutation.cpp`) | array/object receivers skip the cycle check | push-self (refrep), cell_cycle_* (refsem, 12 checks) |

R-B1 (shortcut misuse) is not applicable: no shortcut (§7). R19 kept: every VM-path stale-reference
control (array now through a VM statement, map, set, stack; plain and churn) aborts with
heap-use-after-free, and the R19 build misses every one. A's mutant is R-B2 (the session lane);
C's are R-B6 and M-B1b/d; D (commit 2), E (commit 3) and B's bypass mutant (commit 2) follow.

Gates, each alone through the heavy-job gate, on `e30bd6c9` (exit codes):

| Gate | Result |
| --- | --- |
| `make nift`; `test-rt-lint` (8 self-test fixtures; `src/rt` 0 problems) | 0, 0 |
| `test-rt-mutation-protocol` (89 checks, 63 refusal cases); `-mutants` (M-B1a-e, 5 of 5) | 0 |
| `test-rt-refrep` (54 checks); `-refrep-mutants` (7 of 7); `test-runtime2-refrep-cli` (6) | 0, 0, 0 |
| `test-runtime2-refsem` (463 checks); `-refsem-mutants` (22 caught) | 0, 0 |
| `test-runtime2-collections` (271); `-collections-mutants` (11 caught) | 0, 0 |
| `test-runtime2-safety` (B0 fixed-seed fuzz: 33,512 programs, seed 1); `-safety-mutants` (7 caught) | 0, 0 |
| `test-runtime2-vm-default`, `-cp14b`, `-cp14b-design-mutants` (1-11 detected, 12 unverified comparator profile as before), `-args-cmd`, `-args-cmd-mutants` | 0 each |
| `test-runtime2-packages`, `-packages-mutants`, `-packages-legacy`; `-coro-module` | 0 each |
| `test-cp19-functional`, `test-cp19-session-cli`, `test-runtime2-cp22`, `test-runtime2-cp23` | 0 each |
| `test-runtime2-refsem-stale-vm` (8 controls, R19 misses all), `-refsem-sanitize`, `-collections-sanitize` | 0, 0, 0 |
| `make test-rt-vm` (every prerequisite and recipe line, 2,284 s) | 0 |

**Classify diff** against `919b5669` (the B1-perf binary, code-identical), both over this tree's
tests: `NIFT_VM=classify` over every `.f` in `tests/runtime2` (1,816 units): **0 changes**; CP25-R
`classify-all` over the release corpus (589 units; outcome, reason, line, column, reasons, code):
**0 changes**.

Not run locally: TSan lanes, the Linode workloads.

## 11. Size (commit 1)

Production (`src/`): +437 / -295 over 44 files (much of it comments and moved checks); tests and
Makefile +463 / -71 (the new `refrep_vm.cpp` 250 lines, `refrep_cli.py` 100). Within the plan's
estimate for the whole of B2 (about 700 changed, 600 deleted); commits 2 and 3 follow.

## 12. Notes for B5 (carried, from this commit)

- The live array/object loop path, lazy element locations and the for-s2 expectations they carry
  (`t26_read_after_shrink_first_iter`, `g6_rebind_longer`, mutant M1) are B5's (ruling 1).
- The comparator sort's snapshot-and-single-commit (Q7) is B5's (ruling 2). Its tests must cover
  aliased reads during the sort, not only mutations (review L1, mandatory; section 17.5).
- Review L2 (`cell.cpp` `replace_path`: residual take, default Env, partial commit) is B5/B6's and
  L3 (the static `c = 100` validation order) is B5's (section 17.5).
- Reachable only through the reference probe today: every alias of `args`, callback mutation of
  the receiver, cycles through shared arrays/objects; their B5 fixtures can start from
  `refrep_vm.cpp`.
- `merge_deep` shares non-object values of its right operand (by reference, like `copy`'s
  contents); with the guard on this is unobservable.

## 13. Commit 1b: test migration (component: test migration)

Commit 1's gate set missed three suites that exercise the deleted copy-on-write machinery directly:
`test-rt-value`, `test-runtime2-cp18` and `test-cp19-r1` (ownership review). They failed on
`a85e1f4a` (the commit 1 tip). This commit migrates them under the coordinator's hybrid rules:
checks of deleted machinery are retired, the invariants they guarded are re-expressed for the
reference model, and the generators are ported to reference semantics through `mutation::Protocol`
with cycle refusal as a property. Seeds and iteration counts are unchanged. These three suites are
in the gate set of every later commit.

### 13.1 Retirement table

Every retired test last existed at `a85e1f4a`. Nothing is squashed: commit 1 and this commit are
separate.

| Retired (file: test) | What it checked | Replacement (this commit) |
| --- | --- | --- |
| `tests/rt/value_unit.cpp`: `arrays_cow` | `unshare` clones a shared array; writes stay independent | `arrays_shared`: a Value copy shares the object, a mutation is seen through both holders, refcounts count every holder, an explicit shallow clone is a new outer array sharing its elements; object alias check |
| `tests/rt/value_d040.cpp`: `nested_cow` (section 2) | random copies and deep place writes against a deep-copy model | `nested_references`: a node-graph reference model (ops 0-7: alias, shallow copy, deep place write, push, self-store, nesting), every write through the protocol; refused cycles leave model and value unchanged; value and identity checked per step; acyclic (V16) kept |
| `value_d040.cpp`: section 2b, self-insertion stores the pre-write snapshot | `a.push(a)` gives `[1,[1]]` | self-insertion (direct, repeated, two levels deep, through an alias) is a refused cycle, receiver unchanged; struct cases unchanged |
| `value_d040.cpp`: section 3, "storage sharing is never exposed" | `same()` false for COW copies | `same()` is identity for arrays and objects (alias true, distinct-equal false) |
| `value_d040.cpp`: section 7, COW metadata | summary bit copied across COW clones | the bit across aliases (one shared bit) and explicit clones; set by a nested array/object (plan 5.1); a flat scalar array stays clear |
| `tests/rt/value_property.cpp`: `cow_model` (+ `legacy_remove`) | random aliasing and mutation behave like deep copies | `reference_model` (arrays) and the object model ported: a node graph, every mutation through the protocol, nesting refused when the target reaches the receiver, value (legacy oracle) and identity (`same_identity`, `check_slots`) per step; `coverage.refused_cycles` counted |
| `tests/rt/value_regress.cpp`: COW programs "stores the pre-push snapshot" | self-store snapshot | self-store is a protocol refusal before anything changes; child-into-parent stays memory safe |
| `value_regress.cpp`: "a saturated object is shared: mutation clones" | clone at saturated refcount | mutation writes in place and the count stays saturated |
| `tests/rt/value_destroy_oom.cpp`: COW-clone DAG children | DAG built from COW clones | the same DAG built with explicit `shallow_copy` for every kind; destruction properties unchanged |
| `tests/runtime2/cp18/cow_allocation.cpp`: direct `unshare` campaign | allocation failure inside `unshare` | direct campaigns on `array_shallow_clone`, `object_shallow_clone`, `shallow_copy` (struct) |
| `cow_allocation.cpp`: ABI program `fn(g(a)) { a.push(1) ... } g(b)` | COW clone of a shared argument | refused by the guard since before B2; replaced by `fn(g(n)) { a := [n]; a.push(1); ... }` (same output) |
| `tests/runtime2/cp18/cow_negative.py` + `unsafe_unshare.txt` | ordinary-build control: unsafe `unshare` must fail allocation accounting | retargeted to `array_shallow_clone` with `unsafe_shallow_clone.txt` (new array unowned while items copy); detected in both modes |
| `tests/runtime2/cp18/sanitize.py` | sanitizer lane over the `unshare` control | same lane over the shallow-clone control |
| `tests/runtime2/cp18/cow_semantics.py`: `array-admitted` | pinned the COW result `2\n1\n` | `array-argument` (the guard refusal, `reference-guard:argument`) and `array-local` (an admitted mutation with the same output). `array-admitted` already failed at `919b5669` (the guard predates B2) |
| `tests/rt/value_round2.cpp` `heap_accounting`: "copy-on-write clone is counted" (last at `ec7c885a`; found late, see 13.6b) | a write through a copy is tallied as the clone's storage | growth through an alias (the same array, 1,000 protocol pushes) is tallied |
| `tests/rt/value_d040.cpp` `tag_masking` struct case: `struct_add_field` on a shared instance (last at `ec7c885a`) | a write to a shared struct lands in place | the receiver form `struct_set` of the existing field (construction stays on fresh values) |
| `tests/runtime2/cp19/ownership_review.cpp`: COW ownership checks | a holder unchanged by another's mutation | an evaluated result is an owned value; a mutation through the protocol is seen through every holder, each holder keeps the object alive, no holder is null during a mutation |

### 13.2 New allocation-failure coverage (owner addition; ASan/LSan)

`cow_allocation.cpp` gains, each failing every allocation site in turn and checking recovery and
no leak: growth campaigns 0-6 (push, insert, splice, map put, set add, stack push, self-splice);
reference-model campaigns (creation, copy, deep copy, nested growth) with an intact check after
every failure; a persistent `VmSession` campaign (465 sites, 464 recoveries; the internal stop
closes the session by the CP19 design, which the test accepts and records; at least 100 sites
required). Destruction under low memory stays in `value_destroy_oom.cpp`.

The campaign found a pre-existing leak: `make_struct` held a raw pointer across
`fields.reserve`, so an allocation failure in the reserve leaked the instance. Fixed in
`src/rt/aggregate.cpp` (owned `Value` before the reserve).

### 13.3 Self-splice fix and aliasing audit (component: self-splice and aliasing)

`a.splice(i, n, a)` read the receiver's own vector while inserting into it. Fixed in two places:
`mutation.h` (`commit_array`, SpliceInsert copies the items first when the replacement is the
receiver) and `vm_array_mutation.cpp` (the shared-receiver branch clones the replacement when it
is the source). Result: the elements before the removal are inserted, as the copy-on-write
register gave. Legacy inserts the elements after the removal (`[1,1,3,3]` for `a.splice(1,1,a)`);
that difference is a B5 decision. Coverage, both configurations where reachable:
`mutation_protocol.cpp` (begin, middle, end, full range, empty receiver; a replacement holding the
receiver is a refused cycle), `refrep_vm.cpp` (probe: splice-self-middle/front/end/full/empty/
alias/nested), `refrep_cli.py` (production: the program is refused by the guard),
`cow_allocation.cpp` growth campaign 6 (allocation failure while preparing the copy).

Every mutator with an aliasing operand:

| Mutator | Operand aliasing the receiver | Status |
| --- | --- | --- |
| push, insert, set, object put, map put, set add, stack push | the receiver itself | refused as a cycle before anything changes |
| same, with an element of the receiver's own storage | operand copied before the storage can move | safe; tests in `mutation_protocol.cpp` (ASan, 119 checks) |
| splice | the receiver as replacement | fixed (above) |
| merge | the receiver as either side | safe: builds a new object first |
| reverse, clear, pop, remove | none | safe: no operand |
| sort | comparator mutating the receiver | commit 3 |

### 13.4 Pre-existing failures (not caused by B2)

- `test-rt-builtins`: `builtins_diff` static assert "Host built-ins must stay last" and link
  errors. Fails on `919b5669` and on `d209febd`: `tests/rt/builtins_diff.cpp:487` static
  assertion "the Host built-ins must stay last in builtin_list.inc", a compile-time failure,
  identical in two runs on each revision (deterministic; trees from `git archive`, syntax check).
  Out of B2 scope; a release-blocker audit item.
- `test-runtime2-cp18`: `combined_cp14b.py` (`b := a` refused, `reference-guard:inflow`, the
  same compiler at `919b5669`); `cow_semantics.py` struct-copy case admitted on both `919b5669`
  and this tree (left failing, recorded).
- `test-cp19-r1`: `eval_entry.py` unresolved-name check fails with the `919b5669` binary. Release-audit item (coordinator, from the Linode run): the assertion expects
  `NIFT_VM=require` in the refusal text, which now says `NIFT_VM=0 nift eval`: a stale test after
  the VM-default change, not a product defect; not fixed in B2.

### 13.5 R19 (documented negative control)

R19 (`-DNIFT_REFSEM_MUTANT=19`) removes the relocation after a hand-out in the checked
`aggregate.cpp`/`aggregate_collection.cpp` build, so a stale reference stays readable. It is kept
as a negative control, not a mutant to kill: `test-runtime2-refsem-stale-vm` builds a
5,000-element container through a VM session (plain, and after a 20,000-element read-after-push
churn) for array, map, set and stack, takes a reference, mutates once through the VM and uses it.
The normal checked build must abort with heap-use-after-free, and the R19 build must miss (exit
0); if R19 also aborted, the gate fails ("the control does not discriminate"). Evidence: all 8
controls report "detected, missed by R19" on `e30bd6c9` and on this commit.

Gate discrimination, shown by running the gate's loop verbatim with its two binaries swapped
(`scratchpad/b2/r19demo.sh`, on this commit):

| Normal slot / R19 slot | Gate |
| --- | --- |
| `stale-vm` / `stale-vm-r19` (the real gate) | PASS (8 of 8) |
| `stale-vm-r19` / `stale-vm-r19` (a must-catch control missed) | FAIL: "VM-path stale reference (array plain) not detected" |
| `stale-vm` / `stale-vm` (R19 caught) | FAIL: "R19 did not miss array plain (the control does not discriminate)" |

### 13.6 Gates (commit 1b; rerun from scratch after two container restarts)

Each alone through the heavy-job gate, on this commit's tree: `nift`, `test-rt-lint`,
`test-rt-mutation-protocol` (119 checks; ASan/UBSan with checked references: 0 failures, no
leaks), `test-rt-refrep` (61 checks), `test-rt-value`, `test-rt-mutation-protocol-mutants`,
`test-rt-refrep-mutants`, `test-runtime2-refrep-cli`, `-refsem`, `-refsem-mutants`, `-collections`,
`-collections-mutants`, `-safety`, `-vm-default`, `-cp14b`, `-args-cmd`, `-packages`,
`-coro-module`, `test-cp19-functional`, `test-cp19-session-cli`, `test-runtime2-cp22`, `-cp23`,
`-refsem-stale-vm`: 0 each.

`test-runtime2-cp18` and `test-cp19-r1` stop at their first pre-existing failure (13.4), so every
recipe line was run separately (`scratchpad/b2/missed.sh`): CP18 C 1044 checks (both lanes),
Python 70 (both), COW allocation "0 failures" (both, every campaign), `cow_negative` detected
(both modes), CP19 expression unit 1442, eval bypass mutant detected, ownership review 1562 checks
(both lanes), R1 183 observations (both lanes), return-prefix mutant detected, reviewer matrix 135
(both lanes): all 0. Failing, all pre-existing (13.4): `combined_cp14b.py` (both lanes),
`cow_semantics.py` struct-copy (both), `eval_entry.py` (threaded and switch).

Classify diff against `919b5669`: 1,816 runtime2 units, 0 changes; CP25-R 589 units, 0 changes.

Performance (local callgrind, VM Ir/op; d209febd / 919b5669 / 1b): array_push 382 / 384 / 366;
array_set 388 / 413 / 380; array_push_pop 1440 / 1422 / 1304; array_insert_pop 2462 / 2421 /
2209; map_set 1788 / 1472 / 1463; set_add 1366 / 1341 / 1337; stack_push_pop 2240 / 2123 / 2121.
Within F (array_push at most 382, array_set at most 388).

Pending at this commit (run next and recorded in a following commit): the sanitizer lanes
`test-rt-value-sanitize`, `test-runtime2-cp18-sanitize`, `test-cp19-r1-sanitize`,
`test-runtime2-refsem-sanitize`, `test-runtime2-collections-sanitize`, and `make test-rt-vm`.

### 13.6a Sanitizer lanes, `test-rt-vm`, and a nondeterministic mutant (follow-up commit)

Sanitizer lanes on `e65d4b64`, each alone, sanitizer build directories cleared between lanes:
`test-rt-value-sanitize` 0 (3,858 s), `test-cp19-r1-sanitize` 0, `test-runtime2-refsem-sanitize`
0, `test-runtime2-collections-sanitize` 0. `test-runtime2-cp18-sanitize` fails to link, before
B2 too (13.4): `tests/runtime2/cp18/sanitize.py`'s source list lacks `src/PackageTransaction.cpp`
and `src/JsonFile.cpp`, which `src/rt/package_resolve.cpp` has needed since `bb5dc585` (an
ancestor of `919b5669` and `d209febd`, whose `sanitize.py` has the same list).

`test-rt-vm` on `e65d4b64` failed: `test-rt-mutation-protocol-mutants` reported "M-B1 mutant 1
not caught (exit 139)". M-B1a (commit before validation) is caught by its failed checks, but this
commit's new aliasing case (an operand that is an element of the receiver itself) made the
mutant's own validation read the operand after its early commit had moved the storage: undefined
behaviour in the mutant build only, a segmentation fault in about 1 run of 6 (address-layout
dependent; valgrind: an invalid read in `cycle_targets_and_bookkeeping`). Production validates
before the commit and `array_push` copies its by-value operand first (ASan run of the same test:
clean). Fix (`mutation.h`, mutant block only): M-B1a validates a copy of the operand, keeping its
defect (the commit before the validation) without undefined behaviour of its own; the production
path binds the same name to the operand by reference (no code change). After the fix: 200 runs of
`protocol-m1`, every one exit 1; valgrind: no invalid access; mutants 1-5 caught.

Test-infrastructure fix (its own commit, coordinator ruling): `sanitize.py`'s source list gains
`src/PackageTransaction.cpp` and `src/JsonFile.cpp`; a pre-existing defect, on the release-audit
list. The identical list, run as a scratch copy of the harness on `f01622cb`, passed
("PASS CP18 ASan/UBSan: C consumer and allocation controls, both dispatches", both negative
controls detected, 287 s); the lane itself on the fixed tree runs on the Linode (local sanitizer
lanes stopped by the owner's ruling).

### 13.6b Two ports found by the ASan lane on commit 2 (follow-up commit)

`test-rt-value-sanitize` on `ec7c885a` aborted in `value-round2` and `value-d040`: commit 2's
construction check (`check_fresh`, checked and sanitizer builds only) found two test calls that
used the construction API on a shared value: `value_round2`'s "copy-on-write clone is counted"
(never ported in 1b: without the check its byte bound still held through the array's capacity
growth, so the ordinary lane passed) and `value_d040`'s struct `struct_add_field` on an alias.
Both are ported (13.1). Verified with a checked build (`-DNIFT_RT_CHECKED_REFS` on the aggregate
and protocol sources, the rest ordinary objects): value unit, regress, round 2 (1,030), D-040
(191,750), property (2,132,609), destroy-OOM, `mutation_protocol` (209) and the CP19 ownership
review (1,562): 0 failures. The remaining `test-rt-value-sanitize` steps on `ec7c885a` (the 12
stale-reference controls, property, destroy-OOM both) passed when run from the already built
binaries. The full lane is part of the Linode run.

### 13.7 Size (commit 1b)

Production: +15 / -3 (`mutation.h`, `vm_array_mutation.cpp`, `aggregate.cpp`). Tests: about
780 inserted lines, under the 800-line split threshold; no unresolved ownership problem.

### 13.8 Which configuration each ported property runs under (owner refinement: shared identity is not shared mutable state)

Three configurations, kept apart:

- **Representation layer (C++, no compiler, so no guard).** Asserts B2's representation contract
  (commit 1, plan §3/§5): a `Value` copy shares the heap object, a protocol mutation is seen
  through every C++ holder, refcounts count every holder, explicit clones are new outer objects,
  cycles are refused. This is what B2 changes in the runtime; it says nothing about which script
  programs may observe it.
- **Production (guard on, the normal compiler).** Asserts only established behaviour plus the
  approved changes (§8). No script-level assertion has a mutation become visible through an
  alias: the guard refuses every such program (`reference-guard:*`, `aliasing`), and the tests
  assert those refusals.
- **Probe (guard off, `NIFT_RT_REFERENCE_PROBE`, `refrep-vm` only).** Every script-level
  alias-visibility assertion (B5 semantics, exercised early) lives here.

| Test (property) | Configuration |
| --- | --- |
| `value_unit` `arrays_shared`, objects alias check | representation |
| `value_d040` `nested_references`, self-insertion refusals, `same()` identity, summary bit | representation |
| `value_property` `reference_model`, object model | representation |
| `value_regress` self-store refusals, saturation in place | representation |
| `value_destroy_oom` shared-children DAG | representation |
| `value_round2` growth through an alias is tallied (13.6b) | representation |
| `mutation_protocol` (aliasing operands, self-splice, allocation failures) | representation |
| `cp18/cow_allocation` direct, growth and reference-model campaigns | representation |
| `cp18/cow_allocation` ABI program, session campaign | production (no alias observed: `a := [n]` local; the session campaign checks recovery, not aliasing) |
| `cp18/cow_semantics.py` `array-argument` (refused), `array-local` | production |
| `cp19/ownership_review.cpp` (an evaluated array mutated through the protocol is seen by every C++ holder) | representation, on values produced by the production compiler: C++ holders, not script aliases |
| `cp19/session.cpp` lines 91, 176 (commit 1): a C++ host's `VmSession::value` copy sees the next statement's mutation | **production, host-visible**: the one production-configuration alias visibility, through the embedding API (not scripts). It is the plan's approved B2 item "sessions stop cloning on statement mutation" (§8); flagged here for the reviewer |
| `refrep_cli.py` (session atomicity, self-splice refused) | production |
| `refrep_vm.cpp` every function, `session_atomicity` included | probe (its `VmSession` compiles with the probe objects: the earlier comment "guard on" there was wrong and is corrected in the identity-test commit) |

No 1b port assumes B5 is live in production.

## 14. Commit 2: one mutation path (B), throw safety (D), probe isolation

### 14.1 Single mutation path (owner B), enforced structurally

- The storage commits on an existing aggregate (`array_mut`, `array_push`/`array_set` on a
  receiver, the slot functions, `object_set`/`object_remove`, `struct_set`, `map_set`, `set_add`,
  `collection_erase`, `stack_push`/`stack_pop`, `collection_clear`) are private static members of
  `rt::Storage` (`aggregate.h`). Its only production friend is `mutation::Protocol`, whose entry
  points run the stages (kind, immutable, bounds/arguments, cycle, budget) before calling one; so
  a receiver's storage can be written only after the protocol's validation. The second friend is
  `StorageTestAccess` (`tests/rt/storage_access.h`), for the storage-level unit tests only.
- Construction stays public and is separate: `array_push`, `object_set`, `array_build_storage`,
  `array_build_set`, `object_build_slot`, `map_build_set`, `set_build_add`, `stack_build_push` on
  a FRESH value (refcount 1, not immutable; checked builds abort otherwise, `check_fresh`).
- The VM's array instructions (`ArrayMutate`: insert, remove, pop, clear, reverse, splice c = 0/1/2)
  call the protocol's entry points (`vm_array_mutation.cpp`), no longer their own commits. The
  comparator sort reaches the receiver's storage through `Protocol::sort_items`/`sort_storage`.
- Compile-fail tests (`make test-rt-protocol-compile-fail`, `tests/rt/compile_fail/run.sh`):
  `Storage::array_set` from outside ("is private within this context"), the B1 free `map_set`
  ("not a member"/"not declared"), `Protocol::commit_array` (stage 3 without stage 2, private);
  the control (a protocol entry point) compiles.
- Host sensitivity found on the Linode (`test-rt-protocol-compile-fail` exit 2 on `ec7c885a`,
  passing locally): the check matched g++'s diagnostic text, and g++ quotes names with ASCII `'`
  in the C/POSIX locale (this host: no `LANG`) but with typographic quotes in a UTF-8 locale (the
  Linode), so `bypass_removed.cpp`'s "'map_set' is not a member" never matched there. Reproduced
  locally with `LC_ALL=C.UTF-8`. Nothing in the worktree supplied a declaration. Fix: (1)
  `compile_fail/absent.cpp`, which must COMPILE, proves the property structurally, independent of
  any diagnostic: substitution-failure detectors `static_assert` that no removed free commit
  (`map_set`, `set_add`, `stack_push`/`stack_pop`, `collection_erase`/`collection_clear`,
  `array_set`, `array_mut`, the slot functions, `object_remove`, `struct_set`) is found for a
  `Value` receiver, that the `Storage` commits and `Protocol::commit_array` are not accessible
  outside the protocol, with controls that the detectors fire (the construction API and a
  protocol entry point are detected); (2) the real-call cases run under `LC_ALL=C` and require
  only a failed compile whose error names the bypassed function (not its wording);
  `bypass_commit.cpp` passes the edit as `{}`, so its rejection is the commit's own access, not
  the private `ArrayEdit`'s. Discrimination: `absent.cpp` fails with `storage_access.h`
  force-included (free commits back: both free-commit asserts fire), with `Storage`'s members
  public (the access assert fires), and with `commit_array` public (the commit assert fires;
  the first detector version missed this, because it named the private enum). Result: PASS in the
  C and C.UTF-8 locales, and from a clean clone (14.4).
- `rt-lint` (defence in depth): the storage-commit names outside the protocol, and any
  `Storage::` or `StorageTestAccess` use under `src/`, are flagged (fixture `bad_storage.cpp`).
- Mutant M-B2 (refsem 30, `vm_ops.cpp`): an element store through the construction API on the
  receiver (no immutability, cycle or bookkeeping stage); caught by `refrep_vm`.

### 14.2 Throw safety (owner D)

`map_set`, `set_add` and `stack_push` give the strong guarantee: every step that can throw (the
checked-build relocation, the storage reservation, a rebuilt key index) runs before the first
observable change, or is undone (`aggregate_collection.cpp` `append_entry`). The storage is
reserved before the append and grown as `push_back` grows it (`size + max(size, n)`, so the
capacities, heap accounting and amortised cost are unchanged); a new index is built on the side
and swapped in; a failed index build pops the appended entry. Hashing and comparing keys cannot
throw. Deterministic test (`mutation_protocol.cpp` `allocation_failures`): a global `operator
new` that fails the k-th allocation of one insertion, k = 0, 1, ... until it completes, for
`map_put`, `set_insert`, `stack_push` at prefills where the insertion must allocate (storage full,
first index, index growth): after every injected failure the collection is unchanged (items,
identity, every key found through the index) and never holds a key without its value. Mutant 6
(`aggregate_collection.cpp`: the key appended before the reservation) is caught.

Found by the gates while writing it: the first version reserved exactly `size + n`, which turned
every insertion into a reallocation (quadratic, and a budget estimate of a whole new buffer per
insertion): four refsem budget fixtures (`heap/budget_{copy,deepcopy,snapshot,spawn}.f`) stopped
before their `filled` line. Fixed by the `push_back` growth rule above.

Also found by the gates: the one-call `mutation::splice` (natives, C++ callers) had been rewritten
as the removal commit followed by the insertion commit, so an allocation failure in the
insertion left the removal done (CP18 `cow_allocation` growth/splice: "size changed" after a
failure, 1 site instead of 3), and its budget reservation shrank. Restored commit 1's form: the
result is built on the side (every allocation, the removed array included, before any change),
budget reserved for the side copy as before, then swapped into the receiver's own storage
(identity kept; a self-splice only reads the receiver). After the fix: splice 3 sites, splice-self
3, "COW final: 0 failures". The VM's two-instruction splice (c = 2 removal, c = 1 insertion, plan
F2) is unchanged from commit 1.

### 14.3 Probe isolation (owner ruling 2)

The reference probe is a compile-time test configuration only: `-DNIFT_RT_REFERENCE_PROBE` on
`compiler.cpp`, `compiler_rules.cpp`, `compiler_collection_sort.cpp` (no guard, no `aliasing`
refusal, no comparator-sort profile refusal), built into `.build/rt/refrep/probe/` and linked only
into `refrep-vm`. `CompileLimits::reference_probe` (commit 1's runtime field) is deleted: no
runtime flag, environment variable, argument, `CompileLimits` field or embedded API reaches it.
`make test-rt-refrep-production` (`tests/rt/refrep_production.sh`) checks: only the two refrep
rules define the macro (no production flags); neither `nift` nor `libnift_c.so` contains the
probe build's marker symbol (`nift_rt_reference_probe_build`) or text, while the probe binary does
(control); and behaviourally, the production CLI keeps the comparator-sort refusal
(`method:sort`, before anything runs, on this toolchain's libstdc++ 20240904, outside the admitted
profile) and the Phase A guard (`reference-guard:argument`).

### 14.4 Infrastructure fixes in this commit

- `RT_REFREP_OBJECTS` is spelled out from `RT_VALUE_SOURCES` (`CP19_EXPR_OBJECTS` is defined
  further down the Makefile, so a `:=` copy of it was empty and `refrep-vm` failed to link).
- `cow_allocation.cpp` builds its fixtures with the construction API (`map_build_set`, ...).
- `mutation.cpp` grew past rt-lint's 400-line one-concern limit with the restored splice (414):
  the collection entry points (`map_put`, `set_insert`, `collection_remove`, `stack_push`,
  `stack_pop`, `collection_reserve_bytes`) moved to `mutation_collection.cpp`, with the refusal
  builders and stage helpers they share in `mutation_collection.h` (`mutation::internal`); the
  same stages in the same order. The protocol mutant builds compile it with the mutant define
  (it inlines the `mutation.h` stages mutants 4 and 5 change), and rt-lint's storage-write rule
  allows it as part of the protocol.

### 14.5 Gates and performance (commit 2: `497e145f`, `4c08861a`, `ec7c885a`)

Milestone commits preserve tested work; none certifies the checkpoint. Gate status per commit:

- `497e145f` (B, D, probe): every gate below passed except `test-rt-lint` (`mutation.cpp` 414
  lines > 400); fixed by `4c08861a` (the split).
- `4c08861a`, each alone through the heavy-job gate: `nift`, `test-rt-lint` (0 problems, 8
  self-test fixtures), `test-rt-protocol-compile-fail` (3 rejected, control compiles),
  `test-rt-mutation-protocol` (191 checks), `-mutants` (M-B1a-e and 6: 6 of 6),
  `test-rt-refrep` (62 checks; `test-rt-refrep-production` PASS), `-refrep-mutants` (R-B2-6,
  R21, M-B2: 8 of 8), `test-runtime2-refrep-cli`, `-refsem`, `-refsem-mutants`, `-collections`,
  `-collections-mutants`, `-safety`, `-vm-default`, `-cp14b`, `-args-cmd`, `-packages`,
  `-coro-module`, `test-cp19-functional`, `test-cp19-session-cli`, `test-runtime2-cp22`, `-cp23`,
  `-refsem-stale-vm`, `test-rt-value`: 0 each. Missed-suite lines (`missed.sh`): all 0 except
  the pre-existing three (13.4). Classify diff: 1,816 and 589 units, 0 changes. Perf: array_set
  393 (above F's 388): fixed by `ec7c885a`.
- `ec7c885a` (tip of commit 2), with restart-safe result files (`scratchpad/b2/results-c2tip/`:
  tree identity, command, exit status, log checksum per gate): `nift`, `test-rt-lint`,
  `test-rt-protocol-compile-fail`, `test-rt-refrep`, `test-cp19-functional`,
  `test-cp19-session-cli`, `test-runtime2-cp22`, `-cp23`, `make test-rt-vm` (2,541 s): 0 each.
  Missed-suite lines: pre-existing three only. Classify diff: 0 changes. `test-rt-value-sanitize`: exit 2, two unported test calls (13.6b; fixed in `12f486bc`). The other sanitizer and TSan lanes, `test-rt-vm-sanitize` and the full `make test` run on the Linode (owner ruling: heavy validation off this host).

Performance on `ec7c885a` (local callgrind, VM Ir/op; `d209febd` / `919b5669` / 1b `f01622cb` /
commit 2):

| Workload | `d209febd` | `919b5669` | 1b | commit 2 | F |
| --- | --- | --- | --- | --- | --- |
| `ops/array_push` | 382 | 384 | 366 | 368 | <= 382: pass |
| `mutation/array_set` | 388 | 413 | 380 | 380 | <= 388: pass |
| `mutation/array_push_pop` | 1,440 | 1,422 | 1,304 | 1,431 | <= base |
| `mutation/array_insert_pop` | 2,462 | 2,421 | 2,209 | 2,446 | <= base |
| `mutation/map_set` | 1,788 | 1,472 | 1,463 | 1,497 | <= base |
| `mutation/set_add` | 1,366 | 1,341 | 1,337 | 1,364 | <= base (2 Ir margin) |
| `mutation/stack_push_pop` | 2,240 | 2,123 | 2,121 | 2,192 | <= base |

Where commit 2's instructions went: pop, insert and the collection mutators now run the
protocol's out-of-line entry points (owner B: one implementation) instead of the VM's own inline
commits, and `append_entry`'s reservation adds a capacity test (owner D). Every workload stays at
or below `d209febd`; the margins against 1b are recorded for B5's performance work.
`bench-rt-value`: PASS (copy -6.41 %, push -24.93 %, object set -24.18 %, get +0.03 %).

## 15. Identity and probe coverage (owner rulings 5 and 6)

`tests/rt/refrep_vm.cpp` `identity()`, probe configuration only (13.8: production keeps refusing
every program that could observe an alias, `refrep_cli.py` and the refsem suite):

- arrays and objects: an alias is `same`, a distinct equal value is not, `copy` and `deepcopy`
  are distinct; a mutation through an alias keeps identity and is seen through both (arrays
  directly; objects through a member array, because the VM stores elements of arrays only:
  plain objects have no script-level store, `type:element-assignment`/`static-error:member-assign`,
  and in-place object writes are covered at the representation layer, `object_put`);
- through a function argument and return; a closure's capture, read and mutated inside the
  closure; nested mutation (an element array, an object's member array, an object inside an
  array, `same(e, f)` on two reads of one element);
- `thread()` (compiled as a CP22 cooperative task in this test, `cooperative_threads`): two
  aliased arguments arrive as one alias-preserving copy (the callee's push is seen through its
  second parameter), the caller's array is unchanged, and a returned array or object is a
  distinct copy; async: the argument is copied at task creation (D7), so neither side sees the
  other's push;
- a persistent `VmSession` across statements: two bindings of one array are one heap object and
  a push through one is seen through the other; the same for an object through its member
  array; `copy` is a new object; a closure defined in one statement mutates both in a later one;
  a cycle through the alias is refused across statements, the receiver unchanged. Sessions refuse
  `same()` (`builtin-lazy-args:same`), so identity there is compared on the heap objects.

Gate status (local, focused checks only, owner ruling of 2026-11-03): `test-rt-refrep` 79
checks, 0 failures (production checks PASS); `test-rt-refrep-mutants` 8 of 8; `rt-lint` 0.

## 16. Commit 3: sort protection (owner requirement E)

While an in-place comparator sort runs, its receiver is protected: header bit 4
(`kSortProtected`, `heap.h`; bit 4 was unused, the kind uses bits 0-3) plus a per-VM registry
(`Vm::sort_protection_` of `SortProtection` records, `rt/vm_sort_protection.h`: the receiver,
the running task, the controller's registers and call site; process-wide counts of protections
set now and ever, for tests that check a release in another VM). `mutation::refuses_mutation` tests `kImmutable | kSortProtected` in one tag-byte test, so
every protocol entry point (and the VM's own stage 2.2 checks, which call it) refuses any
mutation of the receiver through any holder; the refusal (`Failure::SortProtected`, text
`mutation::kSortReceiverMutated`) is raised by the VM as the catchable
`runtime.sort_receiver_mutated` (`Vm::handle_bail`, which converts exactly that protocol text;
the code is registered as recoverable). The sort itself continues. A recursive sort of the same
receiver is such a mutation (refused at its own stage 2.2); a nested sort of a different array
works. The copy-on-write detach into a spare buffer is deleted from `collection_sort_resume`
(the receiver is sorted in place whoever holds it), with the spare's allocations; the budget
reservation formula is unchanged (now an over-estimate).

Release is deterministic, never lazy, on every exit path:

| Exit | Release | Direct check (`refrep_vm.cpp` `sort_protection`) | Mutant |
| --- | --- | --- | --- |
| completion | `op_collection_sort` when `More` turns false | registry 1 and bit set at a print inside the comparator; registry 0 and bit clear at the print right after the sort | S1 |
| a catchable error out of the comparator (drained) | `unwind()`: after the drain, every protection whose controller is no longer active | registry 0 and bit clear at the first print of the `catch` | S2 |
| the receiver mutated inside the comparator, caught outside | same | same | S2 |
| a mutation caught inside the comparator | none: the sort goes on (still protected) | registry 1 and bit set at the `catch`'s print; sorted result | S5 |
| a stop (a non-bool answer) | every exit of `Vm::run` (a scope guard) | registry 0 and bit clear after the run | S4 |
| task teardown (an async task's sort fails uncaught) | the task runs in its own isolate VM (the production model, `parallel_tasks`), whose `Vm::run` exit releases it | at main's `catch` print: process-wide live protections 0, main's registry 0; at least one protection was started | S4 |
| an exception out of the run (the VM's shutdown path) | the `Vm::run` scope guard; also `~Vm` | bit clear after the exception, registry 0 | S4 |
| the end of a session statement | `Vm::run` exit (each statement is one run) | `VmSession::sort_protections()` 0 and bit clear after the statement; the next statement mutates the array | S4 |

The checks read the registry and the header at the moment each exit completes, through the VM's
synchronous output flush (`flush_threshold` 0: the test's callback runs at each print), never
by attempting a further mutation. Mutants (`make test-rt-sort-protection-mutants`, each a build
of `vm_collection_sort.cpp` with `NIFT_SORT_MUTANT`): S1 completion keeps the protection (caught
by sort-complete), S2 the unwind sweep keeps it (sort-error), S4 run exit and destruction keep it
(sort-stop, also sort-task and sort-session), S5 the bit is never set (sort-complete): each fails
`refrep_vm`. There is no cooperative task-teardown release: under the cooperative model a
coroutine program with a comparator sort is refused (`coro-collection-callback`), so no
cooperative task can hold a protection across its teardown; a release there would be unreachable
code, and its mutant could not be caught (it was written first and removed for that reason).

Reachability: on this toolchain the production VM refuses the comparator sort at compile time
(`method:sort`, libstdc++ outside the admitted profile), and the guard refuses aliasing the
receiver; so E is exercised in the probe configuration, which lifts that refusal (14.3).

`Vm::run` is now a wrapper holding the release guard around the unchanged dispatch loop
(`Vm::run_loop`), so the release covers every exit including exceptions, within rt-lint's
function-length limit.


### 16.1 Gate status (commit 3; focused local checks only, owner ruling of 2026-11-03)

On the commit-3 tree (result files `scratchpad/b2/results-c3/`, tree identity per gate): `nift`,
`test-rt-lint` (0 problems), `test-rt-protocol-compile-fail`, `test-rt-mutation-protocol` (191
checks) and `-mutants` (6 of 6), `test-rt-refrep` (112 checks; production checks PASS) and
`-refrep-mutants` (8 of 8), `test-rt-sort-protection-mutants` (S1, S2, S4, S5 caught),
`test-runtime2-refrep-cli`, `test-runtime2-refsem` and `-refsem-mutants`,
`test-runtime2-collections` and `-collections-mutants` (11 caught): 0 each. The heavy gates
(sanitizers, TSan, `test-rt-value`, `test-rt-vm`, safety, VM-default, CP19, the full `make test`,
classify and perf) are for the Linode.

## 17. Review of `94bfce4b`: REJECT (narrow), and the repair

The independent review of `94bfce4b` (tree `d20fa121`) was a narrow **REJECT**: two semantic
violations visible in production (H1, H2, one root cause), two required items (M1, M2) and LOW
items L1-L8. It passed A, C, D, E (including the removed S3), probe isolation, the 1b migration,
self-splice, the CP18 source-list fix and the compile-fail fix. The REJECT and its evidence stand
on record; the repair is new commits on top of `94bfce4b` (no history rewritten). The owner
accepted the REJECT and the repair scope, with refinements (recursive freshness, the case table,
the M1 publication boundary, typed sort errors, carry-forward owners, evidence by tree).

### 17.1 H1/H2: a compiled constant is a description, not a runtime instance

Finding: a JSON-spelled object literal (`compiler_expr.cpp` `object_literal`) was parsed once
and loaded with `LoadK` from one pool constant, so every evaluation shared one object: its
nested arrays were shared mutable state across loop iterations and calls (review `t/d1.f`: B2
`2 3 4`, base and legacy `2 2 2`; `t/d2.f`), and `same()` answered `true` for two evaluations
(`t/s3.f`). The blanket `mark_immutable` on array/object constants hid nothing (only the top
level) and the clone-site audit row "constants" was wrong (corrected in section 2).

Invariant (owner): every evaluation of an array or object literal produces a fresh outer value,
and every nested aggregate in it is fresh too (recursive freshness); a compiled constant is a
description, not a runtime instance. Fix:

- `OpCode::LoadKFresh` (cold): `R[a]` = `clone_constant(K[b])` (`copy_graph.cpp`): a new array
  or object for every node of the constant tree, scalars and strings shared (immutable), the
  whole footprint reserved against the VM's heap limit first (a budget stop builds nothing).
  A parsed JSON literal is a tree (no sharing, no cycle), so no identity map is needed (the
  first version used `deep_copy`: 7,783 Ir per evaluation of a two-key literal; `clone_constant`:
  4,291; the same literal written so it compiles to construction ops costs 4,569 at `919b5669`
  and 4,433 now; `919b5669`'s shared `LoadK` 1,887 was the defect's cost). Local callgrind,
  guidance only; Linode is authoritative. `ops/object_member` evaluates its literal once, outside
  the loop.
- `object_literal` emits `LoadKFresh` for an array/object constant (code comment states the
  invariant at the load site).
- `FunctionBuilder::constant` no longer marks array/object constants immutable; only the
  internal `sort_by` directions (`compiler_collection.cpp`) are marked.

Re-audit of every site that loads an aggregate from a constant pool, a cache or a prepared or
snapshot structure:

| Site | Per-evaluation identity |
| --- | --- |
| `compiler_expr.cpp` `object_literal` (JSON-spelled `{...}`) | was shared (H1); now `LoadKFresh` |
| other object/array literals (`NewObject`/`ObjectSet`, `NewArray`) | fresh by construction |
| `compiler_expr.cpp` constant expressions (`constant_of`) | numbers, strings, booleans, null only |
| global initialisers, session statements, module bodies, closures, `obj.get(k, default)` | compiled by the same compiler: the same two literal paths |
| struct field initialisers (`structs.cpp` `constant`) | scalars, strings, or `map()`/`set()`/`stack()` built per instance by `CallNative` |
| `sort_by` directions (`compiler_collection.cpp`) | internal, read only by the step, never user-visible: one immutable constant |
| script/module invocation `args`, `cmd` (`compiler_invocation.cpp`) | `NewArray` of string constants per run, then `MakeImmutable`; `cmd` a string |
| isolate task wrapper (`isolate_task.cpp` `build_wrapper`: `LoadK` of transferred globals/args) | the wrapper runs once per task; the values are that task's own transfer copies |
| isolate program snapshot (`isolate_snapshot.cpp`, `constants_from`) | the constant pool is transferred to the task's program as templates; `LoadKFresh` copies per evaluation there too |
| cell selector descriptors (`cell.cpp`) | internal; private clones (section 2) |
| session values (`VmSession::value`), prepared/legacy paths | no aggregate is cached for re-evaluation |

Case table (owner), production fixtures with the guard on unless marked probe:

| Case | Required | Covered by | Fails on R-B7 / `94bfce4b`? |
| --- | --- | --- | --- |
| Repeated evaluation of an object literal | fresh each time | `refsem/parity/b2_literal_fresh_call.f`; session `session_literal_fresh_statement` (refsem_a.py, one statement) | yes: `[1,5] [1,5,5]`; session `2 3 4` (fix `2 2 2`) |
| Repeated evaluation of a nested array literal | no mutation leak | `refsem/parity/b2_literal_fresh_loop.f` (the nested array of an object literal); probe `literal-nested-json` (depth two and more) | yes: `2 3 4`; probe output differs |
| Two calls returning object literals | distinct (`same` false) | `collections/approved/same_object_literal.f` | yes: `true` (fix `false`) |
| Literal evaluated in a loop | no state between iterations | `refsem/parity/b2_literal_fresh_loop.f` | yes: `2 3 4` |
| Aliases to one evaluated literal | shared identity (`same` true) | `same_object_literal.f` (`same(a, a)`); probe `literal-alias` | **no, by nature**: the defect adds sharing, so aliases stay identical on R-B7 too. Production refuses the alias forms that could show more (`aliasing`); the probe row is a positive guard, and lost aliasing is what R-B2 (refsem 24, `alias-array`) catches |
| `copy` / `deepcopy` | new objects sharing (copy) or not (deepcopy) this evaluation's contents | probe `literal-copy` (a push through `copy(x)` is not seen by the next evaluation); `collections/approved/same_object_literal_copy.f` (identity and equality only) | probe: yes (`2 2`, fix `2 1`); the production fixture: **no** (`copy`/`deepcopy` give new outer objects on R-B7 too, and production refuses reaching a copy's member, `aliasing`, and the loop over a copy, `reference-guard:unresolved`; legacy does run the copy-loop form, the re-check's `rc/c3/p/cp1.f`, printing `2 2`; the form shown first in this row was a wrong program of mine, corrected after the re-check) |
| Literal built inside a closure | fresh per evaluation | `refsem/parity/b2_literal_fresh_closure.f` | yes: `2 3` |
| Module function returning a literal | no cross-call leak | `refsem/modules/literal_fresh_module` | yes: `2 3` |

Mutant R-B7 (refsem 31, `vm_report.cpp`: `LoadKFresh` loads the shared pool constant again) is
caught by the refsem suite (6 failing checks: the call, closure and loop parity cases, the module
case, and the one-statement session case with its session summary) and by `refrep_vm` (`literal-nested-json`,
`literal-copy`). Re-review C3: the first sessions row (`session_literal_fresh`, two statements)
passed on R-B7 and on `94bfce4b` (`2 2`), so it tested nothing; it is kept as a parity case and
the one-statement case now covers the row. Each row was re-run on the fix, R-B7 and `94bfce4b`
(table).

### 17.2 M1: mutation authority and the publication boundary

Invariant (owner): published, existing aggregates are modified only through the mutation
protocol; unpublished values under construction may use private construction helpers, with an
explicit boundary those helpers cannot cross.

- **Sort commits: closed at compile time.** `Protocol::sort_storage`, `sort_items` and
  `commit_permutation` are private; their only friend is `mutation::SortController`
  (`rt/sort_controller.h`), included by `collection_sort.cpp` and `collection_plain_sort.cpp`
  only. The free `mutation::sort_storage` / `commit_permutation` are removed. `absent.cpp`
  asserts all five are unreachable (each detector fails against `94bfce4b`'s `mutation.h`), and
  `bypass_sort.cpp` (the reviewer's `Protocol::sort_items(r).clear()`) must fail to compile.
  rt-lint flags `SortController`, the sort names and `rt/sort_controller.h` in any other file.
- **Construction: the boundary is "fresh", enforced by lint plus `check_fresh`, not by the type
  system** (superseded in part by section 18: the sort side is now a passkey, the lint matches
  identifier tokens, and the "why not" below is a scope decision, not an impossibility). Why not
  at compile time (as first argued): the construction functions (`array_push`,
  `array_build_storage`, `array_build_set`, `object_set`, `object_build_slot`,
  `struct_add_field`, `map_build_set`, `set_build_add`, `stack_build_push`) take a `Value&`, and a
  fresh value and a published one have the same type; publication is a run-time property (the
  constructing code is the only holder: refcount 1, not immutable). Making it a type (a builder
  returned by `make_*`, consumed by `finish()`) changes about 95 production call sites in 22
  files and about 230 test call sites: outside B2's scope (reported to the coordinator). The
  enforcement instead: (1) every production use outside the files that implement the API carries
  `// rt-lint: storage-ok fresh, <why>` (67 sites, each checked to construct a value the code
  alone holds and has just made, or a cell's private descriptor, or inside a mutant-only `#if`);
  rt-lint fails any unannotated use (the publication boundary is the annotation, reviewable per
  site); (2) `check_fresh` aborts in checked and sanitizer builds if the receiver is shared or
  immutable (it found two test misuses, 13.6b); (3) M-B2 (refsem 30) is caught behaviourally.
  The lint fixture `bad_construction.cpp` holds the reviewer's `bypass_public.cpp` cases and must
  fire on exactly those five lines (rt-lint self-test).

### 17.3 M2: typed sort-protection errors

The catchable `runtime.sort_receiver_mutated` was chosen by matching the message text, so an
ordinary fatal error that echoed the words became catchable with that code (`t/mag4.f`). Now
`Fault::sort_protected` is set by `raise_refusal` exactly where an `Outcome` with
`Failure::SortProtected` is raised (the per-element refusals, `mutation_failed`, the
`ArrayMutate` stage 2.2, the sort's own stage 2.2), and `Vm::handle_bail` converts on that flag
only. Tests: `refsem/parity/b2_error_text_not_code.f` (`t/mag4.f`: the fatal error stays fatal,
as legacy and the base); a wording control (`test-rt-sort-protection-mutants`: `mutation.cpp`
built with `NIFT_SORT_WORDING_TEST` words the refusal differently, and `refrep_vm` must PASS:
same catchability and code).

### 17.4 LOW items fixed

- L4: the VM's self-splice copy (`vm_array_mutation.cpp`, c = 2) reserves its storage (stage 2.5)
  before the copy, after stages 2.1-2.2.
- L5: `refrep_production.sh` checks that production refuses an object alias with `(aliasing)`
  (the probe lifts `no_alias` in `compiler_rules.cpp`, which the marker symbol in `compiler.cpp`
  does not witness); the reviewer's probe-built `nift` runs the program, so the check fails there.
- L6: `cow_negative.py` dropped the "Outstanding allocations after program destruction delta" stdout
  conjunct because the retargeted mutant (`array_shallow_clone`, unowned while copying) is no
  longer reached by the program-API campaign that prints that line (the old ABI program, a COW
  clone of a shared argument, is refused by the guard); it is reached by the direct clone
  campaign, whose per-failure leak assertion `outstanding_allocations == empty_allocations` is the
  one now required in stderr (with exit 1). Requiring the old line would make the control fail to
  detect a real leak.
- L7: coordinator ruling 5's "with lazy release" corrected (release is deterministic, section 16).
- L8: the B1-perf carry item "the map `set` budget case through KeyedMutate" is covered by
  `tests/runtime2/collections/heap/budget_map.f` (`weekend_collections.py`: `m.set(i, i)` in a
  statement is a `KeyedMutate` MapSet; with `NIFT_VM_HEAP_BUDGET=2000000` it must stop with exit
  70, "memory budget exceeded" and no "done"; unbounded it completes).

### 17.5 Carry-forward (deferred LOW items, with owners)

Each is unreachable with the guard on (the reviewer's evidence) and assigned to a release dependency:

| Item | Evidence of unreachability (guard on) | Owner |
| --- | --- | --- |
| L1: the in-place comparator sort exposes intermediate state (placeholder `""`/`[]`, duplicates) to an alias read inside the comparator (`t/p5.f`, probe) | the guard refuses the alias; production effect analysis refuses comparator access to the receiver (checked with an admitted-profile build, `t/h1`-`h5`) | **B5, mandatory test**: Q7's sort-a-copy-and-commit-once must cover aliased reads during the sort, not only mutations |
| L2: `cell.cpp` `replace_path` / `cell_cached_put` moves the owner value out of its cell for the duration (a residual take), uses a default `mutation::Env` (no VM cycle graph, no heap limit), and commits a deeper level before validating the parent (a parent refusal would leave a partial mutation) | not reached from production by the reviewer | **B5/B6**: give it the VM's Env, validate every level first |
| L3: `ArrayMutate c = 100` (the static const-array path) reports "pop: array is empty" before the immutability error (section 6 named only `set_index`) | `args.pop()` is `reference-guard:unresolved` (`t/ap.f`) | **B5**, with the static path |

### 17.6 Evidence by tree

Only the final frozen tree's results count toward certification.

| Tree | Evidence | Status |
| --- | --- | --- |
| `ec7c885a` (commit 2 tip) | Linode run (partial): `test-rt-value-sanitize` failed (two test ports, 13.6b), `test-rt-protocol-compile-fail` failed (locale, 14.1) | intermediate; superseded and stopped |
| `67527994` | Linode reruns of `test-rt-value-sanitize`, `test-runtime2-cp18-sanitize`, `test-rt-value` | intermediate |
| `ea082292` | compile-fail fix: clean clone, C and C.UTF-8 PASS | intermediate |
| `94bfce4b` (commit 3 tip) | review REJECT (H1, H2, M1, M2); Linode intermediate: 39 gates passing, pre-existing failures only | **REJECTED**; intermediate |
| `534f9af8` (repair tip) | re-review ACCEPT-WITH-CONDITIONS (C1-C3, LOW); Linode run started | intermediate |
| `eba6ca1f` (conditions tip) | re-check CONDITIONS MET; R1-R3 found; Linode run started (about 20 minutes, stopped) | intermediate |
| final frozen tree (after section 19) | focused local checks (19.4); Linode certification pending | the only tree that counts |

### 17.7 Gate status of the repair (focused local checks)

`nift`, `test-rt-lint` (0 problems; self-test 10 fixtures), `test-rt-protocol-compile-fail`
(absent.cpp and 4 bypass cases), `test-rt-mutation-protocol` (191) and `-mutants` (6 of 6),
`test-rt-refrep` (115 checks; production checks incl. aliasing PASS) and `-refrep-mutants` (9 of
9 incl. R-B7), `test-rt-sort-protection-mutants` (S1, S2, S4, S5 caught; wording control PASS),
`test-runtime2-refrep-cli`, `test-runtime2-refsem` (473) and `-refsem-mutants` (23 caught incl.
31), `test-runtime2-collections` (279) and `-collections-mutants`: 0 each. Heavy gates: Linode.

## 18. Re-review of `534f9af8`: ACCEPT-WITH-CONDITIONS, and the conditions

The re-review of `534f9af8` (tree `a1399e9a`) resolved H1, H2, M2 and L4-L8 and set three
conditions (C1-C3) and one LOW item; fixed in new commits on top of `534f9af8` (no rewrite). The
Linode run on `534f9af8` is intermediate evidence (17.6).

### 18.1 C1: the sort storage closed at compile time (passkey)

`rt/sort_controller.h` re-exported the private sort commits through a public struct, so any file
including it could clear a published array (`rr/lint/sort_plain.cpp`), and `rr/lint/sort_bypass.cpp`
(an include path spelled `rt//sort_controller.h`, a token-pasted name) passed rt-lint. Now every
`SortController` function takes a `mutation::SortKey`, whose only constructor is private and
user-provided (a defaulted one would leave `SortKey` an aggregate in C++17, so `SortKey{}` would
compile from anywhere: verified, and `absent.cpp` asserts `!is_aggregate`) and whose only friends
are `collection_sort_init`, `collection_sort_resume` and `collection_plain_sort`. Compile-fail
cases: `bypass_sort_key_{storage,items,commit_permutation}.cpp` (each accessor with a forged key:
`{}`, `SortKey{}`, `SortKey()`), `bypass_sort_paste.cpp` and `bypass_sort_plain.cpp` (the reviewer's
two files); `absent.cpp`: `SortKey` is neither default-constructible nor an aggregate outside.
Correction after the re-check (section 19): the closure is to well-formed ordinary code, and
section 19.1 replaces the friendship with a key on the commits themselves. The routes that still
pass a key check, as the re-checker found them (`rc/forge/`): a reference to an inactive union
member holding a `SortKey` (f06, no cast), storage casts, `memcpy` and a null reference (f07, f08,
f17, f18: deliberate undefined behaviour), and the explicit-instantiation access loophole (f20,
f21: well-defined C++17, [temp.explicit] performs no access check, which no access-control design
can close; it opens `Storage` equally). The guarantee: **closed to well-formed ordinary code; the
remaining routes are deliberate undefined behaviour or a known language loophole, defended by
lint and review.**

### 18.2 C2: construction lint on identifier tokens; `check_fresh`; the scope decision

- rt-lint's construction rule now matches the identifier anywhere in code (comments and strings
  stripped), so a qualified (`rt::array_push`), parenthesised (`(array_push)(...)`), address-taken
  (`&array_build_set`) or `using`-imported reference counts. A line may hold one construction
  reference (one annotation covers one reference: the reviewer's "blanket annotation" form
  fails). Token pasting (`##`) needs `rt-lint: paste-ok` (the VM's three dispatch-label macros);
  the `sort_controller.h` include is matched on the raw line. Fixture `bad_bypass_forms.cpp`
  holds every `rr/lint/vm_bypass.cpp` form, the `sort_bypass.cpp` lines and the reviewer's
  `push_value` patch; the self-test requires exactly its 13 flagged lines. The reviewer's
  `push_value` patch applied to `vm_ops.cpp` now fails rt-lint.
- Labels re-checked at every site: `fresh` (a value the code alone holds and has just made),
  `private-descriptor` (`cell.cpp`: the cell's own selector descriptor, refcount 1, never
  user-visible, written after its creation: not fresh), `mutant-only` (inside a test-mutant `#if`:
  `vm_keyed.cpp` R14, `vm_ops.cpp` M-B2). Three reasons were made precise (`vm_report.cpp`
  `NewArray`/`ObjectSet`, `vm_struct.cpp` `StructAdd`, `session_invocation.cpp`).
- `check_fresh` (checked and sanitizer builds) also refuses a sort-protected receiver. Its limit,
  stated plainly: it detects sharing, not publication; a published value held by one holder (a
  user binding in one register) has refcount 1 and passes. So the construction boundary is the
  lint's per-site annotation plus this run-time check of sharing.
- Scope decision (not an impossibility): a compile-time construction capability (a builder or
  passkey type returned by `make_*` and consumed by `finish()`) is feasible; it touches about 95
  production call sites in 22 files and about 230 test call sites, so it is proposed as a later
  tooling item for the owner to schedule, outside B2.

### 18.3 C3: the case table re-checked against the defect

See the table in 17.1 (new column): every row's covering test was run on the fix, on R-B7 and on
the rejected `94bfce4b`. The sessions row now uses one statement (`session_literal_fresh_statement`:
`2 3 4` on R-B7 and `94bfce4b`, `2 2 2` on the fix); the copy row gained the probe `literal-copy`;
the alias row cannot fail on the defect by its nature (it adds sharing), stated in the table.

### 18.4 LOW: allocation failure in `LoadKFresh`

`cow_allocation.cpp` `literal_campaign`: `clone_constant` of a nested template (object, object,
arrays, strings) failed at each of its 11 allocation sites (nothing leaks, the output stays null:
no partially built value escapes, the template unchanged; on success every node is new), and a
program evaluating a nested JSON literal twice through the program API failed at each of its 43
run-time allocation sites (internal error, no result, nothing leaks; the next run prints `2 2`).
The ASan/LSan run of this campaign is the CP18 sanitizer lane (Linode).

### 18.5 INFO from the re-review (no change)

- Retained literals now cost heap on every evaluation: `rr/p/bud1.f` (2,000 retained
  `{"a": [...2,000 ints...]}`) stops under a 2 MB budget where `919b5669` completed (it kept
  2,000 references to one constant). This follows from the freshness ruling and matches legacy's
  model; a non-retained loop (`bud2.f`) is unaffected.
- `parallel_task_start.cpp` `finite_timer_body` does not admit `LoadKFresh`: a timer task whose
  body builds a JSON literal no longer qualifies for the finite-timer helping path (conservative,
  not a semantic change).
- An extra effect of H1: `thread()` tasks looping over a literal (`rr/p/i1.f`) leaked state
  across tasks on `94bfce4b` (`9 9 9`); the fix gives `6 6 6`, as `919b5669` and legacy.

### 18.6 Gate status (focused local checks)

On the conditions tree (result files `scratchpad/b2/results-cond/`): `nift`; `test-rt-lint`
(self-test 12 fixtures, `src/rt` 0 problems); `test-rt-protocol-compile-fail` (absent.cpp, 9
bypass cases, control: 11 PASS); `test-rt-mutation-protocol` (191) and `-mutants` (6 of 6);
`test-rt-refrep` (116 checks; production checks PASS) and `-refrep-mutants` (9 of 9, R-B7 caught
by `literal-nested-json` and `literal-copy`); `test-rt-sort-protection-mutants` (S1, S2, S4, S5;
wording control PASS); `test-runtime2-refrep-cli`; `test-runtime2-refsem` (474) and
`-refsem-mutants` (23 caught, R-B7 with 6 failing checks); `test-runtime2-collections` (279) and
`-collections-mutants` (11): 0 each. CP18 `cow-allocation` (ordinary build): "COW final: 0
failures", the literal campaigns included. Heavy gates: Linode, on the new tip only.

## 19. Narrow re-check of `eba6ca1f`: CONDITIONS MET, and three new LOW items

The narrow re-check of `eba6ca1f` verified C1, C2, C3 and the LOW item with no regression and
found R1-R3 (LOW; none reachable from a script). R1 and R2 are fixed in one commit on top of
`eba6ca1f`; R3 is carried. The Linode run on `eba6ca1f` (about 20 minutes in) is intermediate.

### 19.1 R1: the sort key on the commits themselves (compile time)

`mutation.h` only forward-declared the friend `SortController`, so a translation unit including
only `mutation.h` could define its own and reach the private sort commits without a key (an ODR
violation, no diagnostic; the re-checker's `rc/forge/f22` emptied an immutable array). Now
`SortKey` is defined completely in `mutation.h` (a second definition does not compile), and
`Protocol::sort_storage`, `sort_items` and `commit_permutation` each require it: no friendship
remains, so a self-defined helper class gains nothing. `SortController` (`sort_controller.h`) is
a named forwarder that also takes the key. Chosen: compile-fail, not lint:
`bypass_sort_redefine.cpp` (f22) fails ("no matching function ... sort_items(Value&)"; it compiles
against `eba6ca1f`'s `mutation.h`), `bypass_sort_key_redefine.cpp` (a public `SortKey` of one's
own: "redefinition of class SortKey"); `absent.cpp` asserts each commit is not callable without
a key. Not closed (as 18.1): f06 (inactive union member), the undefined-behaviour forgeries, and
the explicit-instantiation loophole.

### 19.2 R2: `StorageTestAccess` outside the test tree fails lint, repository-wide

Since commit 1, `Storage` befriends the forward-declared `StorageTestAccess`, defined only in
`tests/rt/storage_access.h`; a production translation unit defining it is the program's only
definition (well-formed) and gets every storage commit (`rc/forge/f23`). Chosen: the second of the
coordinator's options, because the first (the friend under a test-only macro) makes the class
definition differ between test and production translation units that link the same objects (an
ODR difference of its own) and needs the macro on the 28 Makefile build lines that compile the eight test sources using it. `rt-lint.py
--test-access src include bindings` (in `test-rt-lint`) scans every C/C++ source and header kind,
`.inc` included, outside `tests/`: the name may appear only on `aggregate.h`'s two declaration
lines, annotated `rt-lint: test-access-decl`. Control: lint fixture `bad_test_access.cpp` (f23)
must be flagged on exactly its two lines (self-test, 13 fixtures), and f23 copied under a `src/embed`
tree is flagged. It is a lint, not a compile-time closure; the compile-time form belongs with the
builder type (19.3).

### 19.3 R3 carried: rt-lint tokenizer gaps (post-B2 tooling checkpoint)

rt-lint's line scanner misreads these C++14/17 lexical forms, so a construction call can hide
behind them (re-checker's `rc/lintb/rt/vm_bypass2.cpp`; each compiles under g++ and clang++ with
`-pedantic` and lints clean; end to end, `rc/m-digit/`: a digit separator before
`rt::array_push` in `push_value` lints clean, links into `nift`, and refsem fails 5 checks):

| Form | Example |
| --- | --- |
| digit separator | `int n = 1'0; array_push(published, x);` (the `'` opens a char literal to the line's end) |
| raw string | `R"(")"; array_push(...)` |
| line splice inside the identifier | `array_\` then `push(...)` |
| digraph paste | `%:%:` (the alternative token for `##`) |
| annotation text inside a string literal | the annotation is searched on the raw line |
| raw-string comment desync | `R"(")" "/*"` hides the next lines until a later `"*/"` |
| `.inc` files are never opened | `src/rt/natives/builtin_list.inc` is the precedent |

Assigned to the post-B2 tooling checkpoint, with the compile-time builder type (18.2), which
would make the construction lint unnecessary. Now: `test-rt-lint` greps every `.inc` under
`src/rt` for the construction, sort and test-access names and fails on any (none today).

### 19.4 Gate status (focused local checks)

On the R1/R2 tree (result files `scratchpad/b2/results-r123/`): `nift`; `test-rt-lint` (self-test
13 fixtures, `src/rt` 0 problems, test-access scan of `src`, `include`, `bindings` 0 problems,
`.inc` check PASS); `test-rt-protocol-compile-fail` (absent.cpp, 11 bypass cases, control: 13
PASS); `test-rt-mutation-protocol` (191) and `-mutants` (6 of 6); `test-rt-refrep` (116) and
`-refrep-mutants` (9 of 9); `test-rt-sort-protection-mutants` (S1, S2, S4, S5; wording control
PASS); `test-runtime2-refrep-cli`; `test-runtime2-refsem` (474) and `-refsem-mutants` (23);
`test-runtime2-collections` (279) and `-collections-mutants` (11): 0 each. Heavy gates: Linode,
on the new tip only.

