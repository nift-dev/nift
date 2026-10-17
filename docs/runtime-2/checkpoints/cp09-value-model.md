# CP09 — Value model (`src/rt`)

Status: see `../progress.md` (implemented, under review).

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
must be re-interned in the receiving isolate's atom table.

### Strings and atoms
Strings are immutable. Object keys are `Atom`s (a `uint32_t` id) from a
`thread_local` `AtomTable` per isolate, with id 0 reserved for `""`. Lookup
compares atom ids, never text.

### Copy-on-write
Copying a `Value` shares the heap object and increments its refcount.
`array_mut`, `array_push`, `object_set` and `object_remove` call
`unshare()` first, which makes a shallow clone when the refcount is above 1.
Nested values stay shared until they are themselves mutated.

### Objects
Objects are an insertion-ordered `vector<pair<Atom, Value>>`. From 8 entries
upward they also keep an open-addressing index: multiplicative hash of the
atom id, with slots storing position + 1.
- Replacing a key keeps its position, as in legacy.
- Removing a key rebuilds the index.
- When converting from legacy, a duplicate key keeps its first occurrence,
  matching legacy lookup.

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
- Hashing: integral numbers hash as an integer, other numbers by the
  fingerprint of their decimal form, and objects by an order-insensitive sum.
  This makes `equal ⇒ same hash` hold across all number kinds and key orders.

### Errors
`make_error` validates codes exactly as legacy (`user.*`, 2–8 segments),
limits the cause chain to 16, and requires the origin fields all-or-nothing.
`make_user_error(message)` gives `code = "user.raised"` and
`category = "user"`.

### Handles
`HandleRegistry<kind>` provides `add`, `resolve`, `release` and `live`.
Releasing a handle bumps its generation, so stale and foreign-kind handles
fail to resolve. Handles have no string encoding.

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
| Decimal exponents | rt parses exponents up to about ±2.3×10^17. A `Decimal` spelling with a larger exponent falls back to its nearest double for comparison and hashing, whereas legacy compared it exactly. Such a spelling cannot come from arithmetic; it can only come from JSON input. **Known divergence.** |
| Legacy handle strings (`"\x1fnift:…"`) | Converted as strings. Typed handles arrive with CP17. |
| Function, Struct, non-timer Handle → legacy | Conversion fails with an explicit error, because legacy has no equivalent value. |

## Tests

| Suite | Content | Result |
| --- | --- | --- |
| `tests/rt/value_unit.cpp` | Layout, inline/heap strings and refcounts, array and object COW, insertion order, replace/remove, the 300-key index, unordered object equality and hash, the full arithmetic table (overflow, division, modulo, INT64_MIN % -1, non-finite, decimals beyond 2^53), `1 == 1.0 == 1.00` hash, NaN, truthiness, decimal parsing, errors, handles (stale and foreign), functions, structs | 82 checks, 0 failures |
| `tests/rt/value_property.cpp`, default seed | 200,000 random numeric cases. Each compares the result or error against the legacy oracle (doubles compared bit-exact), plus negation, equality, ordering, truthiness, `number_to_i64` and the hash implication. Also 20,000 random nested legacy values (round trip keeps value, type and key order; equality against `runtime_equal`, including shuffled key order; truthiness; hash) and 500 COW sequences of 40 alias and mutate steps, checked against a deep-copy model | 367,200 checks, 0 failures |
| Seed sweep (seeds 1–20, scale 5) | `evidence/cp09/property-seed-sweep.txt` | 36,725,138 checks, 0 failures |
| Mutation check | A wrong overflow message was injected into `number.cpp` | 6,588 failures detected; the change was reverted |
| `make test-rt-value-sanitize` | ASan+UBSan (`-fno-sanitize-recover=all`, leak detection on) for the unit suite and the property suite at seed 20261017 scale 1 and seed 7 scale 3 | clean |
| `make test-rt-lint` | Fixture self-test (each rule fires) plus `src/rt` | PASS, 0 problems |

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
| copy (1,000 copies of a 10k-string array) | 3,810,476 | 1,577,088,074 | 0.002 |
| push (100k numbers) | 8,235,942 | 30,067,859 | 0.274 |
| index only (1M reads) | 4,000,214 | 2,500,136 | **1.600** |
| object set (128k sets, 64 keys) | 13,119,456 | 136,269,850 | 0.096 |
| object get only (128k gets, 64 keys) | 5,121,461 | 118,159,885 | 0.043 |

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
| Equality, compare and hash match semantics.md and legacy | met in differential tests; the decimal-exponent divergence is documented |
| Numeric tower matches the legacy canonical path | met (200k cases per run, 1M+ in the sweep) |
| Typed handles and registry stub; legacy error values | met |
| Conversions with round-trip tests | met |
| Lint and `make test-rt-lint` | met |
| Sanitizer-clean rt tests | met |
| Benchmarks vs legacy | recorded; the index read is 1.6× legacy Ir (open) |
| `make test-rt-value` in `make test` and the guard's REQUIRED list | met |
| `make certify` | see `evidence/cp09/README.md` |

## Risks
- **Index read instruction count is 1.6× legacy.** A union payload may
  remove this. It should be re-measured once the VM's real access path
  (with a kind check) exists.
- **Atom tables are `thread_local`.** This is correct for one isolate per
  thread. If isolates migrate between threads, the table must instead hang
  off the isolate object. That is an architecture point for CP11+.
- **Decimal exponents beyond ±2.3×10^17** compare approximately (see
  Compatibility).
- **Sanitizer coverage:** the sanitizer target is not part of `make test`,
  because sanitizers are unavailable on some CI platforms (MSVC). It is run
  manually per checkpoint.
- **Conversion helpers recurse.** Nesting depth is bounded only by the
  input, as it is in legacy.

## Deliverables
`src/rt/` (11 concerns); `tests/rt/value_unit.cpp`,
`tests/rt/value_property.cpp`, `tests/rt/value_bench.cpp`,
`tests/rt/legacy_oracle.h`, `tests/rt/rt_test.h`, `tests/rt/lint_fixtures/`;
`tools/rt-lint.py`, `tools/rt-bench.sh`; Makefile targets `test-rt-value`,
`test-rt-lint`, `test-rt-value-sanitize`, `bench-rt-value`; certification
guard REQUIRED list.

## Commit strategy
Three local commits on `runtime-2-cp09`, none pushed:
1. the library;
2. the tests, lint, benchmark and build wiring;
3. this record, the evidence and the progress row.

## Evidence
`docs/runtime-2/evidence/cp09/README.md`

## Reviews
None yet. An independent review is requested. Focus points:
- hash/equality consistency for Decimal values with extreme exponents;
- COW correctness when `array_mut` references are held across `array_push`
  on the same value;
- the `thread_local` atom-table decision.
