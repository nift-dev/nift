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
  value). Errors are the parser's message; callers add legacy's prefix
  (`invalid structured literal: …`, `inject: …`).
* `validate(schema, value)`: both converted to JSON documents as legacy's
  `runtime_to_json` (Int beyond 2^53 → exact StrNumber; bytes/Error values →
  `validate: … are not JSON serializable`), then the same `jsonschema`
  validator. A malformed schema is a fatal error (`validate: <reason>`); a
  rejected instance is legacy's **recoverable** (catchable) Error, code
  `schema.rejected` → new `Status::Recoverable` (treated as Error until
  CP13's try/catch). Returns the value.
* `copy(v)`, `deepcopy(v)`: a recursively independent value. With
  copy-on-write the result shares storage and the first mutation of either
  side unshares only the changed path (tested). Structs (legacy re-creates
  them, identity) and collections are CP12 phase B / CP17 (natives report
  `Internal`).

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
* W10 `validate` with quoted-literal arguments refused (J-Q2).
* `Status::Recoverable` → CP13 builds the catchable Error (`code`
  `schema.rejected`, message as returned); until then it is an Error stop.
* `json_parse` is called by the compiler (structured literals) and by CP16
  Host ops (`inject`, `@json`, `read_val`).

## Commits

Local, on `runtime-2-cp15-dev` (base `c3b0046`): natives; tests, probe,
mutants, guards; this record and evidence.

## Evidence

`../evidence/cp15/`: `unit-diff-probe.txt`, `diff-seeds.txt`, `sanitize.txt`,
`guards-check.txt`, `guards.md`, `mutants.txt`.
