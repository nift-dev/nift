# CP14 — Built-ins batch A: collections and strings (design and first families)

Status: development (`runtime-2-cp14-dev`, local, not pushed). **Base: CP12
work in progress `0bd5f06`** (D-017 pipelining: CP12 is still changing its
compiler, inference and VM in its own worktree; nothing here edits those
files). Checkpoint row (`../checkpoints.md`): "Built-ins batch A: collections
and strings — gate: batch fallback = 0; per-builtin instruction guards".

This record covers the work that is independent of CP12's in-flight changes:
the inventory of legacy's collection and string built-ins, the intended
single semantics of each (D-018), the native interface, three families
implemented as pure natives over `rt::Value` with unit, differential and
sanitizer tests, and the plan for wiring them into the compiler and VM once
CP12 certifies. The wiring itself is **not** done.

Round 2 (coordinator, 2026-10-18): family F6 (conversion and formatting)
implemented to the same standard; per-native instruction guards measured with
callgrind and locked (owner addition); natives proven free of legacy-value
conversion (link test).

## Objective

1. Every collection and string built-in legacy provides is listed, grouped
   into families that migrate independently, with its legacy behaviour, its
   intended VM behaviour (one meaning everywhere, D-018), the kind rules the
   compiler needs and an instruction-guard target.
2. Families are implemented as natives that take typed `rt::Value`
   arguments — no source text, no re-parsing, no string conversion — honour
   the CP09 reference contract and keep the live-heap total exact.
3. Legacy behaviour that differs by evaluation context or by expression shape
   is not reproduced: it is recorded as a D-015 candidate and the compiler
   refuses those shapes until the owner decides (D-014 class 2/3).

## Scope

New files only: `src/rt/natives/*` (natives, table, support),
`tests/rt/builtins_{unit,diff}.cpp`, `tests/rt/builtins_support.h`,
`tests/runtime2/builtins/*` (legacy context/shape probe, mutation check),
additive Makefile targets `test-rt-builtins`, `test-rt-builtins-sanitize`,
`test-rt-builtins-probe`. CP12-owned files (`compiler*`, `infer*`, `vm*`,
`vm_ops*`, `native.*`, policies, `script_runner`) are untouched; the natives
link only against the CP09 value model (`value`, `string`, `aggregate`,
`atom`, `heap_account`, `compare`, `number`, `decimal`, `render`, `error`,
`handle`, `legacy_bridge`).

## Where legacy implements these built-ins

Legacy has no single built-in table. The same method is implemented in up to
five places, and which one runs depends on the evaluator (canonical vs
prepared/loop), on the receiver's *shape* (a bare binding, a pure JSON path,
any other expression) and even on the expression's source length:

| Path | Source (search for) | Used when |
| --- | --- | --- |
| Shared read-only layer | `Parser::complete_readonly_method` | first, in both evaluators: `length empty split trim to_upper to_lower` on strings, `size length empty first last` on arrays, `size empty` on objects |
| JSON-path fast path | `resolve_json_value(receiver` ("Fast path: read-only container methods on a pure JSON-path receiver") | canonical, receiver is a binding or member path: `size length empty first last keys values entries` |
| Canonical value families | `evaluate_value_family_1` … `_4` in `ParserExpression.cpp` | canonical, everything else: string, number, object and array families |
| Binding array methods | `evaluate_array_method` (`rb->value->array`) | receiver is a bare array binding: the mutating methods and a second copy of the read-only and callback methods |
| Prepared native methods | `c.native_method` in `ParserTemplate.cpp` | loop code after the shared layer: its own `to_string to_int to_double abs floor ceil round length trim to_lower to_upper size length empty first last replace split`; otherwise `method_fallback` re-evaluates the call textually |
| Pure string plans | `prepare_pure_string_plan` / `evaluate_pure_string_plan` | expressions of only strings, `+`, `split`/`first`/`last`; the "large" variant (source > 8 KiB containing `.replace(`) also `trim trim_start trim_end to_upper to_lower replace` |
| Free functions | `call_args("min"` … `"range"`, `"html_escape"`, `"type"`, `is_*` | canonical call dispatch |

## Inventory

107 entries in batch A's scope, in eight families. Implemented as natives in
this checkpoint: F1, F2, F3, F6 (73 table entries for 71 built-ins:
`to_string` has one entry per receiver kind). Error messages are legacy's exact
text; `<m>` is the method name.

### F1 String methods (receiver String) — implemented (18)

| Method | Args | Result | Legacy errors (besides arity) |
| --- | --- | --- | --- |
| `length()` | — | int: code points (legacy UTF-8 rule: lead C2–F4, continuation bytes; no overlong/surrogate check) | `length: invalid UTF-8` |
| `empty()` | — | bool | — |
| `split(d)` | string | array of strings; empty fields kept; `d == ""` splits code points | `split: expected one delimiter`, `split: delimiter must be a string`, `split: invalid UTF-8` |
| `trim()` | — | string; strips `" \t\r\n"` only | — |
| `trim_start()`, `trim_end()` | — | string; strip every C-locale `isspace` byte (adds `\v`, `\f`) | — |
| `to_upper()`, `to_lower()` | — | string; ASCII only | — |
| `index_of(s)`, `last_index_of(s)` | string | int: **byte** offset or −1 | `<m>: expected one string`, `<m>: argument must be a string` |
| `contains(s)`, `starts_with(s)`, `ends_with(s)` | string | bool | as above |
| `replace(a, b)` | strings | string; all non-overlapping matches, left to right | `replace: expected old and replacement strings`, `replace: arguments must be strings`, `replace: old string must not be empty` |
| `substr(pos[, len])` | sizes | string; **byte** offsets, `pos` clamped | `substr: expected pos and optional length`, `substr: invalid pos`, `substr: invalid length` |
| `to_string()` | — | the string | — |
| `to_int()` | — | int (exact beyond 2^53); one leading `+` stripped, then `from_chars` (so `"+-5"` is −5) | `to_int: invalid integer` (empty, `"+"`), `to_int: invalid or out-of-range integer` |
| `to_double()` | — | number (`strtod`, C locale) | `to_double: invalid number` (empty, leading/trailing space, `0x`/`0X` anywhere), `to_double: invalid or out-of-range number` (ERANGE incl. underflow, partial parse, inf/nan) |

Arity errors: `<m>: expected no arguments` for the zero-argument methods.
Not string methods (legacy: `<m>: unsupported for this value` for names in
legacy's known-method list, `unknown value or malformed expression: <text>`
otherwise): `size first last join replace_once …` — left to the compiler's
existing refusal (diagnostic parity is CP13).

### F2 Array read-only methods and free collection functions — implemented (21)

| Built-in | Args | Result | Legacy errors |
| --- | --- | --- | --- |
| `size()`, `length()` | — | int | — |
| `empty()` | — | bool | — |
| `first()`, `last()` | — | element | `<m>: array is empty` |
| `indexOf(v)` | any | int or −1 (structural equality) | `indexOf: expected one value` |
| `contains(v)` | any | bool | `contains: expected one value` |
| `join(sep)` | string | string; elements rendered as by `print` | `join: expected separator`, `join: separator must be a string`, `join: elements must be renderable scalar values` |
| `slice(start[, end])` | sizes | array; clamped, `end < start` → empty | `slice: expected start and optional end`, `slice: invalid start`, `slice: invalid end` |
| `unique()` | — | array; first of each structurally equal group | — |
| `flatten()` | — | array; one level | — |
| `sum()` | — | number; **double** accumulation, `[]` → 0 | `sum: values must be numeric` |
| `min()`, `max()` | — | element; numbers by exact value, strings bytewise; the first element is taken unchecked (`[true].min()` is `true`) | `<m>: cannot aggregate an empty array`, `<m>: values must be comparable and homogeneous` |
| `take(n)`, `drop(n)` | size | array | `<m>: expected one count`, `<m>: count must be a non-negative integer` |
| `chunk(n)` | size > 0 | array of arrays | `chunk: count must be a positive integer` |
| `from_entries()` | — | object from `{key, value}` objects (exactly those two members); keys: strings as-is, numbers/bools rendered | `from_entries: each entry must be an object containing exactly key and value`, `from_entries: generated key must be string, number, or bool`, `from_entries: duplicate generated key '<k>'` |
| `min(…)`, `max(…)` (free) | values, or one array | the selected value | `<m>: expected at least one value`, `<m>: values must be comparable and homogeneous` |
| `range(stop)`, `range(start, stop[, step])` | int64s | array of numbers (stored as **doubles**) | `range: expected stop, start/stop, or start/stop/step`, `range: arguments must be signed 64-bit integers`, `range: step must not be zero`, `range: result exceeds 10000000 items`, `range: integer overflow` |

"size" arguments follow legacy `runtime_number_to_size` (non-negative,
integral, below 2^64 as a double or at most SIZE_MAX exactly; `-0.0` is 0).

### F3 Object methods (receiver Object) — implemented (11)

| Method | Args | Result | Legacy errors |
| --- | --- | --- | --- |
| `size()`, `empty()` | — | int / bool | — |
| `keys()`, `values()`, `entries()` | — | arrays (entries: `{key, value}` objects), insertion order | `<m>: expected no arguments` (but see Q10) |
| `has(k)` | string | bool | `has: expected key`, `has: key must be a string` |
| `get(k[, default])` | string, any | member, default or null; the default is evaluated **only when the key is missing** | `get: expected key and optional default`, `get: key must be a string` |
| `merge(o)` | object | object; replaced keys keep their position | `merge: expected one object`, `merge: argument must be an object` |
| `merge_deep(o)` | object | recursive merge of nested objects, ≤ 64 levels | as merge, plus `merge_deep: maximum merge depth exceeded` |
| `pick(keys)`, `omit(keys)` | array of strings | object; a key containing `*` or `?` (unescaped) is a glob (legacy `glob_component_match`: a name starting with `.` matches only a pattern starting with `.`) | `<m>: expected one array of keys`, `<m>: keys must be an array`, `<m>: keys must be strings` |

### F4 Array mutating methods — design only (9)

`push(v)` (returns `v`), `pop()`, `insert(i, v)` (returns `v`), `remove(i)`
(returns the element), `clear()` (null), `splice(start, count[, array])`
(returns the removed elements), `reverse()` (returns the array), `sort()`
(numbers or strings, else `sort: incomparable values`), `sort(comparator)`.
Errors: `cannot mutate const array: <name>`, `<m>: array is empty`,
`insert: index out of range`, `remove: index out of range`, … Legacy accepts
them **only on a bare array binding**: `[1].push(2)` and `h.k.pop()` are
`unknown value or malformed expression` (probe `mut.push.lit`,
`mut.pop.member`). CP11 already compiles `push`. These need CP12's place
model (in-place mutation of a binding's array, aliasing and location
references), so they are wired as VM instructions on a place, not as pure
natives.

### F5 Callback (higher-order) array methods — design only (17)

`map filter reduce any all find find_index count sort_by group_by index_by
partition unique_by min_by max_by count_by group_by_each` (and `sort` with a
comparator, F4). Callbacks must be callables (`transformation: callback must
be callable`); predicates must return bool (`<m>: callback must return
bool`); `sort_by` takes one selector or selector/`'asc'|'desc'` pairs whose
directions must be quoted literals; keys must be scalar and homogeneous.
Legacy implements them twice (value family 4 and the binding path) with
different rules (the binding path's `sort_by` has no direction pairs and
orders mixed key kinds by type rank). They need the VM to call back into
compiled code (re-entrant `Call` from a native, the frame budget, error
frames "collection callback"), i.e. CP12's call path and lambdas.

### F6 Conversion and formatting — implemented (21 built-ins, 23 entries)

| Built-in | Receiver / args | Result | Legacy errors |
| --- | --- | --- | --- |
| `to_string()` | number, bool, null (strings: F1) | string: numbers as legacy `dump` (integral doubles in int64 range as integers, others shortest round trip in C++ `general` form: `1e+21`, `1e-07`; exact big integers by digits), `true`/`false`, `null` | `to_string: expected no arguments`; arrays/objects: `to_string: use stringify() for composite values` (not bound: a refusal, Q12) |
| `stringify()`, `highlight()` | any plain value | string: compact JSON (`,` and `:`, Jsonic++ escaping: `\" \\ \b \f \n \r \t`, other bytes < 0x20 as `\u00xx` lower-case, all other bytes raw) | `bytes values are not serializable`, `value serialization depth exceeded` (> 128 nested levels) |
| `prettify()` | any plain value | string: two-space indent, one element per line, `": "` after keys, empty containers `[]`/`{}` | as stringify |
| `html_escape(v)`, `attr_escape(v)` | string, number, bool, null (rendered) | `& < >` escaped; attr also `"` → `&quot;`, `'` → `&#39;` | `<f>: expected one value`, `<f>: value must be a string or scalar` |
| `url_encode(v)` | as above | RFC 3986 unreserved bytes kept, others `%XX` upper-case | as above |
| `type(v)` | any | `null bool int float string array object bytes function struct error` (`int`/`float` by value: `type(2.0)` is `int`) | `type: expected one value` |
| `is_null … is_enum(v)` (13) | any | bool: `public_type(v)` equals the category (`is_number`: int or float) | `<pred>: expected one value` |

`highlight()` only changes the REPL's terminal display; as an expression it
is the compact serialization (legacy's `complete_presentation` ignores the
flag). Arity errors of the presentation methods are not legacy's message
(`o.stringify(1)` is `unknown value or malformed expression`), so W1
refuses them. `type`/`is_*` on handles (timer, file, future, mutex, atomic,
collection, enum) wait for CP17 (the natives report `Internal`).

### F7 Collection handles — deferred to CP17 (10)

`stack() queue() set() sorted_set() map() sorted_map() prique()` and their
methods (`push pop top front add contains remove get set keys …`). They are
`"\x1fnift:collection:"` handle strings in legacy; they move with typed
handles (CP17).

Numbers (`abs floor ceil round to_int to_double` on numbers), bytes
(`encode decode`, bytes methods) and JSON are batch B (CP15); file/stream
string methods (`replace_once insert_before …`) are batch C (CP16).

## Legacy quirks found (D-015 candidates)

Found by `tests/runtime2/builtins/context_probe.py` (each probe in six
contexts: top-level declaration, top-level argument, loop body declaration and
argument, function called from top level and from a loop; plus pairs of
expressions that must mean the same) and by reading the paths above. None is
reproduced by the VM; the "VM handling" column is the wiring rule.

| # | Legacy behaviour | Kind | Intended single semantics (proposal) | VM handling |
| --- | --- | --- | --- | --- |
| Q1 | `trim()` strips `" \t\r\n"`; `trim_start()`/`trim_end()` strip all `isspace` (also `\v`, `\f`), so `w.trim() != w.trim_start().trim_end()` | family inconsistency | one whitespace set for all three | natives reproduce legacy (`pair.trim.forms` pins it); owner decision |
| Q2 | `length()` counts code points but `index_of`/`last_index_of`/`substr` use byte offsets; `substr` can split a UTF-8 sequence (`"été".substr(1, 1)` is an invalid byte) | family inconsistency | one unit (code points) for every string index | reproduced (`pair.substr.units`); owner decision |
| Q3 | `o.length()` on an object: error at top level and in functions, member count in loop code | **context** (CP12 #8) | error everywhere (`size()` counts) | not bound (`find_builtin(Object, "length")` is null); compiler refuses as today |
| Q4 | `x.to_double()` on a number: `to_double: unsupported for this value` at top level, identity in loop code | **context** | identity everywhere (or error everywhere) | number methods are CP15; refuse until decided |
| Q5 | Inside an expression longer than 8 KiB that contains `.replace(`, `trim()` strips every `isspace` byte (legacy's "large pure string plan"), elsewhere only `" \t\r\n"` | **shape** (source length) | as Q1 | the VM has one `trim`; the difference is approved only with a recorded change (policy needs the source length as evidence) |
| Q6 | `sum()` adds nearest doubles: `[2^53+1, 1].sum()` is 9007199254740992 while `a[0] + a[1]` is exact (…994); no overflow error | family inconsistency | exact checked arithmetic as `+` | reproduced (`pair.sum.exact`); owner decision |
| Q7 | `range()` stores `(double)x`: `range(2^53+1, 2^53+2)` is `[9007199254740992]` | inexact | exact integers | reproduced (`pair.range.exact`) |
| Q8 | arrays spell it `indexOf`, strings `index_of`; `a.index_of(x)` is `unsupported for this value` | naming | one spelling (alias both) | reproduced (not bound) |
| Q9 | `pick`/`omit` treat any key containing `*`/`?` as a glob, with the filesystem dot-file rule (`"*"` does not pick `.h`) | semantics | exact keys by default, explicit glob opt-in | reproduced |
| Q10 | `keys()`/`values()`/`entries()` ignore extra arguments when the receiver is a binding or member path (JSON-path fast path) and are an arity error otherwise | **shape** | arity error everywhere | compiler refuses wrong arity (W1); `pair.keys.arity` pins it |
| Q11 | Argument validation is lazy: `s.substr(bad, f())` never calls `f()`; `o.get(k, d)` evaluates `d` only when `k` is missing; `range(a, b, c)` stops at the first bad argument | evaluation order | arguments evaluated once, left to right, before the call (D-018 exactly-once) | W2/W4: admitted only where the difference cannot be observed |
| Q12 | Unknown method diagnostics differ by receiver kind and by legacy's "known method" list (`unsupported for this value` vs `unknown value or malformed expression: <text>`) | diagnostics | one message | CP13 (refused until then) |
| Q13 | Mutating methods work only on bare bindings (`[1].push(2)`, `h.k.pop()` fail) | shape | any place | F4 design |
| Q14 | A chain of presentation methods composes over the original value at top level and in functions called from top level (`o.stringify().stringify()` is `o.stringify()`, `o.prettify().stringify()` is pretty), but each call serializes its receiver in loop code (the second `stringify` JSON-encodes the string) | **context** | each call serializes its receiver (ordinary method semantics) | W6 `builtin-presentation-chain`: a presentation call whose receiver is a presentation call is refused |

The probe's pinned inventory (`tests/runtime2/builtins/context_dependent.txt`)
lists Q3, Q4, Q14 (context) and the shape pairs for Q1, Q2, Q5, Q6, Q7, Q10;
`make test-rt-builtins-probe` fails when legacy's inventory changes.

Everything else probed (224 cases × 6 contexts: all F1–F5 methods, receiver
shapes `h.s`, `h[0]`, `(s + "x")`, chains, calls, computed and large
arguments) behaves the same in every context.

## Native interface (`src/rt/natives/builtin.h`)

```cpp
enum class Status : uint8_t { Ok, Error, Internal, Memory };
struct Context { std::size_t heap_limit; bool over_budget() const; };
using NativeFn = Status (*)(const Context&, const Value* args, uint32_t argc, Value& out, std::string& error);
struct BuiltinSpec { BuiltinId id; string_view name; Receiver receiver; uint8_t min_args, max_args;
                     KindSet result; ResultRule rule; bool may_fail; uint32_t guard; NativeFn fn; };
const BuiltinSpec* find_builtin(Receiver, string_view name);   // compile time
Status call_builtin(const BuiltinSpec&, const Context&, const Value*, uint32_t, Value&, std::string&);
```

* **Typed arguments.** `args[0]` is the receiver of a method; arguments are
  `rt::Value`s in VM registers. No source text reaches a native; there is no
  quoted-argument or textual re-evaluation path.
* **Binding by integer id.** `BuiltinId` (X-macro `builtin_list.inc`) is the
  compile-time binding (architecture #5); the VM never looks up a name.
* **Errors.** `Error` + legacy's message → CP12 `raise(fault,
  StopReason::Error, message)` at the call's source offset (`site(node)`, the
  start of the method-call expression, which is where legacy reports these
  errors: verified for top level, `print` arguments and nested operands).
  `Internal` (a receiver kind the compiler promised away) → `internal(fault,
  …)`, exit 70, never guessed. `Memory` → `memory_exceeded(fault)`.
* **Arity.** Each native checks its own argument count with legacy's
  per-function message; `min_args`/`max_args` are for the compiler (W1).
* **Reference contract (CP09).** Pure natives never mutate their arguments
  and never hold a reference into an argument across a mutation of a value
  that may share its storage. Results are built in fresh values and stored
  into `out` last, so `out` may be the receiver's register (tested). Where a
  result starts as a copy of an argument (`merge`, `merge_deep`), the copy is
  unshared *before* any reference into the other argument is taken, because
  `o.merge(o)` passes the same object twice and `unshare` relocates the shared
  original in sanitizer builds. Elements copied out of a container are `Value`
  copies (O(1), refcounted), never references.
* **Exact heap accounting.** Every heap object comes from CP09 constructors;
  arrays are built with `ArrayBuilder` (one `array_mut` on a private fresh
  array, `heap_retally` every 1,024 elements and at the end). Unit tests check
  `live_heap_bytes()` equals the summed footprint of every live object after
  each family's calls, and returns exactly to its start.
* **Memory budget.** Natives whose result can outgrow their inputs
  (`split("")`, `range`) check `Context::heap_limit` every 1,024 elements, so a
  single call cannot allocate far beyond the VM's budget before the VM's
  post-instruction check.
* **Copy avoidance.** Unchanged results share storage (`trim()` of an
  already-trimmed string, `replace` without a match, `substr(0)`, `get`,
  `first`); `unique` uses CP09 hashes (equal ⇒ same hash) instead of legacy's
  O(n²) scan.

## Kind rules for CP12's inference (`KindSet`, `ResultRule`)

`KindSet` uses CP12 `rt/infer.h`'s bit layout (null, bool, int, frac, string,
array, object), duplicated so CP14 does not include an in-flight header; the
wiring patch `static_assert`s equality.

| Rule | Built-ins | Result kinds |
| --- | --- | --- |
| Fixed | counts and indexes (`length size index_of last_index_of indexOf to_int`) | int |
| Fixed | predicates (`empty contains starts_with ends_with has`) | bool |
| Fixed | string results (`trim* to_* replace substr join to_string stringify prettify highlight html_escape attr_escape url_encode type`) | string |
| Fixed | `is_*` predicates | bool |
| Fixed | `split keys values entries flatten chunk range` | array (elements: string / string / any / object / any / array / int) |
| Fixed | `sum to_double` | int \| frac |
| Fixed | `from_entries pick omit` | object |
| ElementOf | `first last min max` (array) | the receiver's element kinds |
| ElementsOf | `slice unique take drop` | array of the receiver's element kinds |
| MemberOf | `get` | the receiver's member kinds ∪ the default's kinds (∪ null without a default) |
| Merged | `merge merge_deep` | object (members: both sides) |
| Selected | free `min max` | the arguments' kinds, or the single array argument's element kinds |

Receiver rule: a method is bound only when the receiver's inferred kinds are
exactly one of String, Array, Object, Number (int ∪ frac), Bool, Null (no
Unknown); the presentation methods (`Receiver::Any`) accept any kind set
without Unknown. Argument kinds outside
what a native accepts are not refused: the native reports legacy's message
(`may_fail`).

## Wiring plan (after CP12 certifies)

A patch sketch for steps 1–2 against `0bd5f06` (`native.h`, `vm.cpp`,
`compiler_call.cpp`) was written outside the branch (the coordinator's
scratch area, `cp14-wiring.patch`), because CP12 files are not edited here;
it is a starting point, to be redone on CP12's certified tip. Steps, each
with its CP12 dependency:

1. **Dispatch** — extend `CallNative` (`opcodes.h`: `a` = base register of the
   window, `b` = id, `c` = argc) with ids above `NativeId::Print` mapping to
   `BuiltinId`; `Vm::native` (`vm.cpp`) calls `call_builtin(spec,
   Context{heap_limit_}, &R[in.a], in.c, R[in.a], fault.message)`, maps
   `Status` to `raise`/`internal`/`memory_exceeded` (`vm_ops.h`, `vm.cpp`),
   then clears `R[a+1 .. a+c)`. Depends on: `NativeId`, `CallNative`
   operands, `Fault`, `raise`, `internal`, `memory_exceeded`, `heap_limit_`.
2. **Methods** — `UnitCompiler::method_call` (`compiler_call.cpp`): replace
   the `length`/`size` special case by `find_builtin(receiver_of(kinds(
   callee.a)), name)`; receiver in the first register of the window, then
   the arguments; `fb_->set_offset(site(node))`. Keep the `Length` opcode for
   `length()`/`size()` on proven strings/arrays (it is the fast path; the
   guard compares both). Depends on: `kinds()`, `kinds_within()`, `site()`,
   register window helpers (`temp`, `expr_to`, `release_to`).
3. **Free functions** — `UnitCompiler::call`: `min`, `max`, `range` resolve
   to `find_builtin(Receiver::None, name)` when no user function, parameter or
   binding has the name; the D-009#3 shadowing rule and `reserved_name`
   (`native.cpp`, `legacy_names.cpp`) stay authoritative.
4. **Inference** — `infer_types.cpp`: a method-call / native-call node's type
   from the spec's `ResultRule` and the operand types (element and member
   types already exist in CP12's structural types). Depends on the
   structural type representation and the `kKind*` bits (static_assert).
5. **Alias analysis** — `alias.cpp` currently treats every method other than
   `length()`/`size()` as possibly mutating its receiver; bound pure natives
   are read-only (no `escaping` mark), and their results are fresh values
   (legacy: `b := a.first(); b.push(9)` leaves `a` unchanged — probed).
6. **Refusal rules** (new reasons, all before execution):
   * W1 `builtin-arity`: static argument count outside `[min, max]` (legacy's
     arity checks happen before or between argument evaluations, and Q10).
   * W2 `builtin-lazy-args`: a `may_fail` built-in with more than one
     argument whose later arguments are not literals or local reads (Q11:
     legacy may skip them).
   * W3 `builtin-receiver`: receiver kinds not exactly one receiver family
     (String, Array, Object, Number, Bool, Null; Any for presentation), or
     the method not in that receiver's family.
   * W4 `builtin-default`: `get(k, d)` with `d` other than a literal or local
     read (Q11).
   * W5 the context-dependent built-ins (Q3, Q4) are simply not bound.
   * W6 `builtin-presentation-chain`: `stringify`/`prettify`/`highlight`
     on a receiver that is itself a presentation call (Q14).
   * F4 methods stay `method:<name>` refusals until the place model lands;
     F5 until native-to-compiled callbacks exist.
7. **Statistics and suites (D-016)** — a supported-subset suite
   `tests/runtime2/vm_builtins.py` (every F1–F3 built-in, every receiver shape
   that is not refused, in all five CP12 contexts; 100 % on the VM, 0
   differences), an unsupported suite (each W-rule and Q-shape falls back
   with its reason), the generated suites extended with built-in calls, and
   the CP12 context-consistency suite extended with F1–F3 bodies.
8. **Gate** — "batch fallback = 0": no supported-suite or corpus program
   falls back with a `builtin*`/`method:*`/`callable:*` reason naming an
   F1–F3 built-in, except the documented W-rules. "Per-builtin instruction
   guards": see below.

## Wiring (speculative integration, branch `spec-integration`)

Done on `spec-integration` (local, never pushed), **speculative, base CP12
`6c144bb`** (CP12 tip under certification: `runtime-2` @`8957559` + the CP12
commits incl. the CP09 memcpy fix `547614f`). Integrated by the single
integration owner after CP12 freezes; disposable until CP12 certifies.

| Step | Where | What was done |
| --- | --- | --- |
| 1 Dispatch | `native.h` (`kFirstBuiltin = 1`), `vm.cpp` (`Vm::native` default), `vm_builtin.{h,cpp}` (`Vm::builtin`) | `CallNative a=window base, b=kFirstBuiltin+BuiltinId, c=window size` (receiver first). `Ok` → result into `R[a]`, the window's other registers cleared; `Error` → `raise(StopReason::Error, message)` at the call's offset (legacy message, exit 1); `Internal` → `StopReason::Internal` "unexpected operands (compiler defect): built-in <name>" (exit 70); `Memory` → the VM's memory stop (Internal "memory budget exceeded", or the recursion error beyond 64 frames), never a retry: the partial result is already freed, so `memory_exceeded`'s "still within the budget" early return is not used. Off the hot path (CallNative was already a cold opcode). |
| 2 Methods | `compiler_call.cpp` `method_call`, `builtin_call` | `length()`/`size()` without arguments keep the Length opcode and its CP12 checks (`type:length`, `context:object-length` = Q3). Every other method binds through `builtin_bind.{h,cpp}` (`bind_method`): receiver family from the inferred kinds (W3); the window is a fresh register range, receiver and arguments evaluated once, left to right (D-018). |
| 3 Free functions | `compiler_call.cpp` `call` | `range min max html_escape attr_escape url_encode type is_*` bind when no user function has the name (a user function, parameter or binding of a reserved name is refused earlier: `fn-name-reserved`, `param-name-reserved`, `binding-name-reserved`, `call-binding`). The CP12 textual-argument evidence rule (prepared parser rejects an argument → the call is textual) applies to them as to user calls. |
| 4 Inference | `infer.cpp` (`call`), `infer_builtins.{h,cpp}` | Result types from `ResultRule` with CP12's structural types: `split`/`keys` array of string, `values` array of the members, `entries` array of `{key: string, value: members}`, `chunk` array of arrays of the elements, `range` array of int, `pick`/`omit` the receiver's object type, `first last min max` the elements, `slice unique take drop` array of the elements, `get(k[, d])` the member (literal key) or all members ∪ the default or null, `merge`/`merge_deep` the union of both object types, free `min`/`max` the arguments (one array argument: its elements). Arrays/objects whose contents no rule describes (`flatten`, `from_entries`) get an *open* type (container kind, `any`: every kind at every depth below), never "no members". Unbound methods are Unknown (the compiler refuses them); a receiver with empty kinds (never executed) gives the empty type. `static_assert`: the CP14 `KindSet` bits equal CP12's `kKind*`. |
| 5 Alias analysis | `alias.cpp` | Every CP14 method name is read-only (`pure_method_name`): a parameter used as a built-in receiver does not escape. Results are fresh values (legacy probe: `b := a.first(); b.push(9)` and `c := o.get("k"); c.push(2)` leave `a`/`o` unchanged, both engines). |
| 6 Refusals | `compiler_call.cpp` | `builtin-arity:<name>` (W1, Q10), `builtin-lazy-args:<name>` (W2: a later argument of a fallible built-in that is not a literal or a local read), `builtin-default:get` (W4), `builtin-receiver:<name>` (W3: mixed/Unknown kinds, or the family lacks the method: `s.keys()`, `a.index_of()` = Q8, `n.to_double()` = Q4), `builtin-presentation-chain` (W6, Q14; currently unreachable because a method call on a call result is refused first as `postfix-receiver`, kept as defence in depth), `method:<name>` for F4/F5 and batch B/C names. |
| 7 Suites | `tests/runtime2/vm_builtins.py` (in `test-rt-vm` and `test-rt-vm-sanitize`), `vm_subset_gen.py` | Supported: 7 bodies × 5 contexts (top, `if`, loop, function from top, function from loop) covering every bound built-in and the receiver/argument shapes (binding, member path, index, nested built-in arguments), 100 % on the VM, identical to legacy; errors: 19 legacy messages at top level, byte-identical; unsupported: 18 refusals with their reasons, refused before execution; budget: `range`/`split("")` under a lowered heap budget stop with exit 70. The supported-subset generator emits built-in calls (int, string, bool and array results, error-free by construction) in every context. |

Not wired, refused (unchanged reasons): F4 mutating methods other than the
CP11 `push` statement, F5 callbacks, F7 handles, batch B/C names, method
calls on call results (`postfix-receiver`: `s.trim().length()`,
`o.stringify().stringify()`); widening that is a separate step (the CP10
tree and legacy's textual reading of chains must be shown to agree first).

Interface mismatches met (all resolved in the wiring, natives unchanged):

* The sketch's `call_builtin(..., R[in.a], fault.message)` wrote the result
  straight into the receiver's register and the message into the fault; the
  wiring uses a local `out`/message (no aliasing reliance on argument 0 and
  no partially written fault on `Ok`).
* `memory_exceeded` returns "ok" when the live heap is back under the limit,
  which is always the case after a native returned `Memory` (its partial
  result is freed): it cannot be used as the sketch did; the stop is explicit.
* CP12's inference returned `int` for every method call and its `Ty` had no
  "object with unknown members"; the open type uses the existing `any` flag
  with the container kind instead of a new field.
* CP12 has `kinds()` widening Unknown and a probe mode (`unknown_defers`):
  the receiver rule defers in probe mode like every CP12 kind check.
* `kFirstBuiltin` lives in `native.h` (the sketch put it there too); the
  binding helpers are a separate cold file so `infer.cpp`/`vm.cpp` stay
  under rt-lint's 400-line limit.

### Wiring results (2026-10-19, `nice make -j1`, binary of this commit)

| Gate | Result |
| --- | --- |
| `make test-rt-builtins` | unit 635 checks / 0 failures; differential seed 20261018, 5,000 cases (4,979 compared, 21 refused by W1) / 0 failures; isolation PASS; rt-lint PASS; context probe PASS (inventory pinned) |
| `make test-rt-builtins-sanitize` | unit 634, differential 1,994: 0 failures (ASan+UBSan) |
| `make test-rt-builtins-guards` | 25 workloads, 0 guard failures |
| `vm_builtins.py` | 35 supported (7 bodies × 5 contexts) on the VM, identical to legacy; 19 error programs byte-identical; 18 refusals; 2 budget stops; 0 failures |
| `make test-rt-vm` steps | vm-unit, prepared-parser port (100,136), rt-lint, legacy names: PASS; `vm_parity` PASS (rt-diff 815 entries, 0 differing, 7 approved; corpus); adversarial 37,442 programs, 0 non-approved (168 approved); `vm_subset.py` 49 supported / 65 unsupported PASS; unsupported generator 500/500; engine selection, former replay, D-018, context consistency, policy negatives (52/5), review reproducers (87), golden (108), int bounds (2,682): PASS. **Supported generator: seed 1 499/500, seed 2 499/500** — the one refusal in each (`context:assignment-type`, `context:assignment-error`) reproduces on the CP12 binary with every built-in call removed from the program: a pre-existing CP12 compiler-vs-subset-spec gap exposed because the extended generator draws a different random stream (recorded, not hidden by changing inputs). |
| `make test-rt-vm-mutants` | unmutated passes, 8/8 mutants rejected |
| callgrind (`vm_compare.py --groups ops,fib`) | flat call 425, fib 285/call (top level and in a loop), loop 109, add_local 183, if_branch 244: unchanged |

Coverage (same harness and seed before/after; "before" = the CP12 binary of
this base plus feature-serve): shared corpora 104 of 788 distinct scripts on
the VM (84 `vm` + 20 `vm-error`) before and after, identical outcome
distribution — every corpus script calling an F1–F3/F6 built-in also uses a
construct still refused (F4/F5 methods `sort_by` 19, `map` 9, `sort` 2;
`postfix-receiver` chains 14; imports, structs, lambdas, …). General generator
(`vm_gen.py`) 72 of 300 before and after: it emits no built-in calls. The
built-in coverage is carried by `vm_builtins.py` and the supported generator
(built-in calls in ~every program, 100 % on the VM apart from the two CP12
cases above). No corpus program falls back with a `builtin*` reason.

### Generator narrowing and the known-refused list (coordinator ruling, 2026-10-19)

The two supported-generator refusals are a CP12 kind-inference gap (D-027:
non-blocking, the unit runs on legacy; CP12 is frozen, so inference is not
changed here). Minimal shapes (delta-reduced from the generated programs):

* A, `context:assignment-error`: `w := 2.5` … `w = w * 1.5` (top level);
  in a loop `v := w` then `v += 1`.
* B, `context:assignment-type`: `w := 0.25 + 42` (top level); in a loop
  `v := (w) - 2` then `v = v * 1.5`.

In both, `v` is always double at run time (legacy agrees: 3.5, 60.375), but
the compiler proves "always double" only for declaring texts with a double
literal. **Deliberate narrowing of the subset spec, not an input change to
hide a failure:** `vm_subset_gen.py` no longer generates `=` or a compound
assignment, in loop-reachable code (loop bodies, functions), whose target is
a double binding declared from a text that is double only through another
binding (`NARROWED` in the generator; counted as feature
`narrowed:double-via-binding-assign`: 6 statements in 500 programs at
seed 1, 3 at seed 2). The narrowing is slightly broader than the compiler's
refusal (shape B inside one function, or A without the reassignment of `w`,
compile). Evidence kept: `tests/runtime2/vm-known-refused/` (both minimal
shapes and both generated programs) and `vm_known_refused.py` (in
`test-rt-vm`) assert they are still refused with their reasons; when the
inference widens that test fails and the narrowing must be removed.

**Backlog:** widen kind inference: parenthesised double operands in
assignment (always-double through a binding reference, shapes A and B).

## Instruction guards and allocation scaling (measured)

Owner addition (round 2). `tests/rt/builtins_bench.cpp` runs 25
representative workloads; `tests/runtime2/builtins/builtins_guards.py` runs
it under callgrind (D-005: deterministic instruction counts, valid on the
shared host; one valgrind process per size, sequential, ~30 s) at n = 1, 100,
10,000, 100,000. Only the operation is counted (`CALLGRIND_ZERO_STATS` /
`CALLGRIND_DUMP_STATS_AT` around the calls; inputs are built outside).
Allocations are counted with a replaced `operator new` around the same calls,
and the live-heap delta with the exact heap accounting.

Guard: Ir per call at every size, locked in
`tests/runtime2/builtins/builtin_guards.json`; `make test-rt-builtins-guards`
fails above +10 % (+50 Ir) or when a scaling exponent grows by more than
0.15. The n = 1 values (rounded up to 10) are also the `guard` column of
`builtin_list.inc`; natives without a workload keep design targets there.
Callbacks over large arrays (F5) get guards once wired.

| workload | Ir/call n=1 | n=100 | n=10,000 | n=100,000 | exponent | allocs/call n=10⁴ / 10⁵ |
|---|---:|---:|---:|---:|---:|---:|
| any.prettify (records) | 3,781 | 167,224 | 17,988,241 | 177,574,367 | 0.99 | 17 / 20 |
| any.stringify (records) | 4,531 | 122,583 | 13,116,102 | 133,340,177 | 1.01 | 16 / 19 |
| arr.contains (miss) | 190 | 7,318 | 720,118 | 7,200,118 | 1.00 | 0 / 0 |
| arr.first | 128 | 128 | 128 | 128 | 0.00 | 0 / 0 |
| arr.join (strings, concat) | 329 | 15,148 | 1,411,537 | 14,139,279 | 1.00 | 12 / 15 |
| arr.join.numbers | 326 | 24,164 | 2,617,022 | 29,372,310 | 1.05 | 13 / 17 |
| arr.max | 183 | 8,309 | 820,109 | 8,200,109 | 1.00 | 0 / 0 |
| arr.slice (n/2) | 511 | 2,621 | 180,732 | 1,800,874 | 1.00 | 2 / 2 |
| arr.take (copy) | 622 | 4,810 | 410,866 | 4,100,860 | 1.00 | 2 / 2 |
| arr.sum | 151 | 2,527 | 240,127 | 2,400,127 | 1.00 | 0 / 0 |
| arr.unique | 1,388 | 12,569 | 1,515,032 | 14,347,666 | 0.98 | 4 / 4 |
| fn.range (append) | 617 | 9,461 | 757,988 | 7,262,337 | 0.98 | 16 / 19 |
| num.to_string (per call) | 1,005 | 982 | 963 | 955 | 0.00 | 0 / 0 |
| obj.get (member lookup) | 235 | 326 | 351 | 357 | 0.01 | 0 / 0 |
| obj.keys | 2,330 | 12,419 | 982,875 | 9,803,230 | 1.00 | 6 / 6 |
| obj.merge (n + 4 keys) | 1,915 | 3,063 | 292,520 | 1,693,074 | 0.76 | 3 / 3 |
| str.index_of | 241 | 265 | 1,298 | 10,421 | 0.91 | 0 / 0 |
| str.length | 152 | 945 | 80,145 | 800,145 | 1.00 | 0 / 0 |
| str.replace (every 10th byte) | 492 | 2,953 | 209,277 | 2,081,894 | 1.00 | 3 / 3 |
| str.split | 1,186 | 21,694 | 1,919,981 | 18,867,140 | 0.99 | 16 / 19 |
| str.substr (slice n/2) | 267 | 494 | 1,159 | 50,653 | 1.64 | 2 / 2 |
| str.to_double (per call) | 981 | 996 | 1,051 | 1,077 | 0.01 | 0 / 0 |
| str.to_int (per call) | 166 | 199 | 223 | 235 | 0.02 | 0 / 0 |
| str.to_upper | 269 | 2,451 | 210,540 | 2,100,582 | 1.00 | 2 / 2 |
| str.trim (copy of n bytes) | 293 | 527 | 10,630 | 100,656 | 0.98 | 2 / 2 |

Findings:

* **No O(n²) path**: every exponent is ≤ 1.05 except `str.substr`, whose
  absolute cost is a `memcpy` of n/2 bytes (1,159 Ir at 5 KB, 50,653 Ir at
  50 KB: glibc switches copy strategy; still linear in bytes, 1 Ir/byte). The
  10,000 → 100,000 exponent is therefore not a quadratic signal there;
  `str.index_of` (memchr-based) is sub-linear for the same reason.
* **Hidden copies / temporaries**: copying natives allocate exactly the
  result (object + buffer: `slice`, `take`, `substr`, `trim`, `to_upper`,
  `replace` 3 with the growth of the built string); builders whose size is
  unknown grow geometrically (O(log n) allocations: `split`, `range`, `join`,
  `stringify`). One defect fixed: `unique` used an `unordered_map` of vectors
  (2,020 allocations for 1,000 elements); it now uses one open-addressing
  table (4 allocations at any size). No native deep-copies an input.
* **Exact heap**: the live-heap delta per call equals the result's footprint
  (unit test `heap_accounting`); conversions and lookups leave it unchanged.
* `+` concatenation, `a[i]`, `a.push(x)` are VM operations (CP11/CP12
  benchmarks), not natives; `join("")` and `range` stand for concat and
  append here.

**No legacy conversion on any path.** `tests/rt/builtins_bench.cpp` links the
natives with the CP09 value model *without* `RuntimeValue.cpp`,
`legacy_bridge.cpp` and `render.cpp`; any call into them is an undefined
reference (negative control: an object calling `to_legacy` fails that link).
`make test-rt-builtins-isolation` also greps the natives for those headers,
`nift::RuntimeValue`, `to_legacy`/`from_legacy` and `append_rendered`. For
this, number rendering (`append_number`) was ported from Jsonic++
`format_number` instead of going through `render.cpp` (which builds a legacy
`RuntimeValue` per double); it is checked against legacy `dump` on 200,000
random doubles and the special values in the unit test.

## Tests

| Suite | Content | Result (this round) |
| --- | --- | --- |
| `tests/rt/builtins_unit.cpp` | every native (F6: escaping, pretty layout, depth limit, number rendering vs legacy on 200,000 doubles) (results, every legacy message, arity), table invariants (ids, unique names per receiver, lookup), internal status for impossible receivers, heap budget (`range`, `split("")` stop with `Memory`), receiver immutability and `out` aliasing the receiver (`o.merge(o)`, `merge_deep`, self-containing arrays, `replace` into its own register), shared results, exact heap accounting over 13 calls | 635 checks, 0 failures (`-O2`); 634 under ASan (the 160 MB `range` limit case is skipped) |
| `tests/rt/builtins_diff.cpp` | random receivers/arguments (strings with multi-byte, `\v`, `\f`, quotes, backslashes, globs; numbers incl. 2^53+1…+3, −0.0, 1e21, 1e-7; nested arrays/objects; numeric-looking strings; wrong kinds and wrong arity); each case also run by the legacy binary as a Nift program; type + stringify or error message compared | round 1 (F1–F3): seeds 20261018, 2, 3, 4 × 5,000: 19,948 compared, 0 differences. Round 2 (F1–F3 + F6): seeds 20261018, 2, 3 × 7,300: **21,821 compared, 0 differences** (79 refused by W1: Q10 and presentation arity) |
| mutation check (`builtins_mutants.py`) | 10 single-line mutants of the natives (trim set, byte offsets, glob dot rule, mixed min/max, exact sum, −0.0 size, trailing split field, merge order, `+` in to_int, unique order) must each make the differential test fail | **15/15 detected** (M1–M10 F1–F3; M11–M15 F6: upper-case `\u` hex, four-space indent, unescaped `'`, fixed-notation doubles, numeric strings as numbers; 2–49 failures each, seed 1, 3,000 cases) |
| ASan+UBSan (`make test-rt-builtins-sanitize`) | unit + differential (2,000 cases) with CP09's relocate-on-mutation reference checks, leaks on | **clean**: 462 + 1,997 checks, 0 failures |
| `context_probe.py` | 224 legacy cases × 6 contexts + 8 pairs; pinned inventory | Q3, Q4, Q14 (3 cases) context-dependent; 6 shape pairs (Q1, Q2, Q5, Q6, Q7, Q10) |
| guards (`make test-rt-builtins-guards`) | 25 workloads × 4 sizes under callgrind vs the locked JSON | 0 guard failures |
| isolation (`make test-rt-builtins-isolation`) | natives link without legacy values; grep | PASS |
| `rt-lint` | `src/rt/natives` | PASS |

## Exit criteria (CP14) and status

| Criterion | Status |
| --- | --- |
| Inventory and family split, legacy and intended semantics, kind rules, guard targets | this record |
| ≥ 2 families as pure natives with unit, differential and sanitizer tests | F1, F2, F3, F6 (73 entries) |
| Wiring into compiler/VM; supported/unsupported suites; batch fallback = 0 | speculative wiring on `spec-integration` (base CP12 `6c144bb`), see "Wiring"; final after CP12 certifies |
| Per-builtin instruction guards measured and locked | native level: 25 workloads locked (+10 %); VM-level guards (call overhead through `CallNative`) at wiring |
| D-015 candidates recorded | Q1–Q14; owner decisions pending for Q1, Q2, Q4, Q6–Q9, Q14 |

## Risks

* **CP12 interfaces may still change** (opcode operands, kind bits,
  structural types, alias analysis). The natives depend only on CP09; the
  wiring steps name each CP12 dependency so a change is caught at rebase.
* **Legacy oracle drift.** The pinned probe inventory fails if legacy's
  context behaviour changes; the differential test runs against whatever
  binary `RT_BUILTINS_LEGACY` names (default: this tree's `nift`).
* **Reproduced quirks (Q1, Q2, Q6–Q9)** keep legacy's results so the
  differential gate stays at zero; changing them needs owner decisions and
  old/new regression tests (D-014 class 2).
* **Random generator coverage** is bounded by its value pools (no `$`,
  0x1f, CR or NUL in strings; no decimals beyond int64; objects ≤ 5 keys);
  unit tests cover the boundaries the generator does not.

## Deliverables

`src/rt/natives/` (`builtin.{h,cpp}`, `builtin_list.inc`,
`string_methods.{h,cpp}`, `array_methods.{h,cpp}`, `object_methods.{h,cpp}`,
`format_methods.{h,cpp}`, `support.{h,cpp}`); `tests/rt/builtins_{unit,diff,bench}.cpp`,
`tests/rt/builtins_support.h`; `tests/runtime2/builtins/` (`context_probe.py`,
`probe_cases.py`, `context_dependent.txt`, `builtins_mutants.py`,
`builtins_guards.py`, `builtin_guards.json`); Makefile targets; this record. Wiring patch sketch: outside the branch (scratch), as
CP12 files are not to be edited here.

## Commit strategy

Local commits on `runtime-2-cp14-dev` (base `0bd5f06`), none pushed:
(1) natives and table; (2) tests, probe and Makefile targets; (3) this
record and evidence. Rebased onto CP12's certified tip before wiring.

## Evidence

`../evidence/cp14/`: `unit-diff-probe.txt` (`make test-rt-builtins`),
`diff-seeds.txt`, `sanitize.txt` (`make test-rt-builtins-sanitize`),
`mutants.txt`, `context-probe.txt`; round 2: `mutants-f6.txt`,
`mutants-m10.txt`, `guards.md`, `guards-check.txt`. Legacy oracle: the `runtime-2` legacy
build at `/home/claude/review/nift/nift` (`$LEGACY` in the logs).
