# L7 / D-040 step 2 — implementation record

Status: **steps 2.1 and 2.7a implemented (value layer). Integration is
pending the remote gates.** The cost of the summary bit is accepted
provisionally. Branch `l7-value-layer` (local), base `origin/runtime-2`
@`c385d396`.

* Specification: `../semantics.md` "Value semantics (D-040)" V1–V19.
* Step-1 record: `l7-value-semantics-step1.md` (plan §6, cycle rule §2,
  implementation plan §6.1).

**Scope** is limited to the value-layer files owned by this work:

* `src/rt/value.h`, `heap.h`, `heap_account.{h,cpp}`, `aggregate.{h,cpp}`,
  `compare.{h,cpp}`;
* new: `value_cycle.{h,cpp}`;
* tests in `tests/rt/`, and Makefile targets.

**Not changed:** the VM compiler and VM (2.3, after CP13/CP15 integrate),
legacy (2.4/2.5) and natives. Before each file was edited, `git diff
origin/runtime-2...<branch>` was checked for spec-integration-cp15,
runtime-2-cp12b-dev, runtime-2-cp17-dev and feature-host-deps, together with
their worktrees. None of them touches these files.

**Separate records.** Defects C1 and C3 of the memory investigation are
existing runtime defects, not L7. They get their own commits, tests and
section (§6) once the investigation's final report arrives.

## 1. What changed

| Area | Change | Spec |
| --- | --- | --- |
| Structs | Copy-on-write values: `make_struct`, `struct_add_field`, `struct_set`, `struct_get`, `struct_fields`, `struct_type`; `unshare` clones structs | V9 |
| Equality and hash | Structs compare structurally: the same declaration type and every field, private ones included, in declaration order. A struct never equals an object. The hash follows the same rule | V9, V13 |
| `same()` | `Sameness::{Same, Different, NotIdentity}`. Identity applies only to callables and handles; ordinary values give `NotIdentity` (the caller raises `runtime.type_error`) | V18 |
| Place mutation | `array_slot_mut`, `object_slot_mut`, `struct_slot_mut` make each level unique top-down; `array_set` writes one slot | V2 |
| Retain before unique | Every store owns the stored value before the receiver is made unique. Self-insertion therefore stores the pre-write snapshot: `a.push(a)` gives `[1,[1]]`. Checked builds (`NIFT_RT_CHECKED_REFS` or ASan) abort if an object would contain itself directly | V16 |
| Identity summary bit | Bit 7 of the header's tag byte, whose low 4 bits hold the Kind (all 13 kinds fit; bits 4–6 are unused). It means "may contain a callable or a handle". See §2 | V16 |
| Cycle checker | `would_close_cycle(target, stored, graph)`. The inline O(1) gate is `may_contain_identity`. The walk is iterative and visits only closures, handles, captured variables, payloads and marked aggregates, using reused thread-local buffers. `NIFT_RT_NO_CYCLE_CHECK` compiles it out for measurement. Hooks (`CycleGraph::captured`, `payload`) are where the VM's closure cells (CP12C) and the CP17/CP22 payloads plug in | V16 |
| Accounting | The 1 GiB per-object field is kept; objects of 1 GiB or more are counted exactly in a side table. See §3 | concern 3 |

## 2. The identity summary bit (copy-on-write metadata)

* **Where it is stored.** The first version took bit 7 of the accounting
  counter, which cut the per-object accounting range from 1 GiB to 512 MiB.
  The owner rejected that. The bit now lives in the header's tag byte,
  formerly the full `Kind kind` byte; the Kind uses 4 bits. The header stays
  8 bytes, the accounting range is the original 1 GiB, and no storage is
  added. `HeapObject::kind` became the accessor `kind()`; every use is in
  the owned files.
* **Behaviour chosen: conservative, never missing.**
  * The bit is set by any store that may add a callable or a handle:
    `array_push`, `array_set`, `object_set` and `struct_*` when the stored
    value is a callable, a handle or a marked aggregate.
  * It is set unconditionally by raw access: `array_mut` and the slot
    helpers.
  * Copy-on-write clones copy it.
  * It is never cleared by a removal. Removing the last callable or handle
    leaves it set.
  * Only the cycle checker clears it, after verifying that no direct child
    is identity-bearing or marked.
  * It can therefore be set without need, but never missing while a callable
    or handle is present.
* **Tests** (`value_d040` §7):
  * `a := [1]; b := a; a.push(fn)`: `b` keeps its contents and its clear
    bit;
  * a cycle-producing store involving `a` is refused and leaves `a` and `b`
    unchanged;
  * after the last handle is removed the bit stays set, the checker then
    clears it, and it is set again when a function is added;
  * a clone that keeps a function keeps the bit;
  * soundness over random programs (§4).

## 2b. Audit: no reader sees the summary bit as part of the kind

The bit is stored only in `HeapObject::tag`. Each Value keeps its own kind
byte (`Value::bytes_[15]`), and that byte never carries the bit.

| Reader | What it reads | Masked |
| --- | --- | --- |
| `HeapObject::kind()` (`value.h`) | `tag & kKindMask` | yes: the only kind accessor on the header |
| `may_hold_identity`, `mark_…`, `clear_…` (`heap.h`) | bit 7 only | n/a |
| `Value::adopt`, `Value::replace_heap` (`value.h`) | `object->kind()`, written into the Value's kind byte | yes. These are the only paths from the header to a Value. A clone gets the bit before `replace_heap`, so this matters |
| `destroy_heap_object`: `detach_children`, `delete_object`, `has_children` (`value.cpp`) | `object->kind()` | yes |
| `relocate_storage` (`aggregate.cpp`) | `object->kind()` | yes |
| `raw_footprint` (`heap_account.cpp`) | `object->kind()` | yes |
| kind checks, `compare`/`equal`/`hash`/`truthy` (`compare.cpp`) | `Value::kind()` | not affected: the Value's kind byte |
| rendering (`render.cpp`), `stringify`/`serialize` (`natives/format_methods.cpp`), every other native | `Value::kind()` | not affected |
| `legacy_bridge.cpp` (`to_legacy`/`from_legacy`) | `Value::kind()` | not affected |
| VM dispatch (`vm.cpp`, `vm_ops.cpp`), compiler, script runner | `Value::kind()` | not affected |
| C ABI and embedding (`include/nift/*`, `src/Engine*`) | no `rt::Value` or header access (legacy `RuntimeValue` only) | not affected |
| `Value::raw()` | no callers outside `value.h` | n/a |

No file outside the owned value layer reads `HeapObject::tag` or the former
`HeapObject::kind` member (grep across `src/` and `include/`).

**Regression test** (`value_d040` §8). The bit is set on a heap value of
every kind: heap string, decimal, bytes, array, object, struct, error and
function. Against an unmarked twin, the test checks that these all match:

* the Value kind and the header `kind()`;
* `equal` in both directions, and `hash`;
* `truthy` and `append_rendered`;
* `serialize` (stringify);
* `to_legacy` plus `dump`;
* `heap_footprint` and `kind_name`.

It also checks that:

* cloning a marked array, object or struct gives the right kind and keeps
  the bit;
* `adopt` of an object whose bit is already set gives the right kind;
* destroying every marked kind is clean under ASan/UBSan.

The mutant "unmasked `kind()`" crashes the suite (SIGSEGV), so it is caught.

## 3. Heap accounting and budgets (memory investigation concern 3)

**Requirement.** An object larger than the representable footprint must
never silently get past the configured heap budget, and there is no silent
saturation at 512 MiB or 1 GiB.

**Implementation.**

* The header records up to 2^24 − 1 granules (1 GiB − 64 bytes), as before
  L7.
* A footprint that reaches that limit is recorded as the sentinel, and its
  exact byte count is kept in a thread-local side table keyed by object.
* `heap_retally` and `heap_untally` move objects in and out of the table as
  they grow and shrink, so `live_heap_bytes()` always equals the sum of the
  exact footprints, which is what the VM heap budget reads.
* The side table holds only objects of 1 GiB or more.
* The common path stays branch-light: the slow path is kept out of line, and
  `raw_footprint` is inlined into `heap_retally`.

This also closes a pre-L7 gap. A single object above 1 GiB used to count
1 GiB − 64 bytes, so under a budget above 1 GiB it could pass unseen.

**Tests.**

* **Unit** (`value_d040` §6, run under `ulimit -v 4000000`; reserved, not
  touched):
  * single objects at and just above the 512 MiB and 1 GiB boundaries
    (512 MiB − 64, 512 MiB, +1, 1 GiB − 256, 1 GiB − 64, 1 GiB, +1, +64,
    1.5 GiB), each counted exactly;
  * three large objects at once, counted as their sum;
  * transitions above → below, below → above, and above → above, with the
    side-table population checked (0 after leaving, 2 while two are above,
    0 after destruction);
  * counter arithmetic: shrinking, idempotent re-tally, and an exact return
    to the base after destruction.
* **VM** (`tests/runtime2/heap_saturation.sh`, `make
  test-rt-heap-saturation`; up to ~2.5 GiB RSS, so workers only, under
  `ulimit -v 3800000` and `NIFT_VM=require`):
  * a 512 MiB string within a 700 MiB budget runs;
  * 1 GiB under 700 MiB is refused with "memory budget exceeded";
  * 1 GiB within 1088 MiB runs;
  * growth past 1 GiB (capacity 2 GiB) under 1088 MiB is refused. A count
    saturating at the field would let this through.
* **Mutation checks:**
  * dropping the exact count: 8 failures;
  * dropping the table erase on shrink: 3;
  * dropping the erase on destruction: 1.

## 4. Tests

`tests/rt/value_d040.cpp` is part of `test-rt-value` and
`test-rt-value-sanitize`. Default seed 20261020: 111,961 checks, 0
failures, at `-O1` and under ASan/UBSan. It covers:

1. structs: copies, nested place writes, structural equality and hash, other
   types, struct vs object, private fields, self-store;
2. nested copy-on-write against a deep-copy model. These are the owner's
   cases: an object holding arrays of objects and a struct, with four
   copies, deep writes through each copy in turn, pushes, struct writes,
   repeated writes and self-stores. After every step each copy equals its
   model and no value contains itself;
3. self-insertion: arrays (`[1,[1]]`, then independent later writes;
   repeated five times), objects and structs two levels deep, and
   self-insertion through a share another slot still holds. Ordinary
   self-stores never reach the checker;
4. `same()`;
5. summary-bit soundness over random programs, precision for API stores of
   plain data, and conservatism for raw access;
6. the cycle checker against brute-force reachability, over 400 random
   graphs of captured cells, closures, handles and payloads (1,161 stores
   refused, 8,839 allowed). Plain stores never walk, and a conservatively
   marked plain array is verified once, then skipped;
7. large-object accounting (§3);
8. copy-on-write metadata (§2);
9. tag-byte masking on every heap kind (§2b).

Maps and sets have no VM value kind yet (CP15B). Their value-layer tests
arrive with that kind; the language-level self-insertion fixtures for maps
and sets are in the L7 suite (class W).

Existing suites still pass: `value_unit` (its struct case now asserts value
semantics), `value_regress`, `value_round2` and `value_property` (1.87M
checks). `rt-lint` is clean.

Mutation checks on the new code, every one caught:

| Mutation | Result |
| --- | --- |
| no struct copy-on-write | crash |
| identity struct equality | 1 failure |
| no summary marking | 3,494 failures |
| checker always false | 1,218 |
| checker never clears | 2 |
| `same()` accepts structs | 92 |
| no payload walk | 290 |
| no clone summary | 667 |
| marking disabled | 3,504 |

## 5. Performance

**Hot path** (`make bench-rt-value`, callgrind Ir, GCC 13.3.0 `-O2`, empty
run subtracted; base = `c385d396`):

| Workload | Base | Now | Change |
| --- | ---: | ---: | ---: |
| copy (1000 copies of a 10k-string array) | 4,654,714 | 4,654,649 | 0.0 % |
| push (100k numbers) | 13,232,191 | 13,232,126 | 0.0 % |
| index only (1M reads) | 4,000,186 | 4,000,200 | 0.0 % |
| object set (128k sets, 64 keys) | 13,375,314 | 13,759,108 | +2.9 % |
| object get only | 5,121,319 | 5,121,113 | 0.0 % |

**Trade-off (accepted by the owner).**

* Every store that can add content has to maintain the summary bit, so that
  the cycle check never traverses plain data.
* The cheapest form found is one compare for scalars and strings, because
  every kind that can carry identity sorts at or after `Kind::Array`.
* The alternative, a "verified clean" bit that every mutation resets, costs
  nothing per store. But storing plain data into a captured variable would
  then walk it once, which breaks the no-traversal condition.
* The heap-accounting change costs nothing measurable because of the
  out-of-line slow path and the inlined footprint.

**Guard and its reconciliation with the +2.9 %.**

* `bench-rt-value` compares against a baseline **locked at the step-2.1
  tip** (`tests/rt/value_bench_baseline.json`, the "Now" column above), not
  against the pre-L7 base. The tolerance stays 2 %.
* The +2.9 % on object set against `c385d396` is the owner-approved cost of
  the summary bit. It is recorded as an approved exception in the baseline
  (`approved_exceptions`, together with the pre-L7 counts under
  `pre_l7_base`), and the guard prints it on every run.
* The guard is not loosened. The same 2 % applies to every workload,
  including object set, measured from its new locked value.
* The guard's own self-test runs first in `bench-rt-value`
  (`value_bench_guard.py --self-test`). It builds tables from the baseline
  and requires:
  * every workload at +1.9 % passes;
  * each single workload at +2.1 %, object set included, fails;
  * a missing workload fails.
* Counts from another toolchain are reported, not judged. The worker's run
  (REMOTE JOB) confirms the baseline on the certification toolchain.

**Cycle-check cost, isolated from the store** (`make
bench-rt-value-cycle`). The same captured-cell stores run with the check
compiled in and compiled out, and the guard is on the difference per store.
Local numbers, best of 5:

| Store | Difference | Budget |
| --- | ---: | ---: |
| plain value | 0.97 ns | 2 ns |
| closure | 17.3 ns | 50 ns |
| handle | 11.0 ns | 40 ns |
| struct → array → closure and handle | 48.9 ns | 200 ns |

The first implementation (hash sets, `std::function`) cost 120–500 ns per
walking store. Reused flat buffers and a visitor interface brought it to
these numbers.

## 6. Memory investigation defects (C1, C2, C3, accounting follow-up)

These are existing runtime defects, recorded separately from L7 and fixed in
their own commits, each with its original reproducers kept as permanent
regression tests. Evidence: memcheck jobs (`scratchpad/rjobs/memcheck{,2}/out`,
prototypes on `memcheck-repro` 7fab21a6).

### 6.0 Harness fixes from worker run l7-value-layer-2 (commit 7576aebc)

* `test-rt-value-sanitize` failed because the job ran it under `ulimit -v`
  and ASan could not reserve its shadow memory. The target now refuses to
  start under `ulimit -v`, and the ASan run of value-d040 skips the
  address-space section (`no-large`); that section keeps running under
  `ulimit -v` in the non-ASan `test-rt-value`.
* `test-rt-heap-saturation`: the controls (512 MiB in 700 MiB, 1 GiB in
  1088 MiB) were refused because the budget counts a string's capacity and
  the 8-character seed grows to capacity 1.875 x length (libstdc++: new
  capacity = max(needed, 2 x old), starting from the 15-byte inline buffer).
  A 15-character seed keeps capacity == length, so the cases are 480 MiB in
  512 MiB (runs), 960 MiB under 512 MiB (refused), 960 MiB in 1088 MiB (runs)
  and one more byte (capacity 1920 MiB) under 1088 MiB (refused; a count
  saturating at the 1 GiB field would be 1 GiB - 64 and pass). The refusal
  cases are not loosened. The self-append's temporary copy of the operand is
  not counted; it is bounded (one operand, freed within the instruction).

### 6.1 C1: releasing a value needs no memory

* **Defect.** `destroy_heap_object` (runs inside `noexcept` `~Value` and
  `operator=`) kept dying aggregates on a `std::vector` worklist. When the
  address space was exhausted, the first `push_back` threw inside `noexcept`:
  `std::terminate`, SIGABRT, in 18 of 24 cells of the memcheck2 `ulimit -v`
  matrix, sometimes after "memory budget exceeded" had already been printed
  (the VM unwinding the script's values).
* **Fix.** A fixed in-frame buffer (32 pointers) first, then a heap spill
  vector; when the spill cannot grow, that subtree is destroyed in place by
  pointer reversal. Nothing in the release path can throw, recurse or need
  memory (`destroy_heap_object` is `noexcept`).
* **Pointer reversal, why it is correct.** Only objects whose refcount just
  reached 0 (`release_detached`) are entered; such an object is unreachable
  from anything else, so rewriting its storage is invisible.
  * Descending from `cur` into its dying child at slot `i`: the child is
    untallied, the slot is overwritten with the parent link (`park_pointer`,
    a Null-kind value, so no destructor or release ever follows it), and `i`
    is stored in `cur`'s header (refcount + the 3 `counted` bytes, 56 bits),
    which is free because `cur` is dead and already untallied.
  * Ascending: the parent link is read back from the parent's slot
    (`take_parked` leaves a plain Null), the child is deleted with its
    header's `counted` bytes cleared first (so the accounting is not charged
    with the resume index), and the scan resumes at `i + 1`.
  * Children that are still referenced elsewhere (shared copy-on-write
    children, the same object reached twice) only lose one reference, exactly
    as in the normal path; they are never entered.
  * O(objects) time, O(1) space; the tag byte is untouched.
* **Tests (permanent).** `tests/rt/value_destroy_oom.cpp` (from the
  `destroy_oom` reproducer) with a replaceable failing `operator new`, built
  twice: as is, and with a one-slot in-frame buffer (`-DNIFT_RT_DESTROY_LOCAL=1`,
  `value-destroy-oom-small`) so small graphs reach the spill and the reversal.
  Both run in `test-rt-value` and under ASan+UBSan in `test-rt-value-sanitize`.
  * Cases: leaf; flat (1,000 strings); nested (1,000); deep (a 1,000,000-level
    chain, no allocation); deep-reversal (3,000,000 levels walked by pointer
    reversal); wide (1,000 dying aggregates, spill refused); wide-partial (the
    spill grows k = 1..6 times, then fails); spines (80 mixed object / struct /
    array spines up to 20,000 deep, resuming at non-zero child indexes);
    bushy (fan-out 3, depth 9); mixed (shared children and an outside
    reference to an inner node, which survives intact); control (no failure).
    Each checks the live allocation count and live heap bytes return exactly
    to their pre-build values.
  * Property (`property [seeds]`, 3,000 seeds): random small DAGs (arrays,
    objects, structs; copy-on-write clones that share all their children;
    the same child repeated in one parent; outside references to random
    nodes). For every graph, the release is repeated with allocation failure
    injected from every index k on, and at only index k, for every k the
    release reaches. After each: survivors match the model exactly (labels,
    kinds, child order), every surviving object's refcount equals its outside
    handles plus its edges from surviving objects, live heap bytes equal the
    base plus every survivor's footprint, and after dropping the outside
    references the live allocation count is back to the base. With the
    one-slot buffer: 3,000 graphs, 8,720 releases, 3,101 injected failures.
  * `tests/runtime2/memory_c1.sh` (`make test-rt-memory-c1`, workers): the
    memcheck `c1_push.f` matrix, 12 `ulimit -v` limits x {default budget,
    budget beyond the limit}; every cell must exit 70 with its original
    reason ("memory budget exceeded" or "out of memory (std::bad_alloc)") and
    no "terminate called".
* **Cause and effect.** The same test built against the pre-fix
  `value.cpp`: 8 of the 10 cases abort with "terminate called after throwing
  an instance of 'std::bad_alloc'" (leaf and control pass, as they never use
  the worklist); with the fix, all pass. (Local, GCC 13.)
* **Mutation check** (one-slot build under ASan, 400 property seeds): ignoring
  refcounts while reversing (entering shared children: ASan
  heap-use-after-free) and dropping the spill (LeakSanitizer) are caught;
  not untallying a descended child is caught (286 failures);
  swallowing a refused spill (leak) and losing the resume index are caught by
  LeakSanitizer. Two mutants are equivalent at testable sizes and survive:
  skipping a child on resume (the parent's destructor then releases it
  through the normal path) and deleting without clearing the resume bytes
  (they are zero below 2^32 children).
* **ASan without `ulimit -v`.** The ASan+UBSan build of the test runs clean
  with `ulimit -v` unlimited (LeakSanitizer on): 92,930 checks (32-slot
  buffer) and 123,040 checks (one-slot buffer), 0 failures, ~15 s and
  ~1.1 GiB RSS each (local).
* **Performance.** See §5 for the guard run of this tip (value bench and
  release workloads).

### 6.2 Accounting follow-up (review item 4): the side table never allocates

* **Defect** (in this branch's cd56fc1f, same class as C1). The exact side
  table for objects of 1 GiB or more (§3) was a lazily created
  `std::unordered_map`; `heap_retally` (`noexcept`, called after every
  resize) inserted into it, so an exhausted heap meant `std::terminate`.
* **Fix.** A fixed thread-local array of 256 entries (linear search; only
  objects of 1 GiB or more enter it, so it is short and off the hot path).
  `counted_bytes`, `heap_retally` and `heap_untally` never allocate. If more
  than 256 such objects are live at once on a thread, the object is counted
  at the field's maximum and the accounting is marked overflowed:
  `live_heap_bytes()` then returns `SIZE_MAX`, so every budget check fails
  with "memory budget exceeded" instead of under-counting. The flag is
  sticky for the thread (`heap_accounting_overflowed()`). 256 live objects
  of 1 GiB need 256 GiB of address space, beyond any configured budget.
* **Test (permanent).** `tests/rt/heap_account_oom.cpp`, built with a
  one-slot table (`-DNIFT_RT_SATURATED_SLOTS=1`) and a failing
  `operator new`; in `test-rt-value` (reserves ~2 GiB of address space,
  never touched): a saturated object is counted exactly with every
  allocation failing (no allocation attempted); a second one overflows the
  table and the live total reads `SIZE_MAX`; releasing both with allocation
  failing neither throws nor clears the flag. Cause and effect: the first
  step built against the previous `heap_account.cpp` aborts ("terminate
  called after throwing an instance of 'std::bad_alloc'", exit 134).
* **Hot path.** `heap_retally`'s common path is unchanged (the side table is
  reached only at the field's maximum).

### 6.3 C2: interned atoms count against the heap budget

* **Defect.** The atom table (object keys, field names) grew outside the VM
  heap budget: memcheck2 ran 3,000,000 rounds of `c2_atoms2.f` (a new
  generated key per round) under a 16 MiB budget to exit 0 at 511 MB RSS.
* **Fix (intern path only).** `AtomTable::intern` charges each new entry,
  `kAtomEntryOverhead` (96 bytes: the `std::string`, the vector slot, the
  hash node and its bucket) plus the text's heap storage (beyond the
  15-character inline buffer), through `heap_charge` to the thread's live
  heap total, which every VM budget check reads. Lookups (`find`, re-interning
  an existing key) charge nothing; `intern` also keeps `names_` and `ids_` in
  step if the map cannot grow.
* **Which context pays (owner requirement).** Accounting is per thread, and
  the atom table is per thread, shared by every execution context that runs
  on it. Rule: the bytes are charged once, to the context that interns the
  atom first (the one running when the live total grows); a later context
  reusing the atom is not charged again, but it does see the earlier charge
  in the thread's total, as it sees any other memory still held on that
  thread. Nothing is released before the table itself is.
* **Owner of reclamation: CP23** (context lifetime): trimming or dropping
  the table when its context ends, and per-context totals if contexts on one
  thread must not see each other's charges. Until then the charge is the
  honest amount held.
* **Tests (permanent).** `tests/rt/atom_budget.cpp` (in `test-rt-value`):
  exact charge per new atom (short and long), none for re-interning or
  lookups, growth visible for 100,000 keys, another thread's table charged to
  that thread only. `tests/runtime2/memory_c2.sh` (`make test-rt-memory-c2`,
  with the memcheck scripts `c2_atoms2.f`, `c2_atoms3.f`, `c2_control2.f`):
  under 16 MiB, 1,000 rounds run, 1,000,000 rounds are refused with "memory
  budget exceeded", the 1,000-key control runs 3,000,000 rounds.
  `value_round2`'s accounting check now allows for the 200 atoms it interns
  (they stay charged).
* **Status.** Unit tests local; the end-to-end script runs in the worker job
  (needs a `nift` build).

### 6.4 C3: natives check the budget before allocating results

* **Defect.** `join` and `replace` built their whole result before the VM
  next compared the live total with the budget, so one call could overshoot
  a configured budget many times over (memcheck: 16x). The §3 side table
  fixes large values' under-count, not this overshoot.
* **Fix: one shared check, not per-function patches**
  (`src/rt/natives/support.h`).
  * `size_add` / `size_mul`: result-size arithmetic that saturates at
    `kSizeOverflow` instead of wrapping.
  * `reserve_result(context, bytes)`: the pre-allocation check; refuses
    overflowed or impossible sizes (`kMaxResultBytes`, 2^62) whatever the
    budget, and otherwise requires live total + bytes (rounded as a counted
    heap string) to fit the context's heap limit.
  * `BudgetedText`: a string result whose buffer grows only after
    `reserve_result` approved the new capacity plus the storage it replaces;
    `append` returns false and the native returns `Status::Memory` ("memory
    budget exceeded"), freeing its buffer.
  * **Allowed temporary overhead**, against the configured budget (not RSS):
    the buffer being built plus, during a reallocation, its previous storage,
    both inside the checked amount. Buffers below 32 KiB grow unchecked as
    `std::string` would, so the slack per call is at most 96 KiB (matching
    the 64 KiB stride planned for stringify).
  * New natives that build strings use `BudgetedText`; array results already
    go through `ArrayBuilder`'s budget stride.
* **Users now:** `join` (`array_methods.cpp`). **Next:** `replace`
  (`string_methods.cpp`, prepared on local branch `l7-c3-replace`, waiting
  for the DeepSeek case-conversion integration; `cased` untouched),
  `stringify` (64 KiB stride) and `flatten`; print's flush before a large
  append lives in VM output code touched by active branches, coordinated
  separately.
* **Performance.** `test-rt-builtins-guards` (callgrind, +10 % locks): all 25
  workloads within their guards. A first version that computed the exact
  size in a separate pass failed them (join +27 % to +57 %); the growth-check
  form costs nothing measurable.
* **Tests (permanent).** `tests/rt/native_budget.cpp` (in `test-rt-value`;
  replaced `operator new` recording outstanding bytes and peak): saturating
  size arithmetic and `reserve_result` edges (overflow refused even
  unbudgeted, spent budget); zero-length cases (empty array, separator,
  elements); a 4 MiB join under 1 MiB of headroom refused with the peak held
  within the headroom, nothing counted or leaked; the same join within
  budget identical to the unbudgeted one; separators alone over budget;
  repeated expansion `s = [s, s].join("")` refused at the budget, the live
  total never above budget + 96 KiB; allocation failure inside the native
  propagates `std::bad_alloc` without leaks; element errors keep legacy's
  message. Cause and effect: built against the previous `array_methods.cpp`,
  the budget cases fail and the expansion loop runs out of address space
  (`std::bad_alloc`, abort).

## 7. Remaining gates

* **REMOTE JOB** (worker), covering:
  * `test-rt-value` and `test-rt-value-sanitize`;
  * `test-rt-vm` and `test-rt-builtins-guards`;
  * `bench-rt-value` with the guard on the worker toolchain;
  * `bench-rt-value-cycle`;
  * `test-rt-heap-saturation`;
  * the full `nift` build and `rt-lint`.
* Independent review before integration (D-040 process).
* Steps 2.2–2.7b and 2.7c–d per the step-1 plan, after CP13/CP15 integrate.
