# Weekend candidate: map, set and stack values on the VM (cand/collections)

Status: **candidate, frozen for independent review** (not reviewed, not
integrated). Branch `cand/collections`, base local `weekend/r2` @`10c41015`.
Decision: **D-063** (`../decisions.md`). Contract tests:
`tests/runtime2/collections/` (`make test-runtime2-collections`).

Scope: VM support for legacy's `map()`, `set()` and `stack()` collections and
for `same()`, with **value semantics** (owner ruling, an intentional departure
from legacy's reference semantics). `queue()`, `prique()`, `sorted_map()` and
`sorted_set()` stay refused by name (`callable:<name>`); they have no corpus
hits. Out of scope: CP24 templates, persistent sessions, packages/modules,
`cmd()`/`run()`.

## 1. Legacy behaviour (`NIFT_VM=0`)

Source: `src/ParserExpression.cpp` ("v4.3 collection constructors", the
collection-method block, `structural_equal`, `serialize_value`),
`src/ParserTemplate.cpp` (`@for` over collections), `src/Parser.h`
(`CollectionInstance`). Probes: `scratchpad/collections/p/*.f` and the release
study `release-plan/proposals/collections-coro-module.md` §A1 (re-verified).

| Area | Legacy |
| --- | --- |
| Representation | A per-Parser table entry; the script value is the tagged string `"\x1fnift:collection:<id>"`, so a collection's binding type is **string** (`m := "a"; m = map()` is accepted, `m := map(); m = 5` is "cannot assign int to string binding 'm'") |
| Constructors | Exactly the text `map()`, `set()`, `stack()` (`map(1)`: "undefined callable: map") |
| Copying | **References everywhere**: `b := a`, arguments, returns, `arr[0]`, `o["m"]`, loop bindings all alias; only `copy()`/`deepcopy()` make an independent instance; values read *out* (`m.get(k)`) are copies |
| Receivers | Methods only on a bare binding (`o["m"].size()`, `f().get(1)`: "unknown value or malformed expression") |
| All kinds | `size()`, `empty()`, `clear()` (returns null); `stringify()`/`prettify()`; `type()` is `"collection"`; `is_collection()`; callback methods `map/filter/reduce/any/all/find/count/sort_by/group_by` |
| map | `set(k, v)` returns `v`, keeps an existing key's position; `get(k)`; `contains(k)`; `remove(k)` returns the value |
| set | `add(v)` returns `v` (no-op if present); `contains(v)`; `remove(v)` returns `v` (a missing member is a no-op) |
| stack | `push(v)` returns `v`; `pop()`/`top()` the last pushed; `front()` the first; `contains(v)` by structural equality |
| Keys / members | bool, number, string, **and every tagged string** (collections compare structurally, callables/structs/handles by identity); `1` and `1.0` are one key, `"1"`, `1` and `true` are distinct; each NaN is a new key |
| Iteration | `for ((k, v) : m)` with typed keys, insertion order; `for (x : s)` insertion order; `for (x : st)` **top first**; a set or stack is materialised as an array first, a map is iterated over its live entry vector; `for (x : m)`: "map @for syntax is @for((key, val) : map){...}"; pair form over a set/stack: "array @for syntax is @for(item : array){...}" |
| Equality `==` | Kinds must match; stack element-wise in order; set: size + membership; map: size + an equal entry for each; prique by identity; `stack() == []` false |
| Printing | `print(m)`: "print: value is not directly renderable"; `stringify` gives `map([["b",2],[1,"one"]])`, `set([...])`, `stack([...])` (bottom first); `prettify` one entry per line, `[k, v]` with keys/values at the next depth; `"x" + m`: "string concatenation requires renderable scalar values"; `"$[m]"` renders the internal tag `nift:collection:1` (a defect) |
| Errors (fatal, not catchable by `try`) | "get: key not found", "remove: key not found", "pop/top/front: collection is empty", "add/contains/remove: set values must be bool, number, or string", "map: key must be bool, number, or string", arity texts ("set: expected key and value", "get: expected one key", ...), "cannot mutate const collection: m", "set/push would create a cyclic reference"; inside a return expression the error is prefixed `return: ` at the return statement |
| Threads | Cannot cross threads: "thread: argument contains a non-transferable resource"; a captured global collection fails in the worker |
| `same(a, b)` | Location references / sharing array bindings compare storage (`a := [1]; b := a; same(a, b)` true); tagged strings (struct, callable, collection, file, stream) by identity; timers by value; anything else (mutexes included) the fatal "same: operands must be identity-bearing values" |

## 2. Contract (D-063)

1. **Values.** A map, set or stack is an ordinary value (D-040). Assignment,
   declaration, user-function arguments, returns, values read through a
   closure, insertion into arrays/objects/other collections, struct fields,
   `for` loop values and thread arguments/returns/captures all give an
   **independent** value. Copying is **deep** in effect: no mutation of one
   copy, at any depth, is ever visible through another. Implemented with
   copy-on-write (a copy shares the object; the first mutation of a shared
   object clones it, one level at a time), never observable.
2. **Self-insertion snapshots.** `m.set(k, m)`, `k.push(k)`, `s.add(s)` store
   the value the receiver had *before* the call; ordinary values stay acyclic
   (D-040 addendum). Legacy's "would create a cyclic reference" error does not
   occur (approved difference).
3. **Closures** capture bindings, exactly as for arrays (D-038 lexical cells):
   a closure sees later assignments and mutations of the captured binding;
   every value it *returns* or passes on is a copy. A collection that would
   contain a closure capturing the collection's own cell is the existing
   `runtime.reference_cycle` error (as for arrays).
4. **Identity (`same`).** Collections, arrays, objects and structs are
   ordinary values and have no identity: `same()` on them is legacy's error
   text "same: operands must be identity-bearing values" (D-040 #4; approved
   difference where legacy answers true/false). Callables compare by identity
   (legacy-equal). File/stream handles compare by identity. `same()` on a
   mutex, atomic, channel, future or thread is **refused before execution**
   (`same:shared-handle`): legacy rejects them, `rt::same` would compare them.
5. **Everything else is legacy's:** methods, return values, key rules,
   insertion order, stack order, equality, `type`, `stringify`/`prettify`,
   iteration order and error forms, texts, positions and (un)catchability.
   Collection keys/members compare structurally, callables by identity.
6. **Bounded subset.** Whatever is not modelled is refused before any effect
   (section 3), never reinterpreted.

### Value-kind slot

`rt::Kind::Collection = 15` takes the **last free value of the 4-bit kind
tag** (`HeapObject::tag` low bits; `Future = 14` was the previous last).
A further kind needs the tag mask widened (bits 4-6 of the tag byte are
unused; bit 7 is the D-040 identity summary). `value.h` static_asserts this.
The map/set/stack sub-kind lives in the object (`CollectionObject::sub`), so
one tag covers all three (and any later collection kind). No `Value`
redesign: `Value` stays 16 bytes, `HeapObject` 8.

## 3. What stays refused (before execution)

| Shape | Reason | Why |
| --- | --- | --- |
| `queue()`, `prique()`, `sorted_map()`, `sorted_set()` | `callable:<name>` | not in this slice; no corpus hits |
| `map(1)` | `builtin-arity:map` | legacy: "undefined callable" |
| `const m := map()` | `declare-const` | const collections not modelled (legacy's const-check order) |
| a mutator through a `for` loop binding | `collection-loop-binding` | legacy aliases the iterated element |
| callback methods `m.map(...)`, `s.filter(...)`, ... | `collection-callback:<name>` | not in this slice; no corpus hits |
| a method on a non-binding receiver `a[0].size()`, `f().get(1)` | `method:<name>` | legacy rejects it ("malformed expression") |
| a receiver that may be several kinds (`c := map(); c = stack()` on one path) | `collection-receiver:<name>` | no static receiver |
| a struct, plain object, handle or task as a key/member | `collection-identity-key` | legacy compares tagged structs/handles by identity |
| `same()` on shared/task handles | `same:shared-handle` | see 2.4 |
| `"$[m]"`, `"x" + m` | `interpolation:unknown-type`, `type:+` | legacy renders a tag / errors |
| a collection as an argument of a built-in other than `type`/`is_*`/`copy`/`deepcopy`/`same` (`html_escape(m)`, `[m].contains(m)`) | `collection-argument:<name>` | legacy renders/compares the tag string |
| `contains/indexOf/join/unique/sum/min/max/from_entries/merge_deep` on an array/object that may hold a collection | `collection-element:<name>` | idem |
| arity errors in loop code | `builtin-arity:<name>` | legacy's loop-evaluator report not established (outside loops the legacy fatal error is compiled) |
| `m.length()` | `method:length` | legacy: malformed expression |
| piped/tty session statements over collections | `session:binding-shape-contract` | persistent sessions out of scope (D-054 shapes) |
| `nift eval 'map()'` (a collection as the eval result) | error "eval: collection values have no legacy representation", exit 2 | legacy prints the tag string |

## 4. Implementation

Value layer (shared code): `value.h` (kind), `heap.h` (`CollectionObject`:
items flattened `k0, v0, k1, v1, ...` for a map, members for a set, elements
bottom-to-top for a stack; an open-addressing key index from 8 entries,
hashed with `rt::hash`, confirmed with `rt::equal`), `aggregate.{h,cpp}`
(`make_collection`, `map_set`, `set_add`, `collection_erase`, `stack_push`,
`stack_pop`, `collection_clear`, the `unshare` clone, relocation under
checked builds, the identity summary bit on stores), `value.cpp` (destroy,
children for the non-recursive destructor), `heap_account.cpp` (footprint:
object + items + index), `compare.cpp` (legacy's equality per kind; hash =
kind and size, order-independent), `value_cycle.cpp`, `transfer_heap.cpp`,
`isolate_transfer*` (`WireKind::Collection`: threads carry collections as
copies), `vm_ops.cpp` (binding tag 4, legacy's "string").

Compiler/VM: kind bits 20-22 (`kKindMap/Set/Stack`, outside `kKindAll`;
`top_ty` includes them so a collection never hides behind "any"); inference
tracks map keys (`Ty::key`), map values / set members / stack elements
(`Ty::elem`) per binding (sound because copies never share); the
constructors and `same` are built-ins (`builtin_list.inc`); read-only methods
are natives (`natives/collection_methods.cpp`); mutators and fallible reads
are lowered onto the bare binding (`compiler_keyed.cpp`: arguments first, then
take / `KeyedMutate` / put, the array-mutation pattern; `vm_keyed.cpp`), so
errors get the collection error context (`return: ` prefix and position);
the budget is reserved before growth and before a clone. `for` loops take a
snapshot at the header: a set/stack becomes legacy's materialised array
(stack top first), a map is read through `ObjectKeys`/`ObjectLookup` with its
typed keys (`compiler_loops.cpp`, `infer_for.cpp`, `vm_iter.cpp`). Struct
fields may be initialised with `map()`, `set()` or `stack()` (an empty
constant; copies never share).

## 5. Corpus (CP25-R inventory)

`make cp25r-inventory` on this candidate (589 shared units classify-all,
608 execute entries): `callable:map`, `callable:set`, `callable:stack` and
`callable:same` are **no longer seen**; no new D-063 reason is seen on the
corpora (their rows document the refusals).

| Unit | Before (TSV, base) | Now |
| --- | --- | --- |
| probes `map-set-2000/4000/8000`, `official-map-set` | `callable:map`/`set` | **VM**, output = legacy |
| probes `bfs-map-update-2000`, `bfs-set-visit-2000` | `callable:map` / `callable:set` | **VM**, output = legacy |
| `semantics/equality.f` | `callable:map` | **VM**, output = legacy |
| smoke `v44_perf_scaling_smoke` set40/set80/map40/map80 | `callable:set`/`map` | **VM** (`tests/v44_perf_scaling_smoke.sh` passes on the VM) |
| smoke `v48_map_set_scaling_guard` 001-006 | `callable:map`/`set` | **VM** (`tests/v48_map_set_scaling_guard.sh` passes on the VM) |
| for-s2 `g1_map`, `g1_set`, `g1_stack` | `fallback:callable:*` | **VM**, legacy-equal (EXPECT updated) |
| probes `bfs-2000/4000/8000` | `callable:set` (masking `aliasing`) | refused: `aliasing` (array `adj.push(row)`, pre-existing rule) |
| probes `frequency-2000/4000/8000`, `official-frequency-count` | `callable:map` | refused: `postfix-spelling` (`(i % 37).to_string()`) |
| smoke `v46_b4_cp3_recoverable_errors/001-rollback` | `callable:stack` | refused: `statement:Export` |
| smoke `v44_root_path_corruption_reproducers/004-mapstruct` | `callable:map`, `callable:same` | refused: `struct-construction` / `struct-value` (`same()` there is an approved D-040 #4 difference once structs land) |
| smoke `v46_timer_smoke/003-collection-transfer` | `callable:timer` | refused: `callable:timer` (unchanged) |
| smoke `v44_cp9_map_markup_smoke/001-test` | `opaque:template-text` | unchanged (template text, CP24) |

`method:contains` / `method:get` keep one classify-all hit each on other
receivers (a template-text unit; a mutex `get` classify-all artefact whose real
run is on the VM); rows updated. **Pre-existing, not from this candidate:**
`--check` reports six unclassified reasons on
`rt-diff-corpus/async/fileops/main.f` (`coro-blocking-host:file`,
`coro-blocking-host:rm`, `method:open/write/save/close`); the base binary
classifies that unit identically, so the inventory check was already failing
there.

## 6. Tests

`make test-runtime2-collections` (`tests/runtime2/weekend_collections.py`):

* `parity/` (32): legacy and VM give identical stdout, stderr and exit status,
  and the VM ran the program: every method and return value, typed keys,
  indexed maps/sets (300 entries, removal, `1 == 1.0`), collection and callable
  keys, equality, iteration (order, mutation during the loop, loop metadata,
  through a function), nesting read-only, every error text/position (in
  `try`, in a return expression, in loop code, argument faults), `print`,
  binding types, struct-field initialisers, closure reads, `same()` on
  callables, the frequency/BFS probe shapes.
* `approved/` (14): the value-semantics differences, each with the VM golden
  (`.vm`) and the legacy witness (`.legacy`), which must differ: assignment,
  arguments, returns, closures, container insertion and extraction, deep
  nesting (stack in map in map in array), self- and mutual insertion, struct
  fields, thread arguments/returns/captures, `same()` on collections.
* `refuse/` (23): refused before any effect (every fixture prints first;
  exit 1; "no part of it ran"; exact reason), section 3, plus the owner's two
  array cases (see 7).
* heap: budget stops before growth for map/set/stack and for the clone of a
  shared collection (a same-size unshared map fits the same budget), exact
  peak, live heap at stop equal to a control program after collections were
  filled, copied, mutated and dropped.

Mutation controls, `make test-runtime2-collections-mutants`
(`tests/runtime2/collections/mutants.sh`, one translation unit rebuilt per
mutant): each must fail the suite.

| # | Defect | Caught by |
| --- | --- | --- |
| 1 | a shared collection is mutated without a clone (observable COW) | 21 checks |
| 2 | an assignment/argument copy aliases (refcount 2 mutated in place) | 12 |
| 3 | the binding is taken before the arguments run (no self-snapshot) | 2 |
| 4 | the mutated collection is never put back | 55 |
| 5 | a map insert is never counted on the heap | 3 |
| 6 | map equality depends on entry order | 2 |
| 7 | a stack iterates bottom first | 2 |
| 8 | `1.0` does not hash like `1` (indexed lookups miss) | 2 |
| 9 | a transferred collection loses its kind | 3 |
| 10 | `same()` exposes the storage identity of collections | 5 |

A "shallow copy" mutant has no separate form here: with copy-on-write and
mutation only through unsharing entry points a shallow clone *is* the deep
copy; the defects that make a copy observably shallow are 1 and 2.

Worker lanes added: `test-runtime2-collections-sanitize` (ASan+UBSan) and
`test-runtime2-collections-tsan` (thread transfer), both under
`test-rt-vm-sanitize`; the suite and the mutants are under `test-rt-vm`.

## 7. Open questions for the owner

1. **The owner's array cases.** `a := [[1,2],[3,4]]; b := a; b[0].push(5)` is
   not valid Nift in either engine (legacy: "unknown value or malformed
   expression: b[0].push(5)"; VM: refused, `opaque:legacy-runtime-error`);
   `a := [1,2]; a.push(a)` gives `[1,2,[1,2]]` in legacy and is refused by the
   VM (`aliasing`, the existing array admission rule). Both are recorded as
   refusals before effect. The collection equivalents (nested extraction,
   self-insertion) run with value semantics. Should the VM's array admission
   rules (pre-existing, outside this slice) now also admit array aliasing
   shapes with D-040 value semantics as an approved break?
2. **`same()` on mutex/atomic/channel/future/thread** is refused here. D-040
   #4 lists handles as identity-bearing; legacy errors. Admit with identity
   (an approved difference) or keep refused?
3. **Map iteration under mutation**: legacy iterates the live entry vector
   (undefined if the body inserts and the vector reallocates); the VM iterates
   the header snapshot. Recorded as part of the value-semantics break.
4. **Collection keys** compare structurally (legacy's rule too), as snapshots.
   Legacy re-reads an aliased key collection; a key mutated after insertion
   cannot happen here.
