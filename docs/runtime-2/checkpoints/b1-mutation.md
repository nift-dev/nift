# B1: the mutation protocol and immutable objects (cand/b1-mutation)

Status: **candidate**, not reviewed, not integrated. Branch `cand/b1-mutation`,
built on `weekend/r2` @`d209febd` (B0 integrated). Plan: Phase B plan §5.1-5.4
and §6 "B1"; owner decisions of 2026-11-02 (Q1 args, Q2-B const, Q7 sort).
B1 is a value-layer refactor with **no observable change**: arrays and
objects stay copy-on-write, the Phase A guard stays on, `unshare`, take/put
and the name-based const checks all stay, legacy (`NIFT_VM=0`) is unchanged.

Tests: `make test-rt-mutation-protocol` (`tests/rt/mutation_protocol.cpp`),
`make test-rt-mutation-protocol-mutants` (M-B1a/b/c); both are prerequisites
of `test-rt-vm`. The rt-lint rule `storage-write` runs in `test-rt-lint`.

## 1. Contract

One protocol, `src/rt/mutation.{h,cpp}`, for every mutation of an existing
array, object, struct instance or map/set/stack, from a VM instruction or a
native. A mutation that fails validation changes nothing: the receiver is
byte-identical, the live heap and every refcount are unchanged. Argument side
effects already performed while evaluating the operands are not undone (the
F2 ruling).

**Stage order (fixed):**

1. **evaluate**: the compiler has put the receiver and every operand in
   registers before the mutating instruction (F2; unchanged in B1).
2. **validate**, writing nothing:
   1. receiver kind: an ordinary error ("`<method>`: receiver is not ..."), never internal;
   2. the `kImmutable` check: "cannot mutate const array: `<receiver>`" (object/struct/map/set/stack likewise);
   3. bounds and argument kinds (legacy's texts: "pop: array is empty", "map: key must be bool, number, or string", ...);
   4. the cycle check for **each** stored value, in operand order (map: key, then value);
   5. the budget reservation (growth, plus the copy-on-write clone of a shared array).
3. **commit**: one structural change. Splice and sort build the result on the
   side and swap it in (§4).
4. **bookkeeping**: `note_store` and the identity summary bit, `heap_retally`,
   the checked-build `relocate_storage` (D9 schedule unchanged; applied as the
   commit starts, which is what makes a stale reference fault) and clearing
   `kStorageLent` (inside `relocation_due`, unchanged).

The validated entry points (`mutation::push`, `set`, `insert`, `remove`,
`pop`, `reverse`, `splice`, `object_put`, `object_erase`, `field_set`,
`map_put`, `set_insert`, `collection_remove`, `stack_push`, `stack_pop`,
`clear`) run stages 2-4 and return an `Outcome` (`Failure::Kind | Immutable |
Bounds | Argument | Cycle | Budget`, the text, the failing operand). The VM
maps it with `Vm::mutation_failed`: a cycle is the catchable
`runtime.reference_cycle` (legacy's text when the stored value contains the
receiver directly, as before), a budget failure the existing budget stop, the
rest an ordinary error. Where legacy's texts interleave with the stages
(`ArrayMutate`, comparator sort), the instruction calls the stage functions
(`check_mutable`, `array_reserve_bytes` + `reserve`) in the same order and then
one commit function (`commit_array`, `commit_permutation`, `sort_storage`).

The storage-level commit primitives stay where they were (`aggregate.cpp`,
`aggregate_collection.cpp`: `array_push`, `object_set`, `struct_set`,
`map_set`, ...), as the protocol's commit layer: called on an existing
receiver only from `mutation.cpp`; elsewhere only to build or tear down fresh
values. This keeps the Phase A mutants that live in them (R2, R19, M5) where
their scripts compile them; the rt-lint rule (§5) enforces the boundary.

**B1 transition rules** (removed in B2 when `unshare` goes):

- Stage 2.2 applies to an array or object only when the commit would write
  the object itself (`refcount <= 1`). A shared one is cloned by the commit,
  so the immutable object is still never written and the clone (a value
  copy, under COW) is mutable. This is what makes B1 unobservable even on a
  path that might share `args` without the guard noticing. Structs and
  collections, already references, are immutable through every alias.
- Stage 2.4 runs for struct and collection receivers. Arrays and objects are
  acyclic by construction while copied, and a pre-commit walk would see the
  pre-clone object and refuse stores legacy and Phase A accept
  (`s.f = a; a.push(s)`), so it is skipped for them. `closes_cycle` itself
  accepts array and object targets (§3).
- The cycle walk is the existing one: no unique-root shortcut (plan §5.4 rule
  ii is B2's, with soundness evidence).

## 2. Mutator inventory

Every mutator of an existing aggregate, and its route. "VM" is the opcode or
handler; "stages" names the protocol functions it calls.

| Mutator | Entry (VM / native) | Route through the protocol | Stage 2.4 | Stage 2.5 |
| --- | --- | --- | --- | --- |
| `a.push(x)` statement, `Push` op | `vm_ops.cpp push_value` | legacy kind text, then `mutation::push` | skipped (COW) | none (counted after the step by `memory_ok`, as before) |
| `a[i] = v` | `vm_ops.cpp set_index` (`SetIndex`) | legacy kind/bounds texts, then `mutation::set` | skipped (COW) | none (as before) |
| `pop`/`remove`/`insert`/`clear`/`reverse` | `vm_array_mutation.cpp` (`ArrayMutate`) | kind (B0 text), name const check, `check_mutable`, legacy bounds, `array_reserve_bytes`+`reserve`, `commit_array` | skipped (COW) | growth + clone (unchanged formula) |
| `splice` (removal / insertion, two instructions, Phase A F2) | `ArrayMutate` c=0/2, c=1 | as above; `commit_array(SpliceRemove)` then `commit_array(SpliceInsert)` | skipped (COW) | as above |
| plain `sort()` | `collection_plain_sort.cpp` (from `ArrayMutate`) | `check_mutable` (in `ArrayMutate`), incomparable check, budget, order built on the side, `commit_permutation` | n/a (no store) | unchanged |
| comparator `sort(f)` | `vm_collection_sort.cpp` / `collection_sort.cpp` | `check_mutable` before init, budget, `sort_storage` (the unshared working copy) | n/a | unchanged |
| `s.f = v` | `vm_struct.cpp` (`FieldSet`) | legacy field text, then `mutation::field_set` | yes (unchanged texts) | none |
| `m.set(k, v)` | `vm_keyed.cpp` (`KeyedMutate MapSet`) | `mutation::map_put` | key, then value | growth (moved after the cycle check) |
| `s.add(x)` | `KeyedMutate SetAdd` | `mutation::set_insert` | yes | growth (moved after) |
| `m.remove(k)`, `s.remove(x)` | `KeyedMutate MapRemove/SetRemove` | `mutation::collection_remove` | n/a | none |
| `st.push(x)` | `KeyedMutate StackPush` | `mutation::stack_push` | yes | growth (moved after) |
| `st.pop()` | `KeyedMutate StackPop` | `mutation::stack_pop` | n/a | none |
| `c.clear()` | `KeyedMutate Clear` | `mutation::clear` | n/a | none |
| cell location store (`a[0][1] = v` through a selector cell) | `cell.cpp replace_path` | `mutation::set` / `mutation::object_put` on the copied path | skipped (COW) | none (as before) |
| object member store `o["k"] = v` | refused before effects on the VM (`index-target`); `ObjectSet` is object-literal construction only | `mutation::object_put` / `object_erase` exist for natives, tests and B2 | skipped (COW) | none |
| `merge`, collection helpers (`group_by`, `index_by`, `partition`, ...), `object_methods` | natives | build fresh results (`unshare` of a copy, then commit primitives); B2's audit (§5.2) rewrites the `unshare` ones | - | - |
| snapshots, `copy`/`deepcopy`, transfer decode, `NewArray`/`NewObject`/`NewStruct`/`StructAdd` | construction | fresh values: builders / decoders, annotated or allowlisted | - | - |

Order change, intended by the plan and not reachable by any existing test:
the map/set/stack growth reservation now comes after the key check and the
cycle check (it used to come first), so a program that both overflows the
heap budget and stores an invalid key or a cycle now gets the key or cycle
error rather than the budget stop. No other order changed.

## 3. The immutable bit and `args`

`kImmutable` is header bit 5 (`heap.h`; bit 4 stays reserved for §7's
lazy-snapshot pin). It is set once, after construction, and never cleared on
an object. It is set on the injected `args` array in `invocation_bindings`
(`session_invocation.cpp`), which is the single construction of the value
that scripts (`script_runner.cpp`), sessions and the module/package entry
(`module_invocation.cpp` plans the names, the value is the entry's) install.
`copy(args)` (`shallow_copy`) of an immutable array or object builds a new
outer object without the bit (it used to return the value itself, COW);
`deepcopy` and transfer decoding build new objects, so their copies are
mutable. `const` stays binding-only (Q2-B): nothing freezes a `const` object.

Not reachable through `args` in B1: `args.push(..)` is refused by the
name-based check first (same text), and every aliasing form (`b := args;
b.push(1)`, `f(args)` mutating its parameter, `[args]` loops, a stack round
trip) is refused before any effect by the Phase A guard (`aliasing`,
`reference-guard:inflow`; probed on this branch). The mechanism is proved at
the C++ level (`aliases_and_copies`): the refcount-1 immutable array refuses
with "cannot mutate const array: args"; a C++ alias of it is cloned by the
commit and the immutable object stays byte-identical; immutable struct and
map aliases refuse; `copy`/`deepcopy` results are mutable.

`closes_cycle` with an array or object target: the walk follows every array
and object below the stored value (the identity summary does not yet mark
plain nested arrays; plan §5.1 redefines it in B2), so `s.f = a` then a store
of `s` into `a` is found. The walk for struct and collection targets is
unchanged.

## 4. Sort receiver (owner Q7): design for B2/B5

B1 changes no sort behaviour. Today the comparator sort works on the receiver
moved out of its holder (take/put) and made unique (`sort_storage`), so the
working copy *is* the side buffer and the put is the single swap; a comparator
cannot reach it (under COW the program's copy is a different object, and the
guard refuses the aliasing forms).

Once arrays are shared (B2) the receiver stays in its holder, so the
comparator can reach it. The design, through the shared mechanism:

- the comparator sort copies the receiver's elements into a side array at
  init (the D5 entry snapshot it already needs), sorts that, and at the end
  commits once with a protocol commit that swaps the side storage into the
  receiver (`commit_permutation`'s shape, one write);
- while it runs, the receiver is marked **under sort**: a per-isolate
  registry (a small vector of `HeapObject*` with a depth count; nested sorts
  push and pop) rather than a header bit, since bit 4 is reserved and the
  state is rare;
- `mutation::check_mutable` (stage 2.2) also consults the registry, only when
  it is non-empty (one load and compare on the hot path), and refuses with a
  new catchable `runtime.sort_receiver_mutated` ("cannot mutate an array
  while it is being sorted: `<receiver>`"); unrelated callback side effects
  (other arrays, output) are unaffected;
- the registry entry must be removed on every exit (completion, a comparator
  error, unwinding through `try`, cancellation), so the comparator sort's
  resumable controller needs an explicit release on its error and unwind
  paths; that is the part needing care and adversarial tests in B2/B5.

Complexity: moderate and local (registry, one check, an error code, the
release on every exit). Not implemented in B1.

## 5. rt-lint `storage-write`

`tools/rt-lint.py` rule `storage-write` flags, in any file except
`mutation.{h,cpp}`, the commit primitives (`aggregate*.{h,cpp}`,
`aggregate_store.h`), the construction builders (`natives/support.{h,cpp}`
`ArrayBuilder`) and the decoders (`isolate_transfer_decode.cpp`,
`transfer_heap.cpp`, `isolate_snapshot.cpp`):

- a write to `heap_as<ArrayObject|ObjectObject|StructObject|CollectionObject>()->items|entries|fields`
  (`=`, `push_back`, `insert`, `erase`, `clear`, `resize`, ...);
- a raw storage hand-out (`array_mut`, `array_slot_mut`, `object_slot_mut`,
  `struct_slot_mut`);
- a receiver-mutating commit primitive (`array_set`, `object_remove`,
  `struct_set`, `map_set`, `set_add`, `collection_erase`, `stack_push`,
  `stack_pop`, `collection_clear`; `mutation::` calls are not flagged).

A line that builds or tears down a fresh value it alone holds says
`// rt-lint: storage-ok <why>` (12 lines: deep-copy construction, selector-cell
descriptors, fresh native results and scratch, the R14 and argscmd mutant
lines). `array_push`/`object_set`/`struct_add_field` are construction
primitives and not flagged. Evidence: the self-test fixture
`tests/rt/lint_fixtures/bad_storage.cpp` (three bypasses, one protocol call)
fires exactly `storage-write`; run on the `d209febd` versions of
`vm_ops.cpp` and `vm_keyed.cpp` it fires 9 times (`set_index`'s `array_mut`,
the 8 keyed primitive calls); on this tree: 0 problems.

## 6. Mutants

Compile-time `NIFT_MUTATION_MUTANT` in `mutation.cpp`, built against the
otherwise unchanged objects by `test-rt-mutation-protocol-mutants`; each must
exit 1:

| Mutant | Change | Caught by |
| --- | --- | --- |
| M-B1a | `push` commits before it validates | the immutable push case: receiver changed |
| M-B1b | `object_put` skips the immutable check | the immutable object_put case: no refusal, receiver changed |
| M-B1c | an array edit's bookkeeping skips `heap_retally` | the growth tally check: live heap != tally of the grown storage |

The Phase A refsem mutant R5 ("no reference-cycle check at all") moved with
the check: `tests/runtime2/refsem/mutants.sh` now compiles it into
`mutation.cpp` (`closes_cycle`); the R14 hook stays in `vm_keyed.cpp`.

## 7. Local runs

Code commit `df5d2fe7` (this record is the next commit; no code change
after it). Every step ran alone through the host's heavy-job gate:

| Gate | Exit |
| --- | --- |
| `make nift`; `make test-rt-lint` (self-test 8 fixtures; `src/rt` 0 problems) | 0, 0 |
| `test-rt-mutation-protocol` (79 checks, 60 refusal cases, 0 failures); `-mutants` (M-B1a, b, c each caught, exit 1) | 0 |
| `test-runtime2-refsem` (463 checks: every `approved/` fixture including `cell_splice_*` and `splice_argument_order`, goldens unchanged); `-refsem-mutants` (22 of 22, R5 now in `mutation.cpp`) | 0, 0 |
| `test-runtime2-collections` (273); `-collections-mutants` (11 caught) | 0, 0 |
| `test-runtime2-safety` (fuzzer included); `-safety-mutants` (7 caught) | 0, 0 |
| `test-runtime2-vm-default`, `-args-cmd`, `-args-cmd-mutants`, `-coro-module`, `-cp14b` | 0 each |
| `test-runtime2-cp14b-design-mutants` (first run exit 2: mutant 5's source anchor named the old commit tail of `vm_array_mutation.cpp`; anchor moved to the new tail, same mutation; rerun: 1-11 detected, 12 unverified comparator profile as before) | 0 |
| `test-runtime2-packages`, `-packages-mutants`, `-packages-legacy` (all three lanes) | 0 each |
| `test-cp19-functional`, `test-cp19-session-cli`, `test-runtime2-cp22`, `test-runtime2-cp23` | 0 each |
| `make test-rt-vm` (every prerequisite and recipe line, 4,135 s) | 0 |
| `test-runtime2-refsem-sanitize` (ASan/UBSan, 1,228 s), `test-runtime2-collections-sanitize` | 0, 0 |

Test expectations changed: none. Test infrastructure changed: the R5 file
in `refsem/mutants.sh` and the cp14b design mutant 5 anchor (both follow
moved code).

**Classify diff** against the `weekend/r2` binary (`d209febd`, rebuilt
through the gate): `NIFT_VM=classify` over every `.f` in `tests/runtime2`
(1,816 units): identical output; CP25-R `classify-all` over the release
corpus (589 units; outcome, reason, line, column, reasons and code compared,
temporary paths normalised): **0 changes**.

**Benchmarks (local, not authoritative; Linode at certification).**
`bench-rt-value` (callgrind Ir, deterministic) is identical on base and B1:
copy 4,694,315; push 13,331,799; index 4,000,200; object set 13,759,122
(base) / 13,759,108 (B1); object get 5,121,127 / 5,121,113; guard PASS.
`bench-rt-value-cycle` (wall clock, cycle check cost, base -> B1):
store_plain 0.12 -> 0.51 ns, store_callable 10.28 -> 10.77 ns, store_handle
7.02 -> 7.02 ns, store_nested_identity 32.84 -> 31.56 ns; all within
budget, PASS. The plain-store delta is within this shared host's noise
(the "without" side moved 1.82 -> 1.46 ns between runs); the value benchmark
calls the unchanged primitives, so the VM-level routing (one kind check and
one header-bit test per mutation) is what Linode's element/field/collection
workloads must measure.

Not run locally: TSan lanes, the Linode workloads (plan §6 B1: element and
field writes, nested mutation, collection mutation, 200,000-element stress,
release and sanitizer).

## 8. Size and deviations

Production: `mutation.h` 121 + `mutation.cpp` 363 lines, plus about 190
changed lines elsewhere (within the plan's ~500, mostly routing). No new
analysis pass. Deviations from the plan text, for review:

1. The commit primitives were not physically moved into `mutation.cpp`; they
   stay in `aggregate*.cpp` as the protocol's commit layer (keeps R2/R19/M5
   and the stale-reference build where their scripts expect them), and the
   lint allowlist includes those files.
2. B1's refcount-aware immutable check and the skipped array/object cycle
   stage (§1), both required for "no observable change" under COW.
3. Map/set/stack budget reservation moved after the argument and cycle
   checks (the plan's stage order; §2).
4. `push` and `set` (and struct/collection removal) reserve no budget, as
   before; their growth is still counted after the step.

## Independent review (ACCEPT, 5d4db7fa) and record corrections

**Atomicity scope (corrected wording).** On a validation failure, the receiver and everything
reachable from it are unchanged. Side effects of evaluating operands (index, value, arguments)
and VM scratch registers are excluded (Pop/Remove/Splice write their result register before the
budget check). This replaces any wider "live heap unchanged" claim at the VM level.

**Known exception (pre-existing, also on weekend/r2): sessions lose the receiver binding on a
failed array mutator** (pop on empty, insert/remove out of range, invalid splice), because the
session path moves the receiver out and does not put it back on an ordinary error. Not visible in
scripts (ordinary errors are fatal). B2 must remove take/put or restore the binding, with a
session regression fixture.

**Commitments for B2** (from the review):
- Make the receiver-facing commit primitives private to `mutation.cpp`, or lint `array_push`/
  `object_set` on anything but a just-constructed local; the `storage-write` rule is a heuristic
  (reference/pointer aliases, `std::swap`, casts and `collection_sort.cpp` `raw()` evade it).
- Preserve `kImmutable` across thread/async transfer, or obtain an owner ruling that a transferred
  `args` is a mutable copy (today the decoder builds a mutable object; harmless while name checks
  and the guard remain).
- One implementation for array mutators: the VM ArrayMutate path and `mutation::insert/remove/
  pop/reverse/splice/clear` must be the same code (budget formulas differ today).
- Reserve capacity before the first write in `map_set`, `set_add`, `stack_push` (a `bad_alloc`
  between pushes could leave a key without a value).
- VM-level mutants for the immutability routing once the name checks go; mutants for
  budget-after-commit and key/value cycle order; a session receiver-loss witness.
- Sort registry: tie the entry to the sort's controller/VM frame so unwinding, `try`, coroutine/task
  cancellation, budget stops and isolate teardown release it automatically (owner preference);
  define recursive sort of the same receiver (refused as a mutation), nested sorts on different
  receivers (a stack, scoped), and coroutine mutation while a comparator is suspended (refused).

TSan was run by the reviewer: `test-runtime2-collections-tsan`, 273 checks, 0 warnings.

## B1-perf: the `array_push` regression (cand/b1-perf)

**Finding (Linode `bench-rt-vm`, after the ACCEPT above, kept as recorded):** `ops/array_push`
382 -> 504 VM instructions per op (+32 %) against `d209febd`; every other op identical. Owner
decision: optimise B1 on a separate candidate before integration. `cand/b1-perf` branches from
`8975e8eb`; the B1 commits and the ACCEPT above are unchanged.

### Root cause

Reproduced locally with the `vm_compare.py` methodology (callgrind, marginal Ir per op, N = 4,000
vs 8,000; same toolchain as Linode, which reported the same 382 / 504): base 382.3, `8975e8eb`
504.3. `callgrind_annotate` per function (inclusive and exclusive, marginal per op): the whole
+122.0 is in four functions, and nothing below them changed (`array_push` 100.4 inclusive in both,
with `heap_retally` 39, `unshare` 29, `_M_realloc_insert` 8.4):

| Function (exclusive Ir per op) | base | `8975e8eb` | delta |
| --- | --- | --- | --- |
| `push_value` | 38 | 43 | +5 |
| `mutation::push` | - | 46 | +46 |
| `mutation::(anon)::admit` | - | 30 | +30 |
| `mutation::check_mutable` | - | 41 | +41 |
| everything else | | | 0 |

An instruction-level run (`--dump-instr=yes`, same marginal method) of those four functions,
classified instruction by instruction:

| Category | Ir per push | Where |
| --- | --- | --- |
| Protocol dispatch: three extra out-of-line layers (`push_value -> push -> admit -> check_mutable`) | 69 | prologue/epilogue of `push` (21), `admit` (23), `check_mutable` (25): six callee-saved registers and the stack-protector canary in `admit` and `check_mutable`, because the refusal text (`std::string` concatenation) is built in the same function and GCC does not shrink-wrap it |
| Call/argument set-up for those layers | 15 | `push_value -> push` 6, `push -> admit` 4, `admit -> check_mutable` 5 |
| Temporaries: the 48-byte `Outcome` (`std::string` inside) | 14 | zero-initialised in `check_mutable` (6), tested in `push` and `push_value` (4), destroyed in `push_value` (4) |
| Mutation state: the `Env` | 10 | built in `push_value` on every push (only a refusal reads it) |
| Validation: repeated kind / ownership checks | 9 | `admit` re-tests the kind `push_value` had just tested (2); `is_immutable` re-decodes `is_heap` for a value already known to be an array (7) |
| Validation: the `kImmutable` bit itself | 3 | load, `testb`, branch (refcount test only when the bit is set) |
| Validation: bounds, cycle check, budget | 0 | not run for push (B1 rules) |
| Refcount operations | 0 | the item copy into `array_push`'s by-value parameter moved from `push_value` to `push` (+1 register move) |
| Allocations | 0 | |
| Commit and bookkeeping (`note_store`, `heap_retally`, `relocate_storage`, `kStorageLent`) | 0 | `array_push` unchanged |
| Register allocation noise | 2 | |
| **Total** | **122** | |

So the regression is call structure, not validation: 84 Ir of frames and calls, 24 of temporaries,
9 of duplicated checks, against 3 Ir for the check B1 actually adds. The same pattern (a heavy
frame on an out-of-line stage function, `Outcome` round trips, an `Env` and a `natives::Context`
built per call) costs the other mutators too; the microbenchmarks below (new `mutation` group)
measure them.

### The change (code commit `38de7d10`)

No fast path and no second implementation: every mutation still goes through the same protocol
functions, stages and order; only where code lives and what the success path builds changed.

- `mutation.h`: the stage tests the success path needs are inline (`admits` = kind compare +
  `refuses_mutation`; `check_mutable`, `array_reserve_bytes`, `reserve`), and every refusal is
  built by an out-of-line `cold` function (`detail::immutable_failure`, `admit_failure`,
  `bounds_failure`; in `mutation.cpp` `failed`, `failed_text`, `cycle_failure`, `budget_failure`,
  `collection_admit_failure`, `no_field_failure`). A mutation that passes validation builds no text
  and needs no frame for one.
- `push` and `set` are inline in `mutation.h` and take the receiver text as a string view or a
  callable, read only by a refusal (the VM passes its note lookup), so no `Env` is built on their
  success path. The `Env` forms remain the general entry points.
- `commit_array` is inline (always): each `ArrayMutate` case passes a constant edit and compiles to
  that edit alone, as the instruction's own commit did before B1. Only the operand copy for
  `Insert` is now moved into the vector (the copy `insert(const&)` makes internally anyway).
- `ArrayMutate`: push is dispatched before the stage 2.2 test (`mutation::push` runs it, same
  text), so push is no longer checked twice; the array size is read after that test, so the
  compiler keeps the kind proof; the `natives::Context` is built only for `sort`, its only user.
- `KeyedMutate`: each `Outcome` is constructed in place (no default construction plus move
  assignment) and the `Env`, with its note lookup, is built only by a mutator, not by the reads.
- Stage 2.4: `closes` tests `may_contain_identity(stored)` before calling `closes_cycle`. For a
  struct or collection receiver (the only ones `cycle_applies` admits) `closes_cycle` is
  `would_close_cycle`, whose first test is exactly that summary, so the answer is the same; a
  scalar or plain value makes no call.
- Stage 2.5: `reserve_slow` checks the budget with `natives::reserve_within(heap_limit, bytes)`,
  the body of `reserve_result` (which now calls it), instead of constructing a `natives::Context`
  (a `std::string`, a `shared_ptr` and the host environment) per reservation.

Unchanged: the stage order, every refusal text and failure kind, error precedence (kind, then
`kImmutable` with the B1 refcount rule, then bounds/arguments, cycle per stored operand in operand
order, budget), the commit primitives in `aggregate*.cpp` and all bookkeeping, failure atomicity,
COW, take/put, the name-based checks and the Phase A guard.

**Survives B2.** Nothing here depends on `unshare`, COW or take/put. When B2 merges the
`ArrayMutate` path with the `mutation::` functions, both already share the inline stage tests,
`commit_array` and the cold refusals, so the merged path inherits the success-path shape. When B2
drops the B1 refcount condition from stage 2.2, `refuses_mutation` loses one compare. When B2
makes the commit primitives private to `mutation.cpp`, the inline `push`/`set`/`commit_array` move
with them. B2's planned deletion of `unshare` also removes its 29 Ir call from every push.

Size (production, +300/-184): `mutation.h` +132/-8, `mutation.cpp` +107/-140, `vm_ops.cpp`
+25/-12, `vm_keyed.cpp` +18/-13, `vm_array_mutation.cpp` +8/-8, `natives/support.{h,cpp}`
+10/-3. Tests and tools: `vm_compare.py` +29/-1 (the `mutation` group), `Makefile` +3/-2
(mutants 4 and 5).

### Mutants (`test-rt-mutation-protocol-mutants`, each must exit 1)

| Mutant | Change | Caught by |
| --- | --- | --- |
| M-B1a | `push` commits before it validates (now inline in `mutation.h`) | immutable push: receiver changed |
| M-B1b | `object_put` skips the immutable check | immutable object_put |
| M-B1c | an array edit's bookkeeping skips `heap_retally` (now inline `commit_array`) | growth tally check |
| M-B1d (new) | `admits` drops the inline stage 2.2 test | immutable push, set, insert, ... (each refusal case: failure 0 or 3 instead of 2) |
| M-B1e (new) | the hoisted cycle pre-filter only considers arrays and objects | map_put-key, set_insert (a closure over the receiver is stored, no Cycle) |

The mutant-bearing header functions are always inlined, so the mutant define (which reaches only
`mutation.cpp` and the unit test) cannot be overridden by a non-mutant out-of-line copy at link
time.

### Performance (local callgrind, VM instructions per op; same toolchain for all three)

| Workload | base `d209febd` | `8975e8eb` | B1-perf | vs base |
| --- | --- | --- | --- | --- |
| `ops/array_push` (Linode gate) | 382 | 504 | 384 | +2 (+0.5 %) |
| `mutation/array_set` (`a[1] = i`) | 388 | 576 | 413 | +25 |
| `mutation/array_push_pop` | 1,440 | 1,729 | 1,422 | -18 |
| `mutation/array_insert_pop` | 2,462 | 2,778 | 2,421 | -41 |
| `mutation/map_set` | 1,788 | 1,725 | 1,472 | -316 |
| `mutation/set_add` | 1,366 | 1,546 | 1,341 | -25 |
| `mutation/stack_push_pop` | 2,240 | 2,404 | 2,123 | -117 |
| other `ops` (loop, add_local, call, array_index, object_member, string_concat, if_branch) | identical | identical | identical | 0 |

Every mutation workload is at or below the base except two, both explained:

- `array_push` +2 (384.3 vs 382.3; the gate asked for <= 382). Instruction by instruction the
  B1-perf `push_value` is the base `push_value` plus the stage 2.2 test B1 adds (load of the heap
  pointer, `testb $0x20`, branch, and one register copy: 4), minus one callee-saved register pair
  and one jump (3), plus one register move the stack-protector setup forces (1). Nothing of the
  122-Ir protocol structure remains; the 2 Ir are the new check itself. Getting under 382 would
  need compensation outside B1's code (`unshare`'s missing shrink-wrap costs 29 Ir per push; B2
  deletes `unshare`), or a change of the `Outcome` return convention; neither was made. Inlining
  `push_value` into `Vm::cold` was measured and rejected (400). **Owner decision needed.**
- `array_set` +25 (413 vs 388; was 576). B1 routed element stores through the commit primitive
  `array_set` (out of line, by-value item, precise `note_store`) instead of the base's inline
  `array_mut(...)[i] = v`, which marks the identity summary unconditionally. Matching the base
  needs either inlining `array_set` (it uses `aggregate_store.h` internals and the R19 mutant's
  `relocation_due`, so not done in B1) or reverting to the conservative mark. B2's move of the
  commit primitives into `mutation.cpp` is the natural place. Not in the Linode `ops` gate.

`bench-rt-value` (callgrind Ir; calls the commit primitives directly): copy 4,694,315; push
13,331,799; index 4,000,214; object set 13,759,136; object get 5,121,141; guard PASS (B1:
4,694,315; 13,331,799; 4,000,200; 13,759,108; 5,121,113; the +14/+28 are fixed start-up cost).
`bench-rt-value-cycle` (wall clock, shared host): store_plain 0.58, store_callable 14.70,
store_handle 9.14, store_nested_identity 34.93 ns; all within budget, PASS.

### Correctness evidence

Code commit `38de7d10`; every step alone through the heavy-job gate, exit codes:

| Gate | Exit |
| --- | --- |
| `make nift`; `test-rt-lint` (self-test 8 fixtures; `src/rt` 0 problems) | 0, 0 |
| `test-rt-mutation-protocol` (79 checks, 60 refusal cases); `-mutants` (M-B1a-e, 5 of 5 caught) | 0 |
| `test-runtime2-refsem` (463 checks); `-refsem-mutants` (22 caught) | 0, 0 |
| `test-runtime2-collections` (273); `-collections-mutants` (11 caught) | 0, 0 |
| `test-runtime2-safety`; `-safety-mutants` (7 caught) | 0, 0 |
| `test-runtime2-vm-default`, `-cp14b`, `-cp14b-design-mutants` (1-11 detected, 12 unverified comparator profile as before), `-args-cmd`, `-args-cmd-mutants` | 0 each |
| `test-runtime2-packages`, `-packages-mutants`, `-packages-legacy`; `-coro-module` | 0 each |
| `test-cp19-functional`, `test-cp19-session-cli`, `test-runtime2-cp22`, `test-runtime2-cp23` | 0 each |
| `test-runtime2-refsem-sanitize` (ASan/UBSan, 1,233 s), `test-runtime2-collections-sanitize` | 0, 0 |
| `make test-rt-vm` (every prerequisite and recipe line, 4,127 s) | 0 |

Test expectations changed: none. Test infrastructure: two new mutants (`Makefile`
`RT_MUTATION_MUTANTS`), the `mutation` group in `vm_compare.py`.

**Classify diff** against `d209febd` (the base binary and outputs recorded for B1):
`NIFT_VM=classify` over every `.f` in `tests/runtime2` (1,816 units): 0 changes; CP25-R
`classify-all` over the release corpus (589 units; outcome, reason, line, column, reasons, code;
temporary paths normalised): **0 changes**.

Not run locally: TSan lanes, the Linode workloads. The Linode `bench-rt-vm` rerun (which now
includes the `mutation` group) is the authoritative number.
