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
| L7 | Aliasing depends on argument shape (`f(o.k)` aliases, `f(a)` copies) | Explicit, documented value/reference rules | correctness | X | D-015, CP11 D6 |
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
| L25 | Dynamic scoping of free names | Lexical scoping; strict mode warning first | correctness | R | D-009 #8, Assessment A |
