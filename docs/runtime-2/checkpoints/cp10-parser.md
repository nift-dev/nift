# CP10 — Lexer and parser for the full script grammar

Status: see `../progress.md` (implemented, under review).

## Objective
One parser for Nift scripts that produces one AST with byte spans, accepts
every script the legacy engine accepts, and rejects every script the legacy
engine rejects at parse time with the identical diagnostic (text, code,
`file:line:column`). Nothing is wired into execution yet: CP11 compiles this
AST, and templates (CP24) and the shell front end (CP19) reuse it.

## Scope
- New (one concern per header/implementation pair, `tools/rt-lint.py` clean):
  `src/rt/lexer.{h,cpp}`, `src/rt/ast.{h,cpp}`, `src/rt/source_scan.{h,cpp}`
  (whitespace, identifiers, legacy `find_balanced`, statement ends),
  `src/rt/parser.{h,cpp}` (public entry points, shared depth counters),
  `src/rt/parser_internal.h` (interface between the layers, bounds);
  statement layer `src/rt/statement_parser.{h,cpp}` (class, dispatch,
  statement classification), `src/rt/declarations.{h,cpp}` (fn, enum,
  struct, export, import, template forms), `src/rt/control_flow.{h,cpp}`
  (comments, while, try, for, if/else, return, throw, break/continue);
  expression layer `src/rt/expr_parser.{h,cpp}` (class, statements, units,
  operators, await), `src/rt/expr_postfix.{h,cpp}` (postfix links, call
  shapes), `src/rt/expr_literals.{h,cpp}` (primaries, strings, collections,
  lambdas).
- Tests: `tests/rt/parser_unit.cpp`, `tests/rt/parse_dump.cpp` (AST dump,
  verdicts, parenthesised reprint, string readings, fuzz, bench),
  `tests/rt/legacy_oracle.cpp` (in-process legacy translator oracle),
  `tests/rt/rt_parser_tool.h`, `tests/rt/syntax-errors/*.f` (34 hand-written
  syntax errors), `tests/rt/legacy-cases/*.f` (247 adversarial-review
  probes), `tests/rt/error_corpus.txt` (1,500 mutants),
  `tests/runtime2/rt_parser_corpus.py`, `tests/runtime2/rt_parser_negative.py`.
- Build: `make test-rt-parser` (in `make test`, listed in
  `tests/runtime2/certification_guard.sh` REQUIRED), `make test-rt-parser-asan`.
- Legacy code: one line in `src/Parser.h` (`friend struct RtParserOracleAccess`,
  test access to the translator, like the existing `SourceTranslationTestAccess`).
  No behaviour change; `src/rt/*` is not linked into `nift`.

## Where parse errors come from in legacy
A `.f` script (file or `-c`/`-e` program) is first translated as a whole by
`Parser::translate_function_program` (`src/ParserScript.cpp:41-239`) into
template text. Only failures of that translation are parse-time errors: they
are reported before any statement runs, with code `native.translation_error`
(`DiagnosticCode::NativeTranslationError`, Fatal) and printed by the CLI as
`error: <abs path>:<line>:<column>: <message>`, exit 1
(`src/CLI.cpp:1490-1525`; a leading `#!` line is removed before translation,
so line numbers count from the line after it). Everything else — expressions,
declarations, struct bodies, lambda bodies, `@`-directives — is examined only
when the statement runs, after earlier statements have had their effects.

The new parser therefore has two layers:

1. **Statement layer** (`parser.cpp`): a faithful port of the translator's
   statement recognition. Same branch order, same `boundary()` keyword rule,
   same quote/bracket-aware statement splitting (depth may go negative), same
   `find_balanced` (quote, backtick-run, `@/*`, `<!--`, `@//` aware), same
   messages and the same reported offset (the start of the statement being
   translated; for `else`-chain errors the byte after the previous block's
   `}`). Nested blocks are parsed in place over absolute offsets, so nested
   errors carry absolute positions exactly as `SourceView::slice` gives legacy.
2. **Expression layer** (`expr_parser.cpp`): a Pratt parser for statement
   content and expressions. Text that legacy only examines at run time and
   that falls outside the grammar is **never** a parse error here: it becomes
   an `Opaque` node with a reason. Per D-001 a compilation unit containing an
   `Opaque` node stays on the legacy engine as a whole (CP11's whole-unit
   fallback), so legacy keeps producing its run-time behaviour, diagnostics
   included. semantics.md v1.1 has no row that allows any of these to be
   rejected earlier, so all of them are deferred (see "Divergence classes").

## Grammar inventory (accepted today)

Statement forms, in the translator's order (`src/ParserScript.cpp`):

| Form | Accepted text | Parse-time diagnostics (at statement start) | Legacy | AST |
| --- | --- | --- | --- | --- |
| Function | `fn [ [async] ] (name(params)) { body }` | `fn requires '(name(args))'`, `fn requires a block` | :58 | `FnDecl` (+`kFlagAsync`); body translated now (errors propagate). Bad signature (non-identifier, duplicate, `...rest` not last, whitespace-only list `f( )`): run-time "invalid callable signature" (`ParserTemplate.cpp:682`, `ParserHelpers.cpp:625`) → Opaque |
| Enum | `enum Name { A, B }` (name = `[A-Za-z0-9_]+`) | `enum requires 'Name { members }'` | :68 | `EnumDecl`; members split at `,`/newline; a non-identifier or duplicate member, or no member, → Opaque (`invalid or duplicate enum member`, `enum requires at least one member` at run time) |
| Struct | `struct (Name) { fields; methods }` | `struct requires '(name)'`, `struct requires a block` | :72 | `StructDecl` with `StructField` (`[private ]name := init`) and method `FnDecl` (`[private ]fn(name(params)) { }`); the body is copied raw and checked at run time (`ParserTemplate.cpp:636-680`), so any other body → Opaque(struct-body), as are a variadic constructor, a second constructor and a repeated method name (legacy keeps the last silently) |
| Export | `export (binding)` `[;]` | `export requires '(binding)'` | :79 | `Export`; non-identifier → Opaque |
| Import | `import (args)` `[;]` | none; an unbalanced `(` copies the rest of the region verbatim | :84 | `Import`; unbalanced → Opaque(template-text) to region end |
| Comments | `@// …`, `// …` to end of line; `@/* … */`, `/* … */` | `open comment '@/*' has no close '*/'`, `open comment '/*' has no close '*/'` | :91-118 | dropped (statement start only; inside a statement they are not comments) |
| While | `while (cond) { body }` | `function while requires '(...)'`, `function while requires a block` | :119 | `While` |
| Try | `try { } catch (name) { }` (only when `try` is followed by `{`) | `try requires a balanced block`, `try requires catch(identifier)`, `catch requires '(identifier)'`, `catch requires one valid identifier`, `catch(identifier) requires a block` | :126 | `Try` |
| For | `for (x : iterable) { }`, `for ((k, v) : obj) { }` | `function for requires '(...)'`, `function for requires a block` | :142 | `For` + `ForBinding` (`kFlagPairBinding`); any other header (C-style `;` loops, `:=`) → Opaque |
| If | `if (c) { } [else if (c) { }]* [else { }]` | `function if requires '(...)'`, `function if has no matching ')'`, `function if requires a block`, `function else if malformed`, `function else if requires block`, `function else requires block` | :150 | `If` chain (`c` = else-if `If` with `kFlagElseIf`, or else `Block`) |
| Return | `return [expr]` to newline/`;` (quote-aware, **not** bracket-aware) | none | :163 | `Return` (`kFlagBare` when empty) |
| Throw | `throw expr` (needs whitespace after `throw`; not when the rest starts with `:= = += -= *= /= %=`); bracket-aware | `throw requires an Error expression` | :168 | `Throw` |
| Break / continue | alone before newline/`;`/end (spaces allowed) | none | :174-175 | `Break`/`Continue`; otherwise a general statement |
| Shebang text | statement starting `#!` | none | :185 | `Comment` when the statement is one line; the translator emits `@//` + statement, so only the first line of a multi-line one (brackets spanning lines) is a comment and the rest runs as template text → `TemplateStmt` + Opaque(template-text) |
| Template text | statement starting `@` or `$` | none | :190 | exactly one `@fn(…){…}`, `@fn[async](…){…}`, `@struct(…){…}`, `@import(…)` or `$[stmt]` → the same node as the script form (`@fn` bodies get `kFlagDeferredBody`: translated at call time, `ParserTemplate.cpp:682-694`; `@import` gets `kFlagTemplateImport`: its run-time messages say `@import …` where the script form says `import …`); anything else → `TemplateStmt` + Opaque(template-text) |
| `await` | `await expr` | none | :200 | `ExprStmt(Await)`; see `await` below |
| Command | first word contains `/`, or several words, and no assignment form, no brackets, no `=`/`:` at depth 0 | none | :202-230 | `Command` (external process, `@__nift_cmd`) |
| Everything else | `$[stmt]` | none | :231 | statement content (below) |

Statement content and expressions (evaluated at run time by
`src/ParserExpression.cpp`; precedence confirmed by the paren oracle):

| Construct | Grammar here | Legacy reference |
| --- | --- | --- |
| Declarations | `name := e`, `const name := e`, `immut name := e` | `ParserExpression.cpp:4614` |
| Destructuring | `[a, b] := e`, `{a, b} := e`, and with `=` (flat identifiers only) → `Pattern` | `:4596-4612` |
| Assignment | `target = e`; `+= -= *= /= %= &= \|= ^=` (target: name, member, index) | `:2250`, `:4558` |
| Increment | `++x`, `x--` as a statement or as a whole evaluation unit (argument, element, index, parenthesis, right-hand side, condition, lambda body) | `:4574-4588` |
| Operators | `?:` (right) < `??` (right) < `\|\|` < `&&` < `== !=` (right) < `< <= > >=` (one per chain) < `+ -` < `* / %` < prefix `! - +` < postfix. `!` followed by an arithmetic/relational operator or `??` is Opaque (legacy applies `!` after splitting at logical and equality operators but before arithmetic: `!1 + 1` is `!(1 + 1)`, `ParserExpression.cpp:4835`). In `if`/`else if`/`while` conditions, `?:`, `??`, unparenthesised `==`/`!=` chains, lambdas and object literals are Opaque (legacy's condition evaluator groups them differently or reads `=>` as an assignment) | `Ast.cpp:88-176`, `ParserExpression.cpp` operator splitting |
| `await` | `await <prefix operand>` ending its unit: `x := await f()`, `1 + await f()`, `!await f()`, `f(await g())`. Legacy awaits the whole rest of a unit whose text starts with `await ` (`ParserExpression.cpp:2363`), so `await f() + 1` is `await (f() + 1)`: an operator after the operand is Opaque; `await(x)` (removed form), `await` followed by anything but one space, and `await := …` are Opaque; `await` alone is a name | `ParserExpression.cpp:2363`, `:2598` |
| Postfix | `.name`, `[i]`, `?.name`, `?[i]` and calls, with no whitespace before a link or after its dot (legacy: `x [0]`, `o . a`, `o?. a`, `.b` on the next line are malformed). Links on a plain name, an array literal or an object literal; on other receivers (strings, parenthesised expressions, call results of those) only a known builtin method call (the 124 names of `ParserExpression.cpp:3125`) or an empty `.stringify()`/`.prettify()`/`.highlight()`; none on number, `true`, `false`, `null` literals. Calls: callee a name, a member chain from a name (`o.f(1)`, `o?.m.g(2)`), or a known/presentation method on a receiver passing legacy's receiver test (no `:=`, no operator at depth 0); `f(1)(2)`, `(f)(1)`, `g[0](x)`, `xs[0].f(5)`, IIFEs are Opaque. Indexes containing `[`/`]` are Opaque (legacy matches brackets naively: `xs[xs[0]]`). A chain with `?.`/`?[` is regular only as a whole unit (`!n?[0]`, `1 <= o?.k` differ from their parenthesised forms in legacy) | `ParserExpression.cpp:3088-3200`, `ParserHelpers.cpp:472` |
| Literals | numbers `1`, `.5`, `5.`, `1.5e-2`, `007`; `"…"`/`'…'` with `\` escapes (every escape accepted, semantics.md) and `$[expr]` interpolation; a literal with both a backslash and a `$`, or whose `$[` segment would end at a different `]` under legacy's naive bracket matching, is Opaque (legacy decodes escapes first, then interpolates the decoded text with `\$` as an escaped `$`: `"\\$[1+1]"` prints `$[1+1]`); `true false null`; arrays `[a, b]`; objects `{"k": v}` (double-quoted keys without escapes, no duplicates). A string literal that is a whole array element or object value is stored verbatim by legacy (no interpolation; JSON or parameter unescaping), so with `$[` or a backslash it is Opaque. Trailing commas (arrays, objects, calls, patterns, parameter lists) and whitespace-only argument lists `f( )` are Opaque | `Ast.cpp:69-76`, `ParserExpression.cpp:458`, `:1173`, `:4211-4285`, `Parser.cpp:1403` |
| Lambdas | `(a, ...r) => e`, `a => e`, `(a) => { block }`, `async (…) => …`, only at the start of their unit (legacy makes the whole unit a lambda when `=>` occurs at depth 0) and, with a block body, only when the block ends the unit; `( ) => e` is Opaque | lambda syntax in `ParserExpression.cpp:2390-2420` |
| Callables with types | `atomic<int>(…)`, `atomic<bool>(…)` | `ParserExpression.cpp:2561` |

Not in the language today (all run-time errors in legacy, Opaque here): `**`,
`//` and `/* */` after statement text, `&`/`|`/`^` operators, hex and `_`
digit separators, backtick strings, `typeof`/`in`/`not`/`and`, C-style `for`,
typed declarations (`int x := 1`), unquoted object keys, spread in array
literals.

## AST design
- One arena per compilation unit (`rt::Ast`): `std::vector<Node>` plus one
  `std::vector<NodeId>` for child lists. `Node` is 36 bytes: kind, operator,
  flags, `[begin, end)` byte span, three fixed child slots (`a`, `b`, `c`) and
  a `(list, count)` slice. No strings are copied or decoded at parse time:
  names and literals are read back through the span.
- 51 node kinds: 27 statement kinds (including `Command`, `TemplateStmt`,
  `Pattern`) and 23 expression kinds, plus `Opaque` with one of six reasons.
- `Command` and `TemplateStmt` exist so the shell (CP19) and template (CP24)
  front ends can produce the same tree; `kFlagDeferredBody` records the one
  semantic difference between `fn` and `@fn`.
- Diagnostics: `ParseError{message, offset}` with `kCode =
  "native.translation_error"`; `rt::locate` turns the offset into the
  legacy line and byte column.
- Spans: a node ends at the end of its last consumed token, so `2 * (1 + 3)`
  and `-(1)` cover the closing parenthesis.
- Recursion is bounded: 256 nested blocks and 64 expression levels per
  thread (counters shared by nested parsers for interpolation and lambda
  bodies; every parse level, prefix operator and postfix link counts).
  Beyond that the construct becomes Opaque(nesting-limit) and the unit falls
  back. 256 blocks keep the whole parse inside a 1 MB stack (the Windows main
  thread), also unoptimised and under ASan (gate: `ulimit -s 1024`). Legacy's
  evaluator fails at depth 97 ("expression nesting exceeds parser limit",
  `ParserExpression.cpp:1879`): measured, the first failing nesting is 96 for
  parenthesised sums, arrays, calls, unary chains, `-( … )` and method chains
  (one level each) and 48 for `f(1 + …)` (two levels each); rt's counter
  grows at least as fast for every shape, and 64 leaves a margin for the
  levels legacy adds around a unit. Legacy has no parse-time depth
  diagnostic; beyond 256 blocks rt does not look for translation errors
  (the unit falls back and legacy reports them itself).
- Inspection: `parse_dump dump FILE` prints one node per line (kind, operator,
  flags, Opaque reason, span, text of leaves); there is no `nift --dump-ast`.
  Dump, Opaque counting and the reprint walk the tree iteratively.

## Divergence classes (Opaque reasons)
None of these changes behaviour: an Opaque node keeps its unit on legacy.

| Reason | When | Legacy behaviour | Decision |
| --- | --- | --- | --- |
| `legacy-runtime-error` | Text outside the grammar: invalid callable signature (also `f( )`), invalid struct/enum/export names, duplicate or missing enum members, non-`:` `for` headers, unquoted, escaped or duplicate object keys, `**`, hex/separators, trailing `//` or `/*` inside a statement, unterminated string literal inside a statement, relational chains (`a < b < c`), `const` without `:=`, typed declarations, duplicate lambda/destructuring names, `( ) =>`, spread in arrays, trailing commas, nested quotes in interpolation, `$[` segments legacy ends elsewhere, increments of non-lvalues, adjacent literals, `await(…)`, postfix shapes legacy does not dispatch (whitespace before a link, links on scalar literals, calls on call results, indexes, parenthesised callees or lambdas, unknown methods on non-name receivers, brackets inside an index, `f( )`), `$[`/escapes in verbatim array elements and object values | Run-time error when (if) the statement runs — except two irregular acceptances seen: an unterminated string literal inside a call runs with exit 0, and `"say "hi" now"` is accepted as one string (`b4.f` smoke script) | Deferred to run time (D-001; semantics.md does not allow earlier rejection) |
| `irregular-legacy-parse` | `?:` at depth 0 of a statement, condition or lambda body (legacy: `x := true ? 1 : 2` gives `true`; `true ? print("t") : print("f")` prints nothing); `x++ + 1` (legacy applies `++` to the whole unit text); `await x` followed by an operator (legacy awaits the rest of the unit); `!x` followed by an arithmetic/relational operator or `??`; a lambda not at the start of its unit or a block body followed by more text; a postfix chain with `?.`/`?[` inside a larger unit; in conditions: lambdas, objects, `??`, `?:`, `==`/`!=` chains; string literals with both a backslash and a `$` | Accepted with a different meaning (or, for some, rejected depending on the context) | Deferred; behaviour preserved by fallback |
| `template-text` | `@`/`$` statements other than the five forms above (`@script`, `@json`, `@import(…)` followed by more text, …); `import(` without `)`; lambda block bodies starting with `<` (rendered as a template when called, `ParserExpression.cpp:3522`); multi-line `#!` statements | Template engine | Template front end is CP24 |
| `deferred-translation` | Lambda block bodies and `@fn` bodies whose translation fails | Error only when called | Deferred |
| `struct-body` | Struct bodies outside fields/methods, method bodies whose translation fails, variadic or repeated constructors, repeated method names | Run-time error at the struct, or the last method wins | Deferred |
| `nesting-limit` | > 256 nested blocks or > 64 expression levels | Translator stack overflow (≥ ~10,000 blocks) / run-time limit (depth 97) | Fallback |

On the corpora, 45 of 8,583 accepted files contain an Opaque node (31
before the review): 1 in vm-corpus (`**`), 3 in semantics (`**`, nested
quotes in interpolation, `"abc"[1]`; all documented run-time errors) and 41
extracted smoke scripts (before: 28; the review rules add `true`/`null`/
number `.to_string()` ×15, `{"a": "esc \" inside"}` verbatim values with
escapes ×6, `"SELECT $1, \"id $1\" …"` (backslash and `$`), variadic struct
constructors ×2 and `throw (1)` shapes; the rest are shell lines captured by
the heredoc extraction, `"say "hi" now"`, HTML-rendered lambda bodies,
`@script` blocks, unterminated `import(`). The 5,000 generated programs, the
120 probes, rt-diff-corpus and `tests/*.f` contain none. `tests/rt/legacy-cases`
(review probes, 212 legacy-accepted) has 83 units with Opaque nodes by
design.

## Tests
- `tests/rt/parser_unit.cpp`: lexer tokens, all 24 translation messages with
  positions (each cross-checked against the legacy oracle), accepted forms,
  Opaque classes, one or more cases per review finding (`review_findings`),
  `locate`.
- `tests/runtime2/rt_parser_corpus.py` (run by `make test-rt-parser` with
  5,000 generated programs and the quick paren oracle):
  1. accept/reject gate over every corpus (in-process oracle,
     `legacy_oracle compare`);
  2. error corpus: `tests/rt/error_corpus.txt` reproduced by rt and still
     produced by legacy (drift check); `--regen-errors N` rebuilds it from
     structural mutations (delete/insert tokens and keywords, drop closing or
     duplicate opening brackets/quotes, join lines);
  3. paren oracle (`--paren quick|full`): every accepted script is reprinted
     with explicit parentheses around each operator expression and each
     postfix operand (`parse_dump reprint`), and the original and the reprint
     run on legacy `nift` side by side; stdout, exit status and stderr
     (columns and parentheses removed) must agree. String literals are not
     rewritten (the loop path prints `$[...]` literally, semantics.md), and
     entries in `tools/rt-diff/ignore.txt` (clock, thread interleaving) are
     skipped;
     `tests/rt/legacy-cases` is part of the quick set (it holds `==`/`!=`
     chains and other associativity probes), and Await nodes are wrapped
     too, so a misplaced await boundary shows;
  4. fuzz: random byte and structural mutations, plus 100/1,000/10,000-deep
     parentheses, brackets, unary chains and operator chains; and 22
     programs nested 300 and 5,000 deep (blocks, lambdas, struct methods,
     try, parentheses, arrays, calls, unary and postfix chains,
     interpolation) parsed under `ulimit -s 1024`;
  5. negative gate (`tests/runtime2/rt_parser_negative.py`, seeded,
     1,200 generated expression programs plus declaration and string
     programs, about 15 s): every program rt reads with zero Opaque nodes —
     generated ones and `tests/rt/legacy-cases` — is run on legacy `nift`
     and fails the gate on a shape error (malformed or empty expression,
     nesting limit, declaration checks, removed forms, lambda parameter
     lists, object literal checks); generated expression programs also go
     through the paren oracle (unless both runs fail), and string programs
     must print rt's reading of the literal (`parse_dump strings`: parts
     decoded as legacy's `scalar_literal` does, `$[...]` segments
     evaluated). Method calls are generated only on receivers whose run-time
     type supports them and an unbound-name error is not a shape error, so
     every hit is about the text. The expression generator extends the
     review's `exprfuzz.py` (operators, prefixes, postfix chains with
     inserted spaces/newlines/tabs, call shapes, IIFEs, await forms, trailing
     commas, duplicate keys, nesting 20–120 deep) over ten statement forms.
- `make test-rt-parser-asan`: the unit test and the corpus/error-corpus/
  1 MB-stack/fuzz passes under `-fsanitize=address,undefined
  -fno-sanitize-recover=all`.

## Performance
Instructions per KB of script source (callgrind, marginal per iteration: runs
with 1 and 3 iterations, difference / 2), 877 files / 588,766 bytes
(vm-corpus, semantics, cp410 probes, 500 `gen.py` programs):

| Parser | Instructions per pass | Instructions per KB |
| --- | ---: | ---: |
| legacy `translate_function_program` (translation only; expressions are parsed again at every evaluation) | 140,507,767 | 244,375 |
| `rt::parse_script` (full AST including every expression) | 92,688,766 | 161,208 (0.66× legacy) |

## Exit criteria
| Criterion | Result |
| --- | --- |
| Parses 100% of corpus scripts legacy accepts (vm-corpus, semantics, rt-diff-corpus, extracted smoke scripts, cp410 probes, `tests/*.f`, ≥ 5,000 `gen.py` programs) | 8,583 of 8,583 accepted, 0 mismatches (5,000 `gen.py` programs = 6,587 files with modules; `../evidence/cp10/README.md`); also the 212 legacy-accepted review probes |
| Every corpus script legacy rejects at parse time is rejected with identical text and position | No committed or generated corpus script is rejected by legacy at parse time; the 34 hand-written syntax errors (`tests/rt/syntax-errors`, all 24 messages) and the 35 legacy-rejected review probes are rejected identically |
| Error corpus ≥ 1,000 cases, identical diagnostics | 1,500 of 1,500 identical; legacy re-check 1,500 of 1,500 (no drift); 3,892 legacy-accepted mutants all accepted; all 24 messages occur |
| AST inspectable | `parse_dump dump`, unit tests on dumps |
| Parse-time vs run-time errors classified, each class decided | table above |
| ASan/UBSan clean; fuzz without crashes or hangs; bounded recursion | ASan/UBSan clean on 8,864 files, the error corpus, 22 deep-nesting programs on a 1 MB stack and 20,000 fuzz cases; 200,000 fuzz cases without crash or hang (slowest parse 2.5 ms); recursion bounded (Opaque nesting-limit) and safe on a 1 MB stack |
| Regular nodes mean what legacy means (review T1) | negative gate 0 failures (`make test-rt-parser`; seeds 1–12 × 1,500 programs); paren oracle 0 divergent on 8,813 scripts |
| Lint rules | `tools/rt-lint.py` (CP09, from `runtime-2-cp09`, run from a scratch copy) on `src/rt`: 0 problems (function length, line length, one concern per header/implementation pair, ≤ 400 lines per file, string comparisons: keyword checks are byte checks or carry `rt-lint: allow-string-compare` with the reason — parse time only, a unit is parsed once and cached by content hash — and the two token/operator display-name tables carry the same opt-out) |

## Risks
- The paren oracle validates precedence and associativity only where the
  corpora exercise them and where the result is observable; it cannot see a
  difference that produces the same output.
- Opaque reasons are assigned at the point of failure; `legacy-runtime-error`
  also covers the two irregular acceptances named above. CP11 should treat
  every Opaque as "legacy decides", never as a known error.
- "No string comparison on hot paths": keywords are matched by comparing bytes
  of the source at parse time (first-byte prefilter for statement keywords);
  nothing is compared at run time. The remaining literal comparisons are
  marked for `tools/rt-lint.py`.
- The regular set is a whitelist validated against the legacy binary (unit
  cases, review probes, negative gate, paren oracle); the legacy evaluator is
  text-driven and context-dependent, so the whitelist errs towards Opaque.
  Errors that depend on run-time values keep legacy's message text in some
  cases ("unknown value or malformed expression" for an unbound name, a
  non-callable member, a method a value does not support); CP11 must
  reproduce those messages, the parser cannot.
- Interpolation is context-dependent in legacy beyond what the AST shows:
  a call argument that is exactly one quoted literal is passed
  uninterpolated to user callables but interpolated by output builtins
  (`print("$[x]")` prints the value, `id("$[x]")` returns `$[x]`); an
  interpolated name that does not evaluate falls back to page metadata or a
  JSON lookup (`Parser::interpolate_parameter`); `o.stringify()` is a
  presentation modifier even when `o` has a `stringify` field; `n?.a.b` short-
  circuits the whole chain. These are CP11 obligations (recorded here because
  the AST alone does not carry them).
- The translator is quadratic in block nesting (each block is re-scanned for
  its closing brace); the port keeps that shape to keep error positions
  identical.

## Deliverables
Code and tests listed under Scope; this record; `../evidence/cp10/`.

## Commit strategy
Local commits on `runtime-2-cp10` (from `runtime-2` at `fbca7bc`): parser,
tests and build, documentation. Not pushed.

## Evidence
`../evidence/cp10/README.md`.

## Reviews
### Adversarial review 1 (2026-10-18)
The review found that several constructs parsed as regular nodes are
rejected by legacy at run time or mean something else there, which is the
dangerous direction (CP11 would compile them). Owner guidance for the fixes:
where there is a choice, mark the construct Opaque rather than model legacy
exactly; parse-time diagnostics stay identical and no regular node may mean
something different (the negative gate proves it).

| ID | Finding | Status | Fix | Test |
| --- | --- | --- | --- | --- |
| D1 | `await f() + 1` is `await (f() + 1)` in legacy | fixed | `await` operand followed by any infix operator, `++`/`--` → Opaque(irregular-legacy-parse); `await := …` → Opaque; reprint wraps Await nodes | unit `review_findings`; `legacy-cases/c2_aw*.f`; negative gate await forms |
| D2 | `await(x)` is the removed form | fixed | `await` followed by anything but one space and an operand → Opaque | unit; `c2_aw8.f`, `c2_aw9.f`; gate |
| D3 | multi-line `#!` statement runs its later lines as template text | fixed | Comment only without a newline, else `TemplateStmt` + Opaque(template-text) | unit; `c2_sh1.f`, `c2_sh3.f`; gate decl programs |
| D4 | `"\\$[1+1]"` prints `$[1+1]` (escapes decoded before interpolation) | fixed (Opaque) | a literal with a backslash and a `$` → Opaque; `$[` segments whose naive end differs from the quote-aware one → Opaque | unit; `c2_e*.f`, `c2_s*.f`; gate string programs compared with `parse_dump strings` |
| D5 | postfix/call shapes legacy's evaluator rejects | fixed (whitelist) | see the Postfix row: adjacency, receivers, call shapes from `ParserExpression.cpp:3088-3200` and `strip_presentation_chain`, naive index brackets, safe chains as whole units; lambdas only at unit start; `if`/`while` conditions; `!` before arithmetic | unit; `c2_p*.f`, `c2_w*.f`, `c2_o*.f`, `c2_iife.f`, `c2_parencall.f`, `c2_tern_lambda*.f`; gate expression programs + paren oracle; review `exprfuzz.py`: see below |
| D6 | trailing commas | fixed | arrays, objects, calls, destructuring patterns, lambda parameters → Opaque (fn signatures already were) | unit; `c2_trailing_comma*.f`, `c2_pt2.f`, `c2_pt5.f`, `c2_fp1.f` |
| D7 | declaration-time checks | fixed | duplicate/no enum members, variadic or repeated constructor, repeated method, `fn(f( ))`, `( ) => 1`, duplicate object keys → Opaque | unit; `c2_en*.f`, `c2_st*.f`, `c2_fp*.f`, `c2_o1.f`; gate decl programs |
| D8 | expression nesting limit (legacy fails at depth 97) | fixed (conservative) | bound 64; postfix links count; measured legacy limits per shape (Divergence/AST design) | unit (40 vs 70 nested arrays, 70-link chain); gate nesting 20–120 deep |
| D9 | spans after parenthesised operands | fixed | node ends at the last consumed token | unit `2 * (1 + 3)` → 5..16, `-(1)` → 5..9 |
| D10 | `@import(x)` and `import(x)` have different messages | fixed | `kFlagTemplateImport` on the `@import` form; the "same node" claim corrected | unit; `c2_imp*.f` |
| R1 | 4,096-block bound overflows a 1 MB stack | fixed | block bound 256; dump/opaque count/reprint iterative | 1 MB-stack gate (300 and 5,000 deep, 11 shapes), also under ASan |
| R2 | escapes in object keys | fixed (Opaque) | keys with a backslash → Opaque; verbatim string values/elements with `$[` or a backslash → Opaque (legacy stores them uninterpolated, JSON- or parameter-unescaped) | unit; gate decl programs |
| T1 | no negative gate | fixed | `tests/runtime2/rt_parser_negative.py`, gate 5 of `make test-rt-parser` | itself; seeds 1–12 × 1,500 programs: 0 failures after the fixes |
| LINT | `tools/rt-lint.py` problems (file size, own header first, string compares) | fixed | files split by concern (no behaviour change: identical dumps on 526 corpus files and 3,000 generated programs); byte checks or documented opt-outs | `rt-lint.py src/rt`: 0 problems |

Found while fixing (by the new gate, all made Opaque): `!x op y` grouping;
`?.`/`?[` inside larger units; lambdas, objects, `??`, `?:` and `==` chains
in `if`/`while` conditions; whitespace-only argument lists `f( )`; known
methods on `true`/`null`/number literals; `x.f(…)` on array receivers.
The first round of conservative postfix rules made 2,068 of the 6,587
generated programs Opaque (`x.split("-").stringify()`); presentation
methods and links on collection literals were then whitelisted (0 again).

Review mutations re-run: `else`-chain offset `i`→`e` (unit test, 2
syntax-error files, 2 legacy-cases files, 15 error-corpus cases fail);
removing `<!--` from `find_balanced` (15 error-corpus cases and now the unit
test fail); left-associative `==` (unit test and the paren oracle, 2
divergent legacy-cases scripts, fail).

Review harnesses: `c1/` (70 statement-structure probes) and `c2/` (177
expression/declaration probes) are `tests/rt/legacy-cases/`; every one is
either Opaque in rt or accepted by legacy without a shape error (the
remaining legacy failures on regular nodes are value errors: unbound names,
type errors, missing files). `exprfuzz*.py` re-run: see
`../evidence/cp10/README.md`.

### Follow-up from CP11 review round 2 (2026-10-18)
| ID | Finding | Status | Fix | Test |
| --- | --- | --- | --- | --- |
| M4 | A form feed (0x0C) or vertical tab (0x0B) inside a statement was whitespace to the rt lexer; legacy's textual evaluator reports "unknown value or malformed expression" (`print(1)\f`, `x := 2.5\v`), so CP11 compiled and ran a program legacy rejects | fixed (Opaque) | `Lexer::scan` returns a Bad token for `\v`/`\f` anywhere inside statement or expression text → the construct is Opaque(legacy-runtime-error). Between statements the block scanner still skips them (the translator's `isspace`), which is what legacy does. Conservative where legacy accepts them: its prepared loop parser (`Ast.cpp` `P::ws`, `isspace`) runs `x := 2.5\f` inside a loop body; rt leaves that Opaque too (fallback, not a difference) | unit `review_findings` (7 Opaque shapes, 2 accepted between statements); `legacy-cases/c3_*.f` (10 probes); the negative gate's whitespace set now includes `\f` and `\v` (with the old lexer: unit 7 failures and the gate fails) |

The other byte classes legacy treats differently from a plain space were
re-checked and are pinned by unit tests: CR (whitespace in both), NEL,
NBSP, U+2028/U+2029, BOM, NUL, DEL and other C0 controls (`Other` bytes,
Opaque inside a statement).
