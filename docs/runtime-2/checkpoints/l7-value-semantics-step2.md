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
`test-rt-value-sanitize`. Default seed 20261020: 111,896 checks, 0
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
8. copy-on-write metadata (§2).

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

**Guard.** `bench-rt-value` now fails when a workload exceeds the locked
baseline (`tests/rt/value_bench_baseline.json`) by more than 2 %. Counts
from another toolchain are reported, not judged. The baseline is the table
above (GCC 13.3.0). The worker's run (REMOTE JOB) confirms it on the
certification toolchain.

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

## 6. Memory investigation defects C1 and C3 (separate; planned, not started)

These are existing runtime defects, recorded separately from L7 and fixed in
their own commits with their own tests and evidence.

* **C1.** `destroy_heap_object`'s worklist allocates during `noexcept`
  destruction, so the VM aborts (SIGABRT) under `ulimit -v`, also during
  teardown after "memory budget exceeded".
  * Requirement: destruction stays non-recursive and needs no heap
    allocation once memory is exhausted, and the original failure is
    reported intact.
  * Evidence planned: cause and effect (the SIGABRT is back with the fix
    disabled and gone with it enabled), a deep-nesting stress test,
    ASan/UBSan, and the bounded `ulimit -v` matrix.
  * Waits for memcheck2's final report.
* **C3.** `join` and `replace` build results without checking the heap
  budget. The fix checks and reserves before allocating, with
  overflow-checked size arithmetic. It lives in `string_methods.cpp`, which
  is sequenced after the DeepSeek case-conversion change (`cased` stays
  untouched).
  * Tests: output-size overflow, large replacements, zero-length patterns,
    repeated expansion, allocation failure, and the allowed bounded
    temporary overhead defined against the configured budget.
  * Coordinated through the coordinator before editing.

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
