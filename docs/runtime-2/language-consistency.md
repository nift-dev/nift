# Language consistency register

One place for every historical inconsistency found during Runtime 2.0, so the
language-semantics cleanup (a dedicated checkpoint, after the VM migration has
stabilised the native APIs and value representation) can resolve them together
and test that the new rules work coherently. Correctness issues first; cosmetic
ones last. Every change applies to both engines while both exist (D-018), with
old/new examples, a compatibility decision, regression tests, docs,
performance measurements where relevant, and migration guidance.

Status: **R** = recorded only · **A** = intended semantics approved, not yet
implemented · **D** = done (link) · **X** = VM refuses the construct today.

| # | Inconsistency (legacy) | Intended semantics | Priority | Status | Source |
| --- | --- | --- | --- | --- | --- |
| L1 | Loop bodies and loop-called functions evaluated by a second evaluator with different typing, wording and positions | One meaning everywhere | correctness | D (VM, CP12 phase A) · legacy unchanged | D-018 |
| L2 | Arguments evaluated twice when a loop-called function fails preparation | Exactly once | correctness | D (VM) | D-018 #1 |
| L3 | Binding/parameter types derived from source text of initialiser or argument | Type from the evaluated value | correctness | X | D-015, CP11 M3/N1 |
| L4 | Declarations typed against bindings visible at loop preparation | Consistent declarations everywhere | correctness | X | D-015, CP11 M1 |
| L5 | Statements after `break`/`continue` in the same block run | `break`/`continue` always jump | correctness | X | D-014, D-015 |
| L6 | Built-ins win over user functions; varies by context | Consistent lexical resolution, user definitions shadow | correctness | A | D-009 #3, D-014 |
| L7 | Aliasing depends on argument shape (`f(o.k)` aliases, `f(a)` copies) | Value semantics with copy-on-write for all ordinary values incl. structs, maps, sets; `same()` only for identity types (D-040) | correctness | A (D-040; VM refuses today) | D-015, CP11 D6 |
| L8 | Ordinary runtime errors (index, type, undefined callable, arity, arithmetic) are uncatchable | Catchable; internal VM failures and resource exhaustion stay distinct | correctness | A (staged) | D-009 #7, D-022 |
| L9 | Syntax errors found while running, after earlier output | Rejected before anything runs, both engines | correctness | A (CP13) | D-022 |
| L10 | Errors reported at call site / outermost loop statement / `try` start | Reported where they occur | correctness | D (VM) · site-dx (legacy try/catch) | D-018 #3, D-022 |
| L11 | Unterminated literals silently drop the rest of a script (exit 0) | Error at the opening position | correctness | site-dx | D-020 |
| L12 | Script lines starting with `$` silently dropped | Error (syntax prevalidation) | correctness | R (site-dx P13 → CP13) | D-022 |
| L13 | Unknown `$[root]` in templates published literally | Warning now; strict opt-in; strict default later | correctness | site-dx stage 1 | D-019, owner |
| L14 | Integer results exact via `+` but inexact in `sum()` beyond 2^53 | Exact integer accumulation, checked overflow; mixed int/float defined | **high** | A | D-023 Q6 |
| L15 | `range()` stores doubles, inexact beyond 2^53 | Exact integer bounds and steps; test negative steps, empty, overflow | **high** | A | D-023 Q7 |
| L16 | String `length` counts code points; `index_of`/`substr` use bytes and can split UTF-8 | Consistent indexing — preferred: Unicode code points, explicit byte operations; investigate grapheme needs, performance, migration of byte-offset users | **high** | A (definition to finalise) | D-023 Q2 |
| L17 | `trim` strips fewer bytes than `trim_start`/`trim_end` | One documented whitespace set (ASCII or Unicode — decide) | medium | A | D-023 Q1 |
| L18 | `pick`/`omit` treat `*`/`?` as globs | Literal keys by default; explicit pattern matching | medium | A | D-023 Q9 |
| L19 | Arrays `indexOf`, strings `index_of` | Both spellings; one canonical in docs | low | A | D-023 Q8 |
| L20 | `obj.length()` error at top level, member count in loops; `num.to_double()` likewise | One behaviour | correctness | X | CP14 Q3, Q4 |
| L21 | `trim` strips `\v`/`\f` inside long expressions containing `.replace(` | No shape-dependent behaviour | correctness | X | CP14 Q5 |
| L22 | `keys()`/`values()`/`entries()` arity checked only for some receivers; lazy argument validation | Uniform argument checking | medium | X | CP14 Q10, Q11 |
| L23 | Unknown-method messages differ by receiver kind; mutating methods only on bare bindings | Uniform messages; documented mutation rules | medium | R | CP14 Q12, Q13 |
| L24 | `1 + "s"` silently concatenates | Decide: keep documented coercion or require explicit conversion | medium | R | Assessment A |
| L25 | Dynamic scoping of free names | Lexical scoping; strict mode warning first | correctness | A (D-038: lexical, VM refuses caller-local lookup; migration in language-cleanup) | D-009 #8, Assessment A |
| L26 | Chained presentation methods (`o.stringify().stringify()`) idempotent at top level but re-encode in loop code | One behaviour | correctness | X | CP14 Q14 |
| L27 | `return` ends at the line break, so `return {` + multi-line literal fails | Multi-line return values continue to the matching close | correctness | A (CP13) | D-025 |
| L28 | A `;` inside a quoted string at the top level of a `return` value ends the statement (`return "a;b"` fails) | Statement terminators are recognised only outside strings | correctness | R | CP13 |
| L29 | A `{...}` literal that is valid JSON is parsed as JSON, while `[...]` evaluates its elements as Nift expressions (`"é"`, `-0` and big integers differ by bracket shape) | One literal meaning regardless of bracket shape | correctness | R (VM refuses shape-dependent literals, W9) | CP15 J-Q1 |
| L30 | `validate(schema, "x")` with a quoted literal fails at top level, works in loops | One behaviour | correctness | R (VM refuses, W10) | CP15 J-Q2 |
| L31 | Argument checking of `abs`/`round` and `encode`/`decode` differs between top level and loop code | One argument-checking rule | medium | R | CP15 N-Q2, B-Q2 |
| L32 | `abs`/`floor`/`ceil`/`round` go through doubles, inexact beyond 2^53 | Exact on integers (as L14) | correctness | R | CP15 N-Q1 |
| L33 | Two UTF-8 validators: `length`/`split` lenient, `encode`/`decode` strict | One validation policy | medium | R | CP15 B-Q1 |
| L34 | Encoding name must be exactly `"utf-8"` (`"UTF-8"` rejected) | Case-insensitive standard names | low | R | CP15 B-Q3 |
| L35 | `copy(a, b)` evaluates `a, b` as one expression instead of reporting an arity error | `runtime.arity_error` | medium | R | CP15 J-Q3 |
| L36 | `validate` schema `pattern` uses backtracking `std::regex`: deep input crashes (SIGSEGV), `^(a|aa)+$` hangs (ReDoS); a match-time regex error is reported as a recoverable rejection | Bounded, non-backtracking matching; loud fatal error on limits | correctness | R | CP15 review R1 |
| L37 | Malformed schemas (unresolvable or self-recursive `$ref`) are reported lazily as catchable `schema.rejected`, only when that path is reached | Schema errors are loud and reported up front | medium | R | CP15 review N2 |
| L38 | `uniqueItems` validation is O(n²) | Linear (hash-based) | medium | R | CP15 review N3 |
| L39 | `{"a": 1e-400}.a == 0` is false; `to_int("1e-400")` errors; `abs(INT64_MIN)` is the double `9.223372036854776e+18` typed `int` | Consistent underflow and exact integer results (with L14, L32) | correctness | R | CP15 review N6 |
| L40 | Values nested deeper than the JSON limit crash `validate` in legacy (unbounded recursion) | Loud `limit.exceeded` (VM does this from CP15) | correctness | A (CP15) | CP15 review R2 |
| L42 | Structs fail inside loop bodies ("value has no member") in legacy; `deepcopy(id(a[0]))` still aliases; `a[0].push(x)` / `o.k.push(x)` are errors | One meaning everywhere; resolved by D-040 value semantics | correctness | R (found by L7 probes; L41 is reserved on the CP12B branch) | L7 proposal |
| L54 | Hex literal `0x10` is an error at top level but evaluates to 16 inside loop code (prepared path) | One meaning in every context (decision pending) | correctness | R (VM refuses) | CP15B literals inventory (DeepSeek, 2026-10-20) |
| L55 | `1e309` is an error at top level but evaluates to infinity inside loop code | One meaning in every context (decision pending) | correctness | R (VM refuses) | CP15B literals inventory (DeepSeek, 2026-10-20) |
