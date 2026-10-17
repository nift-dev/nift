# CP10 — Lexer and parser for the full script grammar

Status: see `../progress.md` (implemented, under review).

## Objective
One parser for Nift scripts that produces one AST with byte spans, accepts
every script the legacy engine accepts, and rejects every script the legacy
engine rejects at parse time with the identical diagnostic (text, code,
`file:line:column`). Nothing is wired into execution yet: CP11 compiles this
AST, and templates (CP24) and the shell front end (CP19) reuse it.

## Scope
- New: `src/rt/lexer.{h,cpp}`, `src/rt/ast.{h,cpp}`, `src/rt/parser.{h,cpp}`
  (statement layer), `src/rt/expr_parser.cpp` (expression layer),
  `src/rt/parser_internal.h` (interface between the two layers).
- Tests: `tests/rt/parser_unit.cpp`, `tests/rt/parse_dump.cpp` (AST dump,
  verdicts, parenthesised reprint, fuzz, bench), `tests/rt/legacy_oracle.cpp`
  (in-process legacy translator oracle), `tests/rt/rt_parser_tool.h`,
  `tests/rt/syntax-errors/*.f` (34 hand-written syntax errors),
  `tests/rt/error_corpus.txt` (1,500 mutants), `tests/runtime2/rt_parser_corpus.py`.
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
| Function | `fn [ [async] ] (name(params)) { body }` | `fn requires '(name(args))'`, `fn requires a block` | :58 | `FnDecl` (+`kFlagAsync`); body translated now (errors propagate). Bad signature (non-identifier, duplicate, `...rest` not last): run-time "invalid callable signature" (`ParserTemplate.cpp:682`, `ParserHelpers.cpp:625`) → Opaque |
| Enum | `enum Name { A, B }` (name = `[A-Za-z0-9_]+`) | `enum requires 'Name { members }'` | :68 | `EnumDecl`; members split at `,`/newline; a non-identifier member → Opaque |
| Struct | `struct (Name) { fields; methods }` | `struct requires '(name)'`, `struct requires a block` | :72 | `StructDecl` with `StructField` (`[private ]name := init`) and method `FnDecl` (`[private ]fn(name(params)) { }`); the body is copied raw and checked at run time (`ParserTemplate.cpp:636-680`), so any other body → Opaque(struct-body) |
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
| Shebang text | statement starting `#!` | none | :185 | `Comment` |
| Template text | statement starting `@` or `$` | none | :190 | exactly one `@fn(…){…}`, `@fn[async](…){…}`, `@struct(…){…}`, `@import(…)` or `$[stmt]` → the same node as the script form (`@fn` bodies get `kFlagDeferredBody`: translated at call time, `ParserTemplate.cpp:682-694`); anything else → `TemplateStmt` + Opaque(template-text) |
| `await` | `await expr` | none | :200 | `ExprStmt(Await)` |
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
| Operators | `?:` (right) < `??` (right) < `\|\|` < `&&` < `== !=` (right) < `< <= > >=` (one per chain) < `+ -` < `* / %` < prefix `! - + await` < postfix | `Ast.cpp:88-176`, `ParserExpression.cpp` operator splitting |
| Postfix | call `f(a, ...xs)`, `.name`, `[i]`, `?.name`, `?[i]` | `Ast.cpp:88` |
| Literals | numbers `1`, `.5`, `5.`, `1.5e-2`, `007`; `"…"`/`'…'` with `\` escapes (every escape accepted, semantics.md) and `$[expr]` interpolation; `true false null`; arrays `[a, b]`; objects `{"k": v}` (double-quoted keys) | `Ast.cpp:69-76`, `ParserExpression.cpp:1173` |
| Lambdas | `(a, ...r) => e`, `a => e`, `(a) => { block }`, `async (…) => …` | lambda syntax in `ParserExpression.cpp` |
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
- Recursion is bounded: 4,096 nested blocks and 200 nested expression levels
  per thread (counters shared by nested parsers for interpolation and lambda
  bodies). Beyond that the construct becomes Opaque(nesting-limit) and the
  unit falls back; legacy has no parse-time depth diagnostic (its translator
  overflows the stack between 1,000 and 10,000 nested blocks; its evaluator
  reports "expression nesting exceeds parser limit" at run time,
  `ParserExpression.cpp:1879`), so none is invented.
- Inspection: `parse_dump dump FILE` prints one node per line (kind, operator,
  flags, Opaque reason, span, text of leaves); there is no `nift --dump-ast`.

## Divergence classes (Opaque reasons)
None of these changes behaviour: an Opaque node keeps its unit on legacy.

| Reason | When | Legacy behaviour | Decision |
| --- | --- | --- | --- |
| `legacy-runtime-error` | Text outside the grammar: invalid callable signature, invalid struct/enum/export names, non-`:` `for` headers, unquoted object keys, `**`, hex/separators, trailing `//` or `/*` inside a statement, unterminated string literal inside a statement, relational chains (`a < b < c`), `const` without `:=`, typed declarations, duplicate lambda/destructuring names, spread in arrays, nested quotes in interpolation, increments of non-lvalues, adjacent literals | Run-time error when (if) the statement runs — except two irregular acceptances seen: an unterminated string literal inside a call runs with exit 0, and `"say "hi" now"` is accepted as one string (`b4.f` smoke script) | Deferred to run time (D-001; semantics.md does not allow earlier rejection) |
| `irregular-legacy-parse` | `?:` at depth 0 of a statement, condition or lambda body (legacy: `x := true ? 1 : 2` gives `true`; `true ? print("t") : print("f")` prints nothing); `x++ + 1` (legacy applies `++` to the whole unit text) | Accepted with a different meaning | Deferred; behaviour preserved by fallback |
| `template-text` | `@`/`$` statements other than the five forms above (`@script`, `@json`, `@import(…)` followed by more text, …); `import(` without `)`; lambda block bodies starting with `<` (rendered as a template when called, `ParserExpression.cpp:3522`) | Template engine | Template front end is CP24 |
| `struct-body` | Struct bodies outside fields/methods, method bodies whose translation fails | Run-time error at the struct | Deferred |
| `deferred-translation` | Lambda block bodies and `@fn` bodies whose translation fails | Error only when called | Deferred |
| `nesting-limit` | > 4,096 nested blocks or > 200 expression levels | Translator stack overflow (≥ ~10,000 blocks) / run-time limit (96) | Fallback |

On the corpora, 31 of 8,583 accepted files contain an Opaque node: 1 in
vm-corpus and 2 in semantics (`**`, nested quotes in interpolation, both
documented run-time errors) and 28 extracted smoke scripts (shell lines
captured by the heredoc extraction, `"say "hi" now"`, HTML-rendered lambda
bodies, `@script` blocks, unterminated `import(`). The 5,000 generated programs,
the 120 probes, rt-diff-corpus and `tests/*.f` contain none.

## Tests
- `tests/rt/parser_unit.cpp`: lexer tokens, all 24 translation messages with
  positions (each cross-checked against the legacy oracle), accepted forms,
  Opaque classes, `locate`.
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
  4. fuzz: random byte and structural mutations, plus 100/1,000/10,000-deep
     parentheses, brackets, unary chains and operator chains.
- `make test-rt-parser-asan`: the unit test and the corpus/error-corpus/fuzz
  passes under `-fsanitize=address,undefined -fno-sanitize-recover=all`.

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
| Parses 100% of corpus scripts legacy accepts (vm-corpus, semantics, rt-diff-corpus, extracted smoke scripts, cp410 probes, `tests/*.f`, ≥ 5,000 `gen.py` programs) | 8,583 of 8,583 accepted, 0 mismatches (5,000 `gen.py` programs = 6,587 files with modules; `../evidence/cp10/README.md`) |
| Every corpus script legacy rejects at parse time is rejected with identical text and position | No committed or generated corpus script is rejected by legacy at parse time; the 34 hand-written syntax errors (`tests/rt/syntax-errors`, all 24 messages) are rejected identically |
| Error corpus ≥ 1,000 cases, identical diagnostics | 1,500 of 1,500 identical; legacy re-check 1,500 of 1,500 (no drift); 3,892 legacy-accepted mutants all accepted; all 24 messages occur |
| AST inspectable | `parse_dump dump`, unit tests on dumps |
| Parse-time vs run-time errors classified, each class decided | table above |
| ASan/UBSan clean; fuzz without crashes or hangs; bounded recursion | ASan/UBSan clean on 8,617 files, the error corpus and 20,000 fuzz cases; 200,000 fuzz cases without crash or hang (slowest parse 36 ms, machine shared); recursion bounded (Opaque nesting-limit) |
| Lint rules (no function > 150 lines, no line > 120 columns) | checked manually (`tools/rt-lint.py` is CP09, not on this branch): longest function about 70 lines, no line over 120 columns |

## Risks
- The paren oracle validates precedence and associativity only where the
  corpora exercise them and where the result is observable; it cannot see a
  difference that produces the same output.
- Opaque reasons are assigned at the point of failure; `legacy-runtime-error`
  also covers the two irregular acceptances named above. CP11 should treat
  every Opaque as "legacy decides", never as a known error.
- "No string comparison on hot paths": keywords are matched by comparing bytes
  of the source at parse time (first-byte prefilter for statement keywords);
  nothing is compared at run time.
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
None yet.
