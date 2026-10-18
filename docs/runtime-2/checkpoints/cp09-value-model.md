# CP09 — Value model (`src/rt`)

Status: see `../progress.md` (implemented; review round 1 fixed, awaiting re-review).

## Objective
Provide the Runtime 2.0 value representation as a standalone library: a
compact tagged value, immutable strings with atoms for object keys,
copy-on-write aggregates, and numeric, equality, ordering and hashing
semantics that match the legacy engine. Convert values in both directions
so the new and legacy engines can coexist. The interpreter is not changed in
this checkpoint.

## Scope
In scope: `src/rt/*` (library), `tests/rt/*` (unit, property and differential
tests, benchmark, lint fixtures), `tools/rt-lint.py`, `tools/rt-bench.sh`,
Makefile targets and the certification guard.

Out of scope, and left unchanged: the interpreter, number rendering and
formatting, string methods, and the full handle lifetime (CP17). Nothing in
the shipped binary links `src/rt` yet.

## Implementation

### Layout

| Bytes | Content |
| --- | --- |
| 0–7 | payload: int64, double bits, bool, heap pointer, handle id, or inline string bytes 0–7 |
| 8–13 | extra: handle generation (8–11) or inline string bytes 8–13 |
| 14 | aux: inline string length with flag `0x80`, or the handle kind |
| 15 | kind |

`sizeof(rt::Value) == 16`, enforced with `static_assert`. The legacy
`RuntimeValue` is 152 bytes.

### Kinds

| Kind | Storage |
| --- | --- |
| Null, Bool, Int (int64), Double | immediate |
| String | inline when it is 14 bytes or shorter; otherwise a refcounted `StringObject` |
| Decimal | `DecimalObject`: the exact spelling plus its nearest double. This is the legacy `StrNumber`. |
| Bytes, Array, Object, Function, Struct, Error | refcounted heap objects (`heap.h`) |
| Handle | immediate `(HandleKind, 64-bit id, 32-bit generation)` |

Refcounts are plain `uint32_t`. They are not atomic, because values are
owned by one isolate. Cross-isolate transfer must deep-copy, and object keys
must be re-interned in the receiving isolate's atom table. Refcounts
saturate: an object whose count reaches `UINT32_MAX` becomes immortal
(never freed, a leak instead of a use-after-free on overflow).

Destruction is iterative: `destroy_heap_object` detaches children whose last
reference dies onto a worklist before deleting the parent, so values of any
nesting depth are freed without stack growth (tested at 1,000,000 levels of
arrays, objects, and function/struct chains, at `-O2` and under ASan).

### Strings and atoms
Strings are immutable. Inline strings can only be built by `make_string`,
which checks the length (`Value::inline_text` is private). Object keys are `Atom`s (a `uint32_t` id) from a
`thread_local` `AtomTable` per isolate, with id 0 reserved for `""`. Lookup
compares atom ids, never text.

### Copy-on-write
Copying a `Value` shares the heap object and increments its refcount.
`array_mut`, `array_push`, `object_set` and `object_remove` call
`unshare()` first, which makes a shallow clone when the refcount is above 1.
Nested values stay shared until they are themselves mutated.

Reference contract (`aggregate.h`): references and pointers from
`array_items`, `array_mut`, `object_entries` and `object_get`, and references
to elements reached through them, are invalidated by `array_push`,
`object_set`, `object_remove`, `unshare`, `array_mut`/`object_mut`, by copying
the value and mutating either copy, and by assigning or destroying the
value. Holding an `array_mut` reference across a copy and then writing
through it writes into the other sharer's storage (a copy-semantics
violation) or into freed memory. Callers must re-fetch after any of these.

Ownership and lifetime contract (since the CP11 review round 2, owner item
6). A `Value` owns one reference to its heap object; a reference or pointer
obtained from it (element, member, entry, `string_view_of`, `array_mut`)
borrows from the object it was read from and is valid only until the next
mutation entry point on that object or on any Value sharing it
(`array_mut`, `array_push`, `object_set`, `object_remove`, `unshare`,
`append_in_place`), or until that Value is reassigned or destroyed. A
borrowed element that must outlive such a point is copied out first (a
`Value` copy is O(1)). **Enforced** in sanitizer builds (AddressSanitizer,
or `-DNIFT_RT_CHECKED_REFS`): every mutation entry point first moves the
storage of the aggregate (and, on `unshare`, of the shared original; for
`append_in_place`, of the string) to a fresh allocation and frees the old
one, so any reference held across it is a deterministic heap-use-after-free
that ASan reports on its next use, not only when a real reallocation
happens to occur. `make test-rt-value-sanitize` checks the mechanism itself
(`value_round2 stale-array|stale-object|stale-string` must abort), and every
sanitized suite (`test-rt-value-sanitize`, `test-rt-vm-sanitize`: unit tests,
corpus, generated programs, the adversarial gate) runs with it. Release
builds compile it away (no cost).

Audit of the VM and compiler (CP11 round 2): `get_index`, `get_member`
copy the element out (`out = items[i]`; `Value::operator=` copies before
releasing, so `R[a] = R[a][i]` is safe); `set_index` reads the old
element's type, then calls `array_mut` with no reference held, and the
assigned value was copied first; `push_value`/`array_push`, `ObjectSet` and
`NewArray` copy their argument before mutating; `concatenate` builds a new
string; in-place `s += t` (`append_in_place`) handles `t` aliasing `s`. No
borrowed reference is held across a mutation, a copy-then-mutate, a push, a
set or an unshare.

### Objects
Objects are an insertion-ordered `vector<pair<Atom, Value>>`. From 8 entries
upward they also keep an open-addressing index: multiplicative hash of the
atom id, with slots storing position + 1.
- Replacing a key keeps its position, as in legacy.
- Removing a key rebuilds the index.
- A legacy object with a duplicate key does not convert (see
  Compatibility).

### Numeric tower
The numeric tower follows the legacy canonical (textual) evaluator,
`numeric_binary` in `ParserExpression.cpp`.
- `+ - * %` use checked int64 arithmetic when both operands are exactly int64.
- Everything else, and `/` always, uses doubles.
- The error messages are the legacy ones.
- `INT64_MIN % -1` is 0.
- The int64 path returns an `Int`. When converted back to legacy it becomes
  `runtime_integer`, which is a double up to 2^53 and an exact `StrNumber`
  beyond that.
- The double path returns a `Double`.

### Equality, ordering and hashing
These are ported from legacy `NumericForm` and `runtime_equal`.
- Numbers compare by exact decimal value across Int, Double and Decimal, so
  `1 == 1.0 == 1.00`. A double compares by its shortest round-trip spelling.
- NaN is unequal to everything.
- Objects compare as unordered entry sets. Errors compare structurally to a
  depth of 16.
- Functions, structs and handles compare by identity.
- Decimal exponents are exact at any size, as in legacy (whose exponent is
  an arbitrary-precision decimal): `DecimalForm` keeps exponents up to 10^18
  in an `int64_t` and larger ones as exact decimal text, with a canonical
  split. Zero, integer-ness, ordering, equality, hashing, truthiness and the
  arithmetic error messages therefore match legacy for spellings such as
  `0e99999999999999999999` and `1e-99999999999999999999`.
- `equal` and `hash` are iterative (heap worklists; CP11 review round 2,
  R6): no nesting depth reaches the C++ stack (300,000-deep values are
  tested). `equal_within(a, b, max_depth)` visits pairs depth-first in
  legacy's order (array elements in order; object entries in the left
  object's order, matched by key; no identity shortcut when bounded) and
  returns `TooDeep` when two aggregates below `max_depth` still had to be
  compared; the VM uses it (default 10,000 levels, legacy's recursive
  `runtime_equal` answers to about 30,000 and crashes near 50,000) and hands
  deeper comparisons to legacy.
- Hashing is a function of the exact decimal form that equality compares (a
  Double by its shortest round-trip spelling). Integers with |v| ≤ 2^53 take
  an integer shortcut that every kind reaches for the same value; all other
  numbers, including integral doubles above 2^53, hash the canonical
  fingerprint. Objects hash by an order-insensitive sum. This makes
  `equal ⇒ same hash` hold across all number kinds and key orders.

### Errors
`make_error` validates codes exactly as legacy (`user.*`, 2–8 segments),
limits the cause chain to 16, and requires the origin fields all-or-nothing.
`make_user_error(message)` gives `code = "user.raised"` and
`category = "user"`.

### Handles
`HandleRegistry<kind>` provides `add`, `resolve`, `release` and `live`.
Releasing a handle bumps its generation, so stale and foreign-kind handles
fail to resolve. A slot whose generation has reached `UINT32_MAX` is retired
on release and never reused, so generations never wrap. `add(nullptr)` is
rejected (returns Null, uses no slot). Handles have no string encoding.

### Files
One concern per file:

| Concern | Files |
| --- | --- |
| value | `value.h/.cpp` |
| heap layouts | `heap.h` |
| atoms | `atom.h/.cpp` |
| strings, bytes, decimals | `string.h/.cpp` |
| aggregates | `aggregate.h/.cpp` |
| errors | `error.h/.cpp` |
| handles | `handle.h/.cpp` |
| decimal forms | `decimal.h/.cpp` |
| arithmetic | `number.h/.cpp` |
| comparison | `compare.h/.cpp` |
| legacy bridge | `legacy_bridge.h/.cpp` |

## Compatibility
Where semantics.md is silent, rt follows legacy. Each such decision:

| Topic | Decision |
| --- | --- |
| Prepared (loop) AST path computes `+ - *` in doubles without overflow checks | rt follows the canonical path, with checked int64. This is a known legacy divergence already recorded under D-009; the rt choice matches semantics.md "Integers". |
| Kinds of numeric results | Int for the int64 path, Double for the double path. Observable values are identical after `to_legacy`. |
| Decimal exponents | Exact at any size, as legacy (fixed in review round 1; previously a known divergence beyond ±2.3×10^17). A spelling that is not a finite JSON number falls back to its double, as legacy `numeric_form` does. |
| Legacy objects with duplicate keys | `from_legacy` fails with `object has duplicate key '<k>'`. Legacy treats such an object as a multiset (size, iteration and equality count both entries; lookup sees the first), which a unique-key rt object cannot represent; keeping either occurrence silently changed equality both ways (`{a:1,a:2} == {a:2,a:1}` is 1 in legacy, and `{a:1,a:2} == {a:1}` is 0). Reachability: none from scripts found. JSON parsing (`nift_json::parse`, `DuplicateKeyPolicy::Reject`), object literals, YAML front matter, `from_entries` and `index_by` all reject duplicates, and the grouping and counting builders in `ParserExpression.cpp` merge by key; the only `json::Document::parse` call with default options (`Ast.cpp`) accepts numbers only. Only host C++ appending to `RuntimeValue::object` directly can build one. (Found by grep, not proven exhaustive.) |
| Legacy handle strings (`"\x1fnift:…"`) | Converted as strings. Typed handles arrive with CP17. |
| Function, Struct, non-timer Handle → legacy | Conversion fails with an explicit error, because legacy has no equivalent value. |

## Tests
Counts are from `runtime-2-cp09` after review round 1 (see Evidence).

| Suite | Content | Result |
| --- | --- | --- |
| `tests/rt/value_unit.cpp` | Layout, inline/heap strings and refcounts, array and object COW, insertion order, replace/remove, the 300-key index, unordered object equality and hash, the full arithmetic table (overflow, division, modulo, INT64_MIN % -1, non-finite, decimals beyond 2^53), `1 == 1.0 == 1.00` hash, NaN, truthiness, decimal parsing, errors, handles (stale and foreign), functions, structs | 82 checks, 0 failures |
| `tests/rt/value_regress.cpp` (review round 1) | Ports of the reviewer's repros: number equality and hash beyond 2^53 for Int/Double/Decimal, each checked against legacy `runtime_equal`; extreme exponents (zero, sign, truthiness, integer-ness, i64 conversion, ordering, the 10^18 split, and the four arithmetic error messages checked against the legacy oracle); handle retirement at `UINT32_MAX` (via a test hook), double release, `add(nullptr)`; COW programs within the reference contract; destruction of 1,000,000-deep arrays (moved and COW-built `a = [a]`), objects and function/struct chains; inline strings at 0–40 bytes with binary content; a compile-time check that `inline_text` is not callable; refcount saturation; duplicate-key rejection (flat, nested, indexed) | 270 checks, 0 failures; 0.7 s at `-O2`, 4.7 s under ASan, default 8 MiB stack |
| `tests/rt/value_property.cpp`, default seed | 200,000 random numeric cases vs the legacy oracle (result or error, doubles bit-exact), negation, equality, ordering, truthiness, `number_to_i64`, hash implication. Numbers include integral doubles above 2^53, Decimal exponents of any size (around 10^18 and with up to 41 digits), zero mantissas, and in a quarter of cases a second operand that is the first value in another kind or spelling. 20,000 random nested legacy values (round trip, equality incl. shuffled key order, truthiness, hash), with objects up to 64 keys from an 86-key pool and occasional duplicate keys (conversion must fail exactly when a duplicate exists anywhere). 500 array COW sequences. 300 object sequences of 120 set/remove/re-add/alias/nest/nested-mutate steps over 4 aliased slots and up to 70 keys, checking every slot's entries in order against a legacy deep-copy model, every key lookup (present and absent) on the mutated slot, and equality/hash against a reverse-order rebuild every 8 steps | 1,867,405 checks, 0 failures. Reported coverage: 47,126 equal number pairs hash-checked (29,002 cross-kind), 9,148 cases with a big-exponent Decimal, 30,095 integral doubles > 2^53, 959 generated objects ≥ 8 keys, 663 duplicate-key rejections, 754,979 lookups on indexed objects |
| Seed sweep (seeds 1–5, scale 5) | `evidence/cp09/property-seed-sweep.txt` | 46,347,719 checks, 0 failures |
| Mutations (review round 1) | See the table below | all detected; each reverted |
| `make test-rt-value-sanitize` | ASan+UBSan (`-fno-sanitize-recover=all`, leak detection on): unit, regress (1,000,000-deep values) and property at seed 20261017 scale 1; plus property seed 7 scale 2 | clean |
| `make test-rt-lint` | Fixture self-test (each rule fires) plus `src/rt` | PASS, 0 problems |

Mutation runs (`-O2` binaries; property at the default seed, scale 1). A
crash counts as detection; where the property suite crashed, the CHECK lines
it printed first are named.

| Mutation | unit | regress | property |
| --- | --- | --- | --- |
| A: `from_legacy` keeps the last duplicate key | 0 | 4 failures | 1,128 failures |
| A0: keeps the first duplicate (the pre-review behaviour) | 0 | 4 failures | 1,128 failures |
| B: `object_mut` skips `unshare` (object COW) | 1 failure | 1 failure | slot-model and lookup CHECKs fail, then segfault |
| C: `object_remove` skips the index rebuild | 1 failure | 0 | lookup CHECKs fail (object model), then abort |
| R1: pre-review number hash (i64 shortcut for every integer) | 0 | 3 failures | 895 failures |
| R2: pre-review exponent limit (reject > 17 exponent digits) | 0 | 11 failures | 5,877 failures |
| R3: handle generations wrap (no retirement) | 0 | 4 failures | 0 (not covered) |
| R5: recursive destruction | 0 | segfault | 0 (not covered) |
| R6: no refcount saturation | 0 | 2 failures, then glibc aborts on heap corruption | 0 (not covered) |

Before round 1, mutation A0 was the shipped code and survived the suite; the
structural generator never built objects with 8 or more keys, the COW model
covered arrays only, and decimal exponents stayed below 30.

The legacy oracle in `tests/rt/legacy_oracle.h` is a copy of the canonical
`numeric_binary`. The canonical evaluator is a static function in
`ParserExpression.cpp` and cannot be linked on its own. Equality,
comparison, truthiness, negation and integer conversion use the real legacy
functions from `src/RuntimeValue.cpp`.

## Lint (`tools/rt-lint.py`)

| Rule | Check |
| --- | --- |
| function-length | No function body over 150 lines |
| line-length | No line over 120 columns |
| string-compare | Heuristic: no comparison against a string literal and no `strcmp`/`strncmp`. A line can opt out with `// rt-lint: allow-string-compare <why>`, and a file with `// rt-lint: cold-file <why>`. `error.cpp` is cold, because member lookup by name runs on error paths only. |
| one-concern | Each header states its concern after `#pragma once`. Each `.cpp` includes its own header first. No file exceeds 400 lines. |

## Performance
Measured with `make bench-rt-value`: callgrind instruction counts with the
empty run subtracted, GCC 13.3 `-O2`.

| Workload | rt Ir | legacy Ir | rt / legacy |
| --- | ---: | ---: | ---: |
| copy (1,000 copies of a 10k-string array) | 4,207,707 | 1,577,088,074 | 0.003 |
| push (100k numbers) | 9,235,173 | 30,067,859 | 0.307 |
| index only (1M reads) | 4,000,200 | 2,500,122 | **1.600** |
| object set (128k sets, 64 keys) | 13,119,442 | 136,269,836 | 0.096 |
| object get only (128k gets, 64 keys) | 5,121,447 | 118,159,871 | 0.043 |

Re-measured after review round 1. Copy (3.81M → 4.21M Ir) and push
(8.24M → 9.24M Ir) grew by about 10–12% from the refcount saturation
compare in retain/release and the iterative destruction pass; both remain
far below legacy.

The index row is a regression in instruction count. Both engines do one
load per element. GCC keeps the rt accumulator in a general-purpose register
because the value is stored as a byte array, which adds two moves per
element (4 vs 2.5 Ir per read). The rt array strides 16 bytes instead of 152,
so its cache behaviour is better, but that does not show in instruction
counts. Storing the payload as a union may fix this. It is left open
(see Risks).

## Exit criteria

| Criterion | Status |
| --- | --- |
| 16-byte value with all listed kinds | met |
| Refcounted, non-atomic, isolate-owned heap kinds | met |
| Strings with atoms; COW aggregates; ordered objects with atom lookup | met |
| Equality, compare and hash match semantics.md and legacy | met in differential tests, including exponents of any size (round 1) |
| Numeric tower matches the legacy canonical path | met (200k cases per run, 5M in the sweep) |
| Typed handles and registry stub; legacy error values | met |
| Conversions with round-trip tests | met |
| Lint and `make test-rt-lint` | met |
| Sanitizer-clean rt tests | met |
| Benchmarks vs legacy | recorded; the index read is 1.6× legacy Ir (open) |
| `make test-rt-value` in `make test` and the guard's REQUIRED list | met |
| `make certify` | not re-run after review round 1 (host constraint); `evidence/cp09/certify-summary.txt` is from the pre-review commit |

## Risks
- **Index read instruction count is 1.6× legacy.** A union payload may
  remove this. It should be re-measured once the VM's real access path
  (with a kind check) exists.
- **Atom tables are `thread_local`.** This is correct for one isolate per
  thread. If isolates migrate between threads, the table must instead hang
  off the isolate object. That is an architecture point for CP11+.
- **Reference invalidation is enforced only in sanitizer builds.** The
  relocate-on-mutation check (Copy-on-write) turns any violation into an
  ASan abort in the sanitized suites; a release build relies on the audited
  contract. A violation on a path no sanitized test executes would go
  unnoticed.
- **Recursion that remains.** Destruction, `equal` and `hash` are
  iterative. `from_legacy`/`to_legacy` still recurse once per nesting level
  and fail cleanly ("value nesting exceeds 512 levels") at
  `kMaxBridgeDepth`, which bounds their stack use (also under ASan); neither
  is reachable from the VM. Error cause chains are bounded at 16.
- **Live-heap accounting granularity.** `heap_account.h` counts each object
  as its own size plus the capacity it owns, rounded up to 64-byte granules
  and saturating at 1 GiB per object, with libstdc++'s 15-character inline
  string buffer; the count is exact in those units (it returns to its
  starting value when everything is freed, tested) but is not the
  allocator's real usage (malloc headers, fragmentation).
- **Refcount saturation leaks.** An object referenced 2^32 − 1 times
  becomes immortal. This trades an unreachable-in-practice leak for memory
  safety.
- **Handle slots retire after 2^32 generations**, so a registry that
  churns one slot that often grows by one slot per 2^32 releases.
- **Duplicate-key legacy objects** do not convert; the whole-unit fallback
  must treat that conversion error as "stay on legacy". Reachability was
  checked by grep, not proven.
- **Sanitizer coverage:** the sanitizer target is not part of `make test`,
  because sanitizers are unavailable on some CI platforms (MSVC). It is run
  manually per checkpoint.

## Deliverables
`src/rt/` (11 concerns); `tests/rt/value_unit.cpp`,
`tests/rt/value_regress.cpp`, `tests/rt/value_property.cpp`, `tests/rt/value_bench.cpp`,
`tests/rt/legacy_oracle.h`, `tests/rt/rt_test.h`, `tests/rt/lint_fixtures/`;
`tools/rt-lint.py`, `tools/rt-bench.sh`; Makefile targets `test-rt-value`,
`test-rt-lint`, `test-rt-value-sanitize`, `bench-rt-value`; certification
guard REQUIRED list.

## Commit strategy
Local commits on `runtime-2-cp09`, none pushed:
1. the library;
2. the tests, lint, benchmark and build wiring;
3. this record, the evidence and the progress row;
4. review round 1: library fixes;
5. review round 1: regression tests and wider property generators;
6. review round 1: property coverage report;
7. review round 1: this record and refreshed evidence.

## Evidence
`docs/runtime-2/evidence/cp09/` (refreshed after review round 1 unless noted):
`unit.txt` (`make -j2 test-rt-value`: unit, regress, property), 
`property-default.txt`, `property-seed-sweep.txt`, `sanitize.txt`,
`lint.txt`, `bench-callgrind.md`; `certify-summary.txt` is from the
pre-review commit and was not re-run. (Earlier revisions of this record
pointed at an `evidence/cp09/README.md` that was never committed.)

## Reviews

### Round 1 (adversarial review)

| # | Finding | Status | Fix | Test |
| --- | --- | --- | --- | --- |
| 1 | DEFECT: `equal ⇒ same hash` failed for integral doubles above 2^53 (Double(2^62) equals Int/Decimal 4611686018427388000 but hashed its exact value) | fixed | `hash_number` (`compare.cpp`): integer shortcut only for \|v\| ≤ 2^53 per kind; otherwise the canonical fingerprint of the exact decimal form | `value_regress` `hash_beyond_2_53`; property: integral doubles above 2^53 and same-value operands in other kinds (mutation R1: 895 property failures) |
| 2 | DEFECT: Decimal exponents beyond ±2.3×10^17 fell back to the double; zero/integer/i64/truthiness/arithmetic errors disagreed with each other and with legacy | fixed (exact) | `DecimalForm` keeps exponents up to 10^18 in `int64_t` and larger ones as exact text; `number_form` shared by `number.cpp` and `compare.cpp`; legacy fallback to the double for non-JSON spellings | `value_regress` `extreme_exponents` (vs `runtime_equal`, `runtime_truthy`, `runtime_number_is_integer`, `runtime_compare_numbers_relational`, oracle errors); property: exponents of any size (mutation R2: 5,877 failures) |
| 3 | DEFECT: handle generation wrapped at 2^32; `add(nullptr)` created a dead live slot | fixed | retire a slot at `UINT32_MAX`; reject null resources; `retired()`; `set_free_slot_generation_for_test` hook | `value_regress` `handles` (mutation R3: 4 failures) |
| 4 | RISK: references from `array_mut`/`object_get`/elements invalidated by copy+mutate, push, set, unshare | documented | contract in `aggregate.h` and Copy-on-write above; no debug assertion | `value_regress` `cow_contract_cases` covers the in-contract programs only |
| 5 | RISK: recursive destruction overflowed the stack (~400k depth) | fixed | iterative worklist in `destroy_heap_object` (`value.cpp`); `Value::release_detached` | 1,000,000-deep arrays (moved and COW), objects, function/struct chains at `-O2` and ASan (mutation R5: segfault). `equal`/`hash`/conversions still recurse (Risks) |
| 6 | RISK: public `inline_text` without length check; uint32 refcount overflow | fixed | `inline_text` private, `make_string` befriended; refcounts saturate at `UINT32_MAX` (immortal) | compile-time `static_assert`; saturation test (mutation R6 detected) |
| 7 | DOC/DEFECT: duplicate legacy keys changed equality both ways | fixed by rejection | `from_legacy` fails with `object has duplicate key '<k>'`; reachability in Compatibility | `value_regress` `duplicate_keys`; property duplicates (mutations A and A0: 4 regress / 1,128 property failures) |
| 8 | Test honesty: no objects ≥ 8 keys, arrays-only COW model, small exponents, no duplicates | fixed | generators extended (see Tests); coverage counts printed per run | mutations A, B, C detected by the property suite (table above) |

Mutation B is taken to be "object COW skips `unshare`" (the reviewer's text
named A and C; B was not restated in the brief this round worked from).

### Follow-ups from the CP11 review, round 2 (2026-10-18)

| # | Finding | Status | Fix | Test |
| --- | --- | --- | --- | --- |
| R6 | RISK (CP11 review): `rt::equal` recursion segfaulted on ~50,000-deep nested arrays reached by the VM (legacy also crashes at 50,000; it returns `true` at 20,000–30,000) | fixed | `equal` and `hash` iterative (same results and hash values as before); `equal_within` depth-bounded in legacy's visiting order (the VM bails beyond 10,000 levels, so legacy decides exactly where it can); `from_legacy`/`to_legacy` fail cleanly beyond 512 levels | `tests/rt/value_round2.cpp`: 300,000-deep equal/unequal/hash, bound and visiting order, bridge depth both ways, hash values against the recursive definition; VM: unit tests (9,999 decided, 10,001 bails), corpus `review2_r6_deep_equality.f`, adversarial `deepeq` family |
| R5 | D-013 violation (CP11 review): the VM's frame-memory budget sampled values two levels deep, so nested values evaded it and the process was OOM-killed | fixed (exact) | `heap_account.{h,cpp}`: every heap object records the footprint it was counted with (24 bits of the header padding: `HeapObject` stays 8 bytes); the isolate keeps the exact total; creation, capacity changes (string append, push, insert/remove, COW clone) and destruction update it | `value_round2` heap accounting (strings, appends, nested arrays, COW clone, objects, 100,000-deep nesting; the total returns exactly to its start); VM: see the CP11 record |
| 4 (round 1) | RISK: reference invalidation was a contract without a check | fixed (enforced in sanitizer builds) | relocate-on-mutation in `aggregate.cpp`/`string.cpp` (Copy-on-write above); ownership/lifetime contract and VM/compiler audit documented | `value_round2 stale-*` must abort under ASan (`test-rt-value-sanitize`); every sanitized suite runs with the check |

### Follow-up from the CP11 review, round 3 (2026-10-18)

| # | Finding | Status | Fix | Test |
| --- | --- | --- | --- | --- |
| F4 | RISK (CP11 review, memory budget): `equal_within` pushed every child pair of each compared aggregate on a work list (24 B per element, not part of the counted heap): two 1M-element arrays peaked at 73 MB under a 50 MB VM budget | fixed | one frame (left, right, next child) per open nesting level: O(depth) working memory, at most `max_depth` + 1 frames; visiting order, results and the TooDeep boundary unchanged (`hash` already used one frame per level) | `value_round2` (300,000-deep equal/unequal, bound and visiting order) unchanged and passing; CP11 `vm_subset.py` equality-memory checks (peak RSS under a 50 MB budget: 86 MB on the pre-fix build, ~37 MB after) |
