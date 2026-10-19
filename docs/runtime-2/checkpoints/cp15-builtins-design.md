# CP15 — Built-ins batch B: JSON, bytes, numbers, time (design and natives)

Status: development (`runtime-2-cp15-dev`, local, not pushed). **Base: CP14
tip `c3b0046`** (itself on CP12 work in progress `0bd5f06`; D-017
pipelining). Checkpoint row (`../checkpoints.md`): "Built-ins batch B: JSON,
bytes, numbers, time — gate: as CP14 (batch fallback = 0; per-builtin
instruction guards)". The native interface, call convention, reference and
heap rules, kind lattice and wiring plan are CP14's
(`cp14-builtins-design.md`); this record lists only what CP15 adds or
changes. Nothing is wired into the compiler/VM (CP12 not yet certified).

## Scope

New files only: `src/rt/natives/{number_methods,bytes_methods,json_methods}.{h,cpp}`;
CP14's own files extended (`builtin.h`, `builtin.cpp`, `builtin_list.inc`,
the CP14 tests, probe, mutants, guards). The natives link the CP09 value
model plus `src/Sha256.cpp` (vendored SHA-256) and `src/JsonSchema.cpp` with
the header-only Jsonic++ parser — none of which uses `RuntimeValue`
(`make test-rt-builtins-isolation`).

## Inventory (batch B)

| Family | Built-ins | Status |
| --- | --- | --- |
| N1 numbers | `to_int abs floor ceil round` (methods on numbers); `to_string` was CP14 F6; `to_double` on numbers is context-dependent (CP14 Q4) | **implemented** (5) |
| B1 bytes | `bytes([..])`, `s.encode("utf-8")`, bytes `length size empty slice decode("utf-8")`, `sha256(string \| bytes)` | **implemented** (8) |
| J1 JSON values | `json_parse` (native API: legacy's strict JSON reader behind `{...}` literals, `inject`, `@json`, `read_val`), `validate(schema, value)`, `copy(v)`, `deepcopy(v)`; serialization = CP14 F6 (`stringify`/`prettify`) | **implemented** (1 API + 3) |
| B2 bytes, host | `open_bytes`, `read_bytes`, `read_all_bytes`, `write_bytes` (files), `secure_random_bytes(n)`, `ffi_bytes`, `ffi_snapshot_bytes`, `sha256_file(path)`; bytes indexing `b[i]` (VM op) | design only: Host ops (CP16) / VM (`GetIndex`) |
| J2 JSON, host | `inject(path)`, `@json(name, path)`, `read_val()`, `write_val(v)` | CP16 (they call `json_parse` / F6 serialization) |
| T1 time | `epoch()` (Unix ms), `sleep(ms)`, `timer(...)` and timer methods `start stop pause resume reset elapsed`; template variables `build-time build-date build-UTC-time build-UTC-date build-YYYY build-YY build-timezone` | **design only**: every one reads the clock or blocks; they become Host ops in CP16 (recorded in the Host-op trace, D-013 effects). Legacy has no pure time formatting or arithmetic built-in, so nothing in T1 is implementable as a pure native |

### N1 numbers (receiver Int, Double or Decimal)

| Method | Result | Legacy errors |
| --- | --- | --- |
| `to_int()` | int: exact integer in int64 (`runtime_number_to_i64`), any size up to int64 | `to_int: expected no arguments`, `to_int: invalid or out-of-range integer` (fractions, beyond int64) |
| `abs()`, `floor()`, `ceil()`, `round()` | number: the C function on the **nearest double** (`round` halves away from zero) | `<m>: expected no arguments` |

Kinds: `to_int` → int; `abs` → int ∪ frac; `floor ceil round` → int.

### B1 bytes

| Built-in | Result | Legacy errors |
| --- | --- | --- |
| `bytes()`, `bytes(array)` | bytes; each element an integral number 0–255 (`runtime_number_to_unsigned(v, 8)`) | `bytes: expected zero arguments or one byte array`, `bytes: expected an array of integers`, `bytes: array values must be integers from 0 to 255` |
| `s.encode(enc)` | bytes of the string | `encode: expected encoding`, `encode: encoding must be exactly 'utf-8'` (exact spelling, no case folding), `encode: invalid UTF-8 text` (strict UTF-8) |
| `b.length()`, `b.size()`, `b.empty()` | int / bool | `<m>: expected no arguments` |
| `b.slice(start[, end])` | bytes (clamped like arrays) | `slice: expected start and optional end`, `slice: invalid start`, `slice: invalid end` |
| `b.decode(enc)` | string | `decode: expected encoding`, `decode: encoding must be exactly 'utf-8'`, `decode: invalid UTF-8` |
| `sha256(v)` | 64 lower-case hex characters of the string's bytes or the bytes value | `sha256: expected one string or bytes value`, `sha256: expected a string or bytes value` |

Bytes are not serializable (`bytes values are not serializable` in
`stringify`; `… not JSON serializable` in `validate`), `type(b)` is `bytes`,
`==` compares contents. **Kind lattice:** bytes need a bit CP12's 8-bit kind
set does not have (its 0x80 is Unknown); `KindSet` is now 16 bits with
`kBytes = 0x80` and `kAny = 0xff`. Wiring rule **W8**: CP12's inference
widens its set (or B1 stays refused); the wiring patch `static_assert`s the
layouts.

### J1 JSON values

* `json_parse(text)`: Jsonic++ strict parse (RFC 8259; duplicate keys
  rejected — **legacy JSON never produces a duplicate-key object**, which is
  why CP09's `from_legacy` rejection is unreachable from JSON; nesting ≤
  512) mapped exactly as legacy `runtime_from_json`: numbers whose spelling
  matters (more than 15 integer digits, zero with a fraction or sign,
  exponent magnitude ≥ 300) become `Decimal`s with that spelling (`0.0`,
  `-0`, `12345678901234567890123`, `1e-400`), others `Double`s; non-finite
  numbers are a parse error. Converted directly to rt values (no legacy
  value). Errors are the parser's message. Callers: CP16's `inject`,
  `@json`, `read_val` (they add legacy's prefix, e.g. `inject: …`) and the
  compiler for `{...}` literals, which runs it **at compile time** on the
  static source text: success → the native result is the literal's value;
  any failure → the unit is refused (W9), because legacy then falls back to
  its Nift literal evaluator (review R3). A `json_parse` error therefore never
  surfaces at run time for a literal (the earlier "callers add legacy's
  prefix" was wrong for literals).
* `validate(schema, value)`: both converted to JSON documents as legacy's
  `runtime_to_json` (Int beyond 2^53 → exact StrNumber; bytes/Error values →
  `validate: … are not JSON serializable`), then the same `jsonschema`
  validator. **Deliberate change from legacy (review R2):** the conversion is
  capped at 512 nested levels (the parser's limit); deeper values stop with
  `Status::Limit` — fatal, uncatchable, code `limit.exceeded` (D-022/D-025),
  message `validate: value nesting exceeds 512 levels`. Legacy recurses
  without a bound and crashes (the VM makes such values cheap to build).
  **Known, not fixed (review R1):** a schema `pattern` can crash or take
  exponential time (`std::regex`, ReDoS) — identical in legacy; recorded in
  the consistency register for the cleanup checkpoint. A malformed schema is a fatal error (`validate: <reason>`); a
  rejected instance is legacy's **recoverable** (catchable) Error, code
  `schema.rejected` → new `Status::Recoverable` (treated as Error until
  CP13's try/catch). Returns the value.
* `copy(v)`, `deepcopy(v)`: a recursively independent value. With
  copy-on-write the result shares storage and the first mutation of either
  side unshares only the changed path (tested). Functions and Error values
  are immutable or identity values and are returned as they are, as legacy
  copies them (review D1). A struct or handle **anywhere** in the value
  (legacy re-creates structs) is `Internal` (review D2: the native walks the
  value); struct cloning is CP17. Wiring rule **W11**: the compiler refuses
  `copy`/`deepcopy` unless the argument is provably plain (null, bool,
  number, string, bytes, or arrays/objects of plain values) — until CP17 —
  so the `Internal` check only makes a future breach loud.

## Quirks found (register candidates, D-015)

Probe: `tests/runtime2/builtins/context_probe.py`, now 272 cases × 6
contexts + 11 pairs; inventory pinned in `context_dependent.txt`.

| # | Legacy behaviour | Kind | Proposed single semantics | VM handling |
| --- | --- | --- | --- | --- |
| N-Q1 | `abs floor ceil round` work on the nearest double: `(-(2^53+1)).abs()` is 9007199254740992 | exactness (as L14) | exact integer results for integers | reproduced; register |
| N-Q2 | `abs/floor/ceil/round` with arguments: `abs: expected no arguments` at top level, arguments ignored in loop code (prepared `native_method` has no arity check) | **context** | arity error everywhere | W1 refuses wrong arity |
| B-Q1 | Two UTF-8 validators: `length`/`split("")` accept overlong 3/4-byte forms and surrogates, `encode`/`decode` reject them (`"\xed\xa0\x80".length()` is 1, `.encode("utf-8")` fails) | family inconsistency | one strict validator | reproduced (unit test pins both); register |
| B-Q2 | `encode()`/`decode()` without an argument: `expected encoding` at top level, `encoding must be exactly 'utf-8'` in loop code | **context** | one message | W1 |
| B-Q3 | Encoding names are case- and alias-sensitive (`"UTF-8"` rejected) | naming | accept `utf-8` case-insensitively (or document) | reproduced; register (low) |
| J-Q1 | A `{...}` literal that is valid JSON is parsed as JSON (JSON escapes `é`, `-0` kept, integers of any size exact), but an `[...]` literal evaluates its elements as Nift expressions (`"é"` → `u00e9`, `-0` → `0`, `12345678901234567890123` → `integer literal outside signed 64-bit range`): the same text means different values by bracket | **shape** | one literal grammar for both (JSON escapes and exact numbers in both, or neither) | compiler: structured literals compile per bracket kind exactly as today; refuse the differing spellings until decided (W9) |
| J-Q2 | `validate(schema, "x")` with a string *literal* argument fails at top level (`unknown value or malformed expression: x`: legacy strips the quotes) and works in loop code | **context** | works everywhere | W10 `builtin-literal-arg` refuses quoted literals as `validate` arguments until decided |
| J-Q3 | `copy(a, b)` evaluates `a, b` as one expression (no arity message) | diagnostics | arity error | W1 |
| J-Q4 | `json_parse` runs through a full document tree then converts: ~15 allocations and ~9,600 instructions per 70-byte record (below) | performance | direct parse into rt values | recorded; guard locked so it cannot regress; optimisation candidate |
| T-Q1 | `timer`, `epoch`, `sleep` and the `build-*` variables read the clock at evaluation time; `build-*` are local-time strftime results | effects | Host ops with recorded results (CP16) | design only |

## Tests

| Suite | Result |
| --- | --- |
| `builtins_unit` (+ batch B: every N1/B1/J1 result and message, strict vs lenient UTF-8, json_parse spellings/duplicates/trailing comma/non-finite, validate fatal vs recoverable, copy independence) | 782 checks, 0 failures |
| `builtins_diff` (random cases for all 89 natives; 1 in 8 cases a random **JSON object literal** run through legacy's JSON fast path vs `json_parse`; bytes results compared by SHA-256 of their content) | seeds 20261018, 2, 3 × 8,000: **23,935 compared, 0 differences** (65 refused by W1), about 3,000 of them JSON object literals; `make test-rt-builtins` (5,000 cases): 4,986 compared, 0 differences |
| mutation check | CP14 M1–M15 plus M16–M22 (exact `abs`, byte 256, lenient `decode`, `json_parse` dropping exact spellings, `sha256` off by one byte, `validate` accepting, banker's `round`): **all detected** (M17 after adding byte-boundary elements to the generator) |
| ASan+UBSan | **clean**: 781 unit + 1,996 differential checks, 0 failures |
| guards (callgrind) | 34 workloads × 4 sizes, locked (+10 %): 0 guard failures |
| isolation | natives + Sha256 + JsonSchema link without `RuntimeValue`/bridge/render |

## Guards added (callgrind, Ir per call)

| workload | n=1 | n=100 | n=10,000 | n=100,000 | exponent | allocs/call (10⁴ / 10⁵) |
|---|---:|---:|---:|---:|---:|---:|
| json.parse (records, ~70 B each) | 13,280 | 960,351 | 96,245,702 | 959,002,691 | 1.00 | 150,026 / 1,500,029 |
| fn.validate (records) | 103,667 | 972,013 | 90,961,991 | 914,094,110 | 1.00 | 59,103 / 599,103 |
| fn.sha256 (bytes) | 4,539 | 7,960 | 530,000 | 5,265,408 | 1.00 | 2 / 2 |
| fn.bytes (array) | 353 | 2,534 | 220,628 | 2,200,628 | 1.00 | 2 / 2 |
| str.encode | 370 | 1,463 | 110,633 | 1,100,626 | 1.00 | 2 / 2 |
| bytes.decode | 213 | 1,040 | 70,672 | 700,665 | 1.00 | 2 / 2 |
| bytes.slice (n/2) | 355 | 437 | 1,253 | 50,705 | 1.61 (memcpy) | 2 / 2 |
| num.round (per call) | 148 | 125 | 125 | 125 | 0.00 | 0 / 0 |
| num.to_int (per call) | 171 | 146 | 146 | 146 | 0.00 | 0 / 0 |

No O(n²) path (bytes.slice's exponent is glibc `memcpy` changing strategy,
as CP14's `substr`). Hidden temporaries: `json_parse` and `validate` build
Jsonic documents (J-Q4; `validate` also uses `std::regex` in the schema
validator, 104k Ir fixed cost); everything else allocates only its result.
The CP14 workloads' guards are unchanged (re-checked: 0 failures).

## Wiring additions (on top of CP14's plan)

* W8 bytes kind bit in CP12's inference (16-bit kind set).
* W9 structured literals: keep legacy's per-bracket meaning only where both
  agree; refuse `[...]` literals containing JSON-only spellings (`\u`
  escapes, `-0`, integers beyond int64) and the reverse until J-Q1 is decided.
  A `{...}` literal is compiled by running `json_parse` on its source text at
  compile time: parsed → that value; not JSON for any reason (lone
  surrogate, `01`, invalid UTF-8, keys duplicated after unescaping, depth
  beyond 512, non-finite number, trailing comma) → the unit is refused
  (legacy's fallback evaluator accepts some of these: 44 of 112 invalid
  literals in the seed-1 run).
* W10 `validate` with quoted-literal arguments refused (J-Q2).
* W11 `copy`/`deepcopy` (and `validate`) only on provably plain arguments
  until CP17 (review D1/D2).
* `Status::Limit` → fatal error with code `limit.exceeded` (not catchable).
* `recoverable_code(id)` (`builtin.h`) gives the code of a Recoverable
  result (`schema.rejected` for validate).
* `Status::Recoverable` → CP13 builds the catchable Error (`code`
  `schema.rejected`, message as returned); until then it is an Error stop.
* `json_parse` is called by the compiler (structured literals) and by CP16
  Host ops (`inject`, `@json`, `read_val`).

## Wiring (integration, branch `spec-integration`)

On `runtime-2` @`bd1c8a9` (CP12 certified) after the CP14 wiring and CP13,
by the single integration owner. Rules applied:

| Rule | Implementation |
| --- | --- |
| W8 bytes | **Refused, not widened**: CP12's kind set is frozen (its 0x80 is Unknown). Every built-in with a fixed bytes result or a bytes receiver (`bytes()`, `encode`, bytes `length size empty slice decode`) is refused before execution, `builtin-bytes:<name>`; `static_assert`s pin the bit layouts. `sha256` of a string binds (bytes values cannot exist on the VM). Widening = a 16-bit kind set in CP12's inference (backlog). |
| W9 `{...}` literals | `UnitCompiler::object_literal` runs `natives::json_parse` on the literal's source text at compile time: JSON → that value as a constant (`LoadK`; copy-on-write on first mutation); a literal of constants only (numbers, strings without interpolation, `true/false/null`, negated numbers, nested literals of those) that is not JSON → refused, `json-literal`; a literal with expressions (never JSON) compiles as before. |
| W10 | a string-literal argument of `validate` → `builtin-literal-arg:validate`. |
| W11 | `validate`, `copy`, `deepcopy`: an argument whose kinds include Unknown → `builtin-plain-arg:<name>` (the VM has no struct, handle or function values, so known kinds are plain). |
| N-Q2, B-Q2, J-Q3 | W1 arity refusals (`builtin-arity:<name>`). |
| `Status::Recoverable` | an Error stop with legacy's message and `code` `schema.rejected`, `catchable` true (VM statistics `code`/`catchable`); no try/catch on the VM yet (CP13 T4), so uncaught exactly as legacy. |
| `Status::Limit` | a fatal Error stop (exit 1), `code` `limit.exceeded`, not catchable. |
| Inference | `validate` → its value argument's type, `copy`/`deepcopy` → the argument's type; bytes results Unknown (refused). |
| Link | `src/Sha256.cpp`, `src/JsonSchema.cpp` join `RT_VALUE_SOURCES` (the rt test binaries); `nift` already links them. |

Tests: `tests/runtime2/vm_builtins.py` gains the CP15 bodies (numbers,
`sha256`/`validate`/`copy`/`deepcopy`, JSON literals with JSON-only
spellings, mixed literals) in the five contexts, error programs (N1, `sha256`
kinds, a rejected and a malformed `validate`), refusals (W8 ×2, W9, W10,
N-Q2) and the VM-only `Limit` check (600-deep value: exit 1,
`limit.exceeded`, not catchable; legacy is not the oracle there, review R2).

## Commits

Local, on `runtime-2-cp15-dev` (base `c3b0046`): natives; tests, probe,
mutants, guards; this record and evidence.

## Review round 1 (adversarial review of CP15, 2026-10-19)

| # | Finding | Fix | Test |
| --- | --- | --- | --- |
| D1 | `copy`/`deepcopy` of a function or Error value was `Internal`; legacy copies them | returned as they are; `Internal` only for Struct/Handle | unit (function identity, Error inside an array) |
| D2 | a struct/handle nested in a copied array/object stayed shared (legacy re-creates structs) | the native walks the value: any nested Struct/Handle is `Internal`; wiring rule W11 (plain arguments only, until CP17) | unit (struct in object in array, handle in object) |
| R2 | `validate` conversion recursion unbounded: a 200,000-deep value crashed | 512-level cap → `Status::Limit` (fatal, `limit.exceeded`); deliberate change from legacy (crash) | unit (512 ok, 513 Limit, 200,000 Limit) |
| R3 | `json_parse` ≠ legacy's object-literal path, which falls back to the Nift evaluator | W9 clause: compile-time `json_parse` of `{...}` literals, refuse on failure; doc fixed (here and `json_methods.h`) | differential: invalid JSON literals tallied (seeds 20261018/2/3: 299/294/295 refused; legacy's fallback *accepts* 132/123/128 of them) |
| R4 | differential missed edges | Decimal receivers via `json_parse` (bound through `{"v": …}.v`), int64 extremes (`-2^63` via the JSON literal), ±0.5/1.5/2.5 frequent; invalid JSON literals (each in its own program); `validate` cases wrapped in try/catch so Recoverable (`caught schema.rejected`) ≠ Error; new mutant M23 | M22 now dies deterministically (4 failures, seed 1); M23 detected (18) |
| R1 | schema `pattern` crash / ReDoS (`std::regex`) | identical in legacy; recorded in the register, not fixed | — |
| note | `recoverable_code(id)` was documented but missing | added (`builtin.h/.cpp`) | unit |
| note | `uniqueItems` is O(n²) (inherited) | guard `fn.validate.unique` at n ≤ 1,000 (exponent 1.95, 100→1,000) | guards |
| found | UBSan: `make_string` of an empty bytes value's view passed a null pointer to `memcpy` (`value.h:144`, CP09 `inline_text`) — reached by `bytes().decode("utf-8")` | natives never pass a null data pointer (decode, sha256 of empty bytes); the CP09 `inline_text` precondition is reported for CP09 | unit (empty decode, empty sha256), sanitized differential clean |

What was run (round 1; all heavy steps under `heavy.lock`;
`../evidence/cp15/review1/`):

| Run | Result |
| --- | --- |
| `make test-rt-builtins` (unit, differential 5,000, isolation, lint, probe) | unit 793 checks, 0 failures; differential 4,980 compared, 0 differences (188 invalid literals refused); isolation PASS; probe pinned PASS |
| differential seeds 20261018, 2, 3 × 8,000 | 23,925 compared, 0 differences; 888 invalid JSON literals refused (W9) |
| `builtins_mutants.py` (all) | 23/23 detected (M22 4 failures, M23 18); bytes mutants M17/M18/M20 re-run after the UBSan fix: 3/3 |
| `make test-rt-builtins-sanitize` | first run: UBSan stop (null `memcpy` source, above); after the fix: unit 792 + differential 1,993 checks, 0 failures |
| guards | old locked file re-checked: 34 workloads, 0 failures; re-locked with n = 1,000 added and `fn.validate.unique`: 35 workloads, 0 failures |

## Evidence

`../evidence/cp15/`: `unit-diff-probe.txt`, `diff-seeds.txt`, `sanitize.txt`,
`guards-check.txt`, `guards.md`, `mutants.txt`; `review1/` (round 1).
