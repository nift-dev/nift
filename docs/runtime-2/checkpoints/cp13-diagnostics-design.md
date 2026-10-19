# CP13 — Errors, try/catch, diagnostics parity (design)

Status: **design approved by the owner (D-022 and refinements); fixtures,
specifications and gates prepared; no runtime code yet.** Branch
`runtime-2-cp13-design`, rebased on `runtime-2` @`1633043` (D-022
refinements). Observations of the VM are from the CP12 phase A working binary
(`nift-cp12/nift`; branch `runtime-2-cp12-dev`, HEAD `0bd5f06` at capture
time; D-018, not yet certified); legacy from `runtime-2` @`8de2049` and
v4.12.0 (`ce8cd36`). checkpoints.md row: *CP13 | Errors, try/catch,
diagnostics parity | CP12 | Diagnostic corpus byte-identical*.

Inputs: D-009 #1/#5/#7, D-013–D-022, `semantics.md` (Errors), CP10–CP12
records, `src/Diagnostic.h`, legacy error paths (`Parser::fail`,
`fail_recoverable`, `@__try`/`@__throw` in `ParserTemplate.cpp`, script
translation in `ParserScript.cpp`, `run_script_source` in `CLI.cpp`,
`ProjectInfo::print_build_error`), the VM's `script_runner.cpp`, Assessment A
§4.2/§4.10/§8.1–3 and site review part 2.

## Decisions in force (D-022, owner, 2026-10-18)

| # | Decision | Where in this record |
| --- | --- | --- |
| 1 | Opt-in JSON diagnostics (`--format=json` / `NIFT_FORMAT=json`), versioned schema with code, severity, message, primary span, context, full call chain and the exact text line; `eval --json` errors are JSON; text output byte-identical; call chains in JSON only | (c), `../diagnostics-schema.md` |
| 2 | Catchability staged: CP13 reproduces today's caught/fatal split exactly; widening is a later language checkpoint for both engines. Specific codes for index, type and other language errors now | (d), (g) |
| 3 | Syntax errors reported before anything runs (no output, no effects), with old/new tests | (f) |
| R1 | Prevalidation applies to **both** engines while fallback exists; whole unit before any instruction; imports validated when loaded; syntax validation is not static validation; one authoritative parser (CP10); zero false rejections over every corpus **and real projects**, as a permanent gate (evidence, not proof) | (f) |
| R2 | Identity (code), severity and catchability are separate; adding a code never changes catchability; both engines emit the same codes; tests assert codes and the unchanged split | (g) |
| R3 | Future checkpoint: catchable ordinary runtime errors on both engines; internal VM failures and resource exhaustion stay distinct | (d) |
| R4 | The 89-case corpus stays the untouched baseline; approved changes get separate old-vs-new tests tied to decisions | (b) |
| C | Coordinator: try/catch body positions fixed in legacy by feature-site-dx (also uncaught errors in try/catch bodies, and the `$`-line drop investigation); prefixes other than `return:` kept for parity (carried as frames in JSON); the VM's uncoloured `error:` is a defect | (a), (b), (h) |

## Objective

1. Every error class a CP13-scope program can raise is reported by the VM
   exactly as legacy reports it, except where an owner decision approves a
   change (D-001 #4, D-009 #1, D-018c, D-022), proven on a fixed corpus byte
   for byte, with every approved change pinned by its own old-vs-new test.
2. `throw`, `try`/`catch`, `error()` and Error values run on the VM with one
   meaning everywhere (D-018) and today's catchable/fatal split.
3. One coherent diagnostic model on both engines: code, severity,
   catchability, span and call chain, emitted as JSON on request.
4. Syntax errors are rejected before any instruction runs, on both engines,
   without ever rejecting a valid program.

Out of scope: template execution on the VM (CP24); host-op errors on the VM
(CP16); widening catchability (future checkpoint, (d)).

---

## (a) Inventory

Fixture names refer to `tests/runtime2/diagnostics/` (89 cases; the
`MANIFEST.tsv` column `inventory` gives the row id S1–S13). "VM" = what the
CP12 binary does under `NIFT_VM=1` (engine outcome from `NIFT_VM_STATS`).
Legacy = `runtime-2` and v4.12, identical on every case except
`tpl_script_try_catch` (D-002 filesystem freedom, see below).

### Report surfaces

| Surface | Legacy format | Exit | VM (CP12) |
| --- | --- | --- | --- |
| Script file | `error: <abs path>:<line>:<col>: <message>` (one line; the message itself may span lines, see S1) | 1 | same line format (`script_runner.cpp: error_line`), exit 1; never coloured (legacy colours `error:` on a TTY — **VM defect, class 1**) |
| `nift -e` | `error: <command-line>:<line>:<col>: <message>` | 1 | same (`eval_e_error` ran on the VM) |
| `nift eval [--json]` | plain `eval: <message>`, no position, **not JSON even with `--json`** | 2 | legacy only |
| Build | `error: while building <name>` / `  <rel path>:<l>:<c>` / `  <message>` / excerpt / caret line; stdout `0 of N files built successfully`; last-good output kept | 1 | legacy only (templates: CP24) |
| VM internal failure | — | — | `error: <path>: NIFT_VM internal failure: <what> (...)`, exit 70 |
| `NIFT_VM=require` refusal | — | — | `error: <path>:<l>:<c>: NIFT_VM=require: not compiled for the VM (<reason>)`, exit 1 |

Output before an error is kept (stdout lines printed before the failing
statement appear; the VM streams and flushes stdout before writing stderr).

### Error classes

| Id | Class (fixtures) | Legacy behaviour | VM on CP12 | Gap for CP13 |
| --- | --- | --- | --- | --- |
| S1 | Syntax / translation (`syn_*`) | Most syntax errors are found **at run time** by the textual evaluator: `syn_unclosed_paren` prints `before`, then fails at 2:6 with a message that quotes the **rest of the file** (multi-line message); `syn_stray_brace` likewise. `fn add(a,b)` → `fn requires '(name(args))'`; `if(x) stmt` → `function if requires a block`. **Silent:** unterminated string/array → exit 0, rest of file dropped (Assessment A #1); a script line starting with `$` is template text and is silently dropped (`syn_dollar_line_silent`, new finding) | `parse-error` or `opaque:legacy-runtime-error`: falls back, so legacy's run-time timing is preserved | Decision needed (decided: D-022 #3, section (f)): the VM detects syntax errors before execution, so the `before` line would not be printed. Today the compiler refuses such units, which keeps parity at the price of never running them |
| S2 | Names (`name_*`) | `unknown value or malformed expression: x`; `undefined callable: f`; `assignment to undefined binding`; `binding already declared in this scope`; no `assert` | fallback (`unresolved-name`, `callable:*`, `assign-free-name`, `static-error:redeclared`) | Phase B resolver decides which names are statically unresolved; the errors are then run-time errors at the use (parity) or refusals |
| S3 | Types (`type_*`) | Binding type/const errors; concatenation with a callable; `print` of an aggregate; ordering comparison message names the internal **`@if`** directive; unknown method reported as `unknown value or malformed expression: s.nosuch()`; **silent coercion** `1 + "s"` → `1s` (Assessment A #3) | `type_binding_mismatch`, `type_add_int_string` on the VM, identical; others refused (`type:*`, `method:*`, `expression:Lambda`, `declare-const`) | Parity for refused ones as the type rules are admitted; `1 + "s"` belongs to site-dx/D-019 |
| S4 | Arithmetic (`arith_*`) | division/modulo by zero, int64 overflow, fractional modulo | VM, identical (overflow case refused: `number-literal`) | — |
| S5 | Index/member (`idx_*`) | canonical messages (`JSON array index 5 is out of range in 'a[5]'`, `JSON object has no key`, `value has no member`, negative index, element/member assignment) | VM, identical where compiled | — |
| S6 | Calls (`call_*`) | Errors in a callee called from top level are reported at the **`return` statement** with one `return: ` prefix per return passed (`call_error_two_deep`: `return: return: `); a statement error inside the callee is at the failing expression; loop-called callees at the failing expression; lambda errors carry a `lambda body error: ` prefix; arity `callable argument count mismatch: f`; `print(a, b)` → `print: expected one value`; recursion limit 64 | Two-deep case ran on the VM: `2:10: value has no member: missing` (approved D-018c); recursion completes (D-001 #4); the rest refused (`free-name`, `statement:StructDecl`, `expression:Lambda`, `static-error:arity`) | old-vs-new tests in `changes/` pin the D-018c intent for 4 more cases; the prefixes other than `return: ` need a decision (decided: kept for parity, carried as frames in JSON) |
| S7 | Loops (`loop_*`) | Output before the error kept; legacy loop wording `cannot change binding type: x` | VM: canonical wording (approved D-009 #1), `for` refused (`statement:For`) | — |
| S8 | `throw` (`throw_*`) | `throw error(m)` uncaught → `error: …:<throw line>:<col>: m` (position baked in at translation: accurate, also inside functions and loops); `throw "x"` → `throw expression must evaluate to Error` (reported at the enclosing `try`, 1:1); invalid custom code → `error: invalid user Error code` | **refused**: `statement:Throw`, `callable:error` (`error()` itself is not compiled) | Compile `throw` and `error()` |
| S9 | `try`/`catch` (`try_*`) | Catch binding exposes `message`, `code`, `category`, `source` (absolute path), `line`, `column`, `cause`; `stringify()` gives JSON. Custom `user.*` codes and cause chains (≤16). Rethrow keeps the **original** origin. Nested try, try in functions/loops, `break`/`return` through `try` all work. Output printed inside the try before the throw is kept. The catch binding is scoped to the catch block. **Defect:** errors inside a `try` or `catch` body get positions relative to the body text (`try_error_in_catch` 1:21 for 5:9) | **refused** (`statement:Try`) | Compile try/catch; positions accurate (`changes/`, site-dx) |
| S10 | Catchability (`try_uncatchable_*`, `try_catch_*`) | Catchable = diagnostics whose code has disposition `Recoverable` (`Diagnostic.h`): `user.raised`, `io.*`, `stream.*`, `json.parse_failed`, `schema.rejected`, `package.not_installed`, `package/io.import_source_unreadable`, `ffi.library_load_failed`, `ffi.symbol_not_found` — mechanically, `fail_recoverable` sets `active_recoverable_` and `@__try` catches only when it is set. Everything else is fatal **even inside `try`** (undefined callable, index, binding type, division by zero, callee errors): D-009 #7 "A for now". **Defect:** those fatal errors are reported at body-relative positions (`1:1`, `1:10`, `1:12`), and a caught io error's `line:column` is body-relative (`1:10`, `1:1`; site review #3) while one raised in a callee is right (`3:13`) | refused | Reproduce the split exactly (option D, section d); positions accurate |
| S11 | Imports, threads, processes | `import: ` frame prefix with the module's own position; missing import catchable; thread failure `thread: return: …` at the `return`; `run("missing")` → `launched:true`, exit 127, empty stderr (Assessment A #4) | refused (`statement:Import`, `callable:thread`, `callable:run`) | Phase B (imports), CP16 (processes), CP22 (threads) |
| S12 | `-e` / `eval` | see surfaces | `-e` runs on the VM | `eval --json` errors are text (decided: JSON, D-022 #1) |
| S13 | Templates (`tpl_*`, sites) | Multi-line build diagnostics with excerpt and caret, last-good output kept, exit 1. **Silent:** unknown root `$[nosuch]`/`$[titel]` published literally, exit 0 (Assessment A #2); unknown `@directive` passes through (documented for CSS) | not on the VM until CP24 | parity fixtures only; the VM must reproduce them in CP24 |

Other legacy findings recorded for the decision log:

* **Codes are mostly absent.** Every diagnostic raised through `Parser::fail`
  without an active diagnostic gets `internal.legacy_failure`
  (`Parser.cpp`, `fail`). The `native.*` codes in `Diagnostic.h`
  (`native.name_error`, `native.arithmetic_error`, …) are defined but almost
  unused (only `native.unsupported_value`, `native.resource_lifecycle`,
  `native.translation_error` appear). Codes are visible to users only on
  caught errors.
* **Frames exist but carry no sites.** `Diagnostic::frames` is filled for
  callables (`ParserTemplate.cpp` callable invocation), imports, `@script`,
  futures and threads, but every frame's `site` is empty; they are used only
  to build the compatibility prefixes (`project_diagnostic`).
* **Absolute paths** appear in every script diagnostic and in
  `e.source`/`stringify()`.
* v4.12 → runtime-2 differences in the corpus: only `tpl_script_try_catch`
  (v4.12: `cat: path must stay inside the Nift project`, fatal, reported with
  the **translated** source `$[t := cat(...)]` in the excerpt; runtime-2:
  `io.open_failed`, caught). Recorded as `.v412.*` goldens.

### VM coverage of the corpus (measured, D-016)

CP12 binary, `run.sh --vm`: of 80 script/`-e`/`eval` cases, 15 execute on
the VM (12 identical to legacy, 3 differing exactly as approved: D-018c,
D-001 #4, D-009 #1), 63 fall back before execution and 2 (`eval`) never
reach the VM; the 9 site cases are legacy-only. Leading refusal reasons:
`statement:Try` (20), `callable:*` (7, two of them `callable:error`),
`opaque:legacy-runtime-error` (5), `unresolved-name`/`free-name` (5),
`statement:Throw` (3), `parse-error` (2), `type:*` (4). With the
intended expectations (now `changes/ --expect new`) 11 cases failed, all because the unit falls back and legacy
prints the old text (4 D-018c callee positions, 5 positions of fatal
errors inside `try`, 2 try/catch-body position defects).


---

## (b) The diagnostic corpus, "byte-identical", and old-vs-new tests

**Baseline (untouched, R4).** `tests/runtime2/diagnostics/scripts/` and
`sites/`: 89 cases, goldens `<case>.{stdout,stderr,exit}` = what legacy
prints today (runtime-2 = v4.12 except `tpl_script_try_catch`, kept as
`.v412.*`). These expectations are never edited to make new code pass.
`MANIFEST.tsv` classifies each case: `parity` (66), `approved:<decision>`
(10: the case's behaviour is superseded by an approved change and checked in
`changes/`), `site-dx` (13: legacy itself is being changed by
feature-site-dx — the four Assessment A reproducers, `1 + "s"`, the
`$`-line drop and the seven try/catch-body position cases).

**Changes (separate old-vs-new tests).** `tests/runtime2/diagnostics/changes/`:
34 cases, each tied to a decision in `changes/MANIFEST.tsv`:
`<case>.old.*` = legacy behaviour before the change, `<case>.new.*` = intended
behaviour (a missing `.new` stream means unchanged).

| Decision | Cases | Kind |
| --- | --- | --- |
| D-018c (accurate locations, no `return:` prefix) | 6 | old-vs-new |
| D-001 #4 (frame budget) | 1 | old-vs-new (`.new` captured from the CP12 VM) |
| D-009 #1 (canonical wording) | 1 | old-vs-new (`.new` captured from the CP12 VM) |
| D-022 #3 (syntax before execution) | 7 + 3 guards | old-vs-new: no output, no effects (`syn_after_effect` checks a file is not created), import validated when loaded; guards: translator errors already pre-execution, and `"abc"[1]` (valid syntax, run-time error) must not be rejected |
| site-dx try/catch-body positions (coordinator decision C) | 7 | old-vs-new, still fatal (D-022 #2) |
| D-025 multi-line return values | 8 + 1 guard | old-vs-new, `.new` from the implementing binary (section (i)) |

The D-018c `.new` files other than the CP12 captures were written from the
rule (failing expression's start, no prefix) and are verified when the VM
runs them; the two `syn_malformed_in_*` `.new` messages are derived from
legacy's message for the same statement and are verified by the
prevalidator (T8).

**Codes (D-022 #2, R2).** `changes/CODES.tsv`: the expected JSON `code` and
`catchable` for all 89 baseline cases and the changes cases;
`codes_check.py` derives the code of every fatal case from its legacy
message with `code-table.tsv` and requires `catchable=no` for every case
that ends in a fatal diagnostic (adding codes never changes catchability):
97 rows, 0 unmapped.

**What is compared.** stdout, stderr and exit status as bytes from one run in
a fresh copy of the case directory with `env -i` (`PATH`, temporary
`HOME`/`TMPDIR`, `LANG=C`, `LC_ALL=C`, `TZ=UTC`), stdin `/dev/null`, no
TTY; the copy's real path becomes `$ROOT`. Site cases drop `time taken:`
lines and append the published files listed in `<case>.publish` and the
presence of `.nift/.unfinished`; script cases may list created files in
`<case>.files` (effects check).

**Runner verdicts** (`run.sh`): baseline differences on `site-dx` cases are
`EXPECTED-CHANGE` (also on `changes --expect old`); with `--vm`, baseline
differences on `approved:*` cases are `SUPERSEDED`; anything else is a FAIL.

**Measured (design phase).**

| Run | Result |
| --- | --- |
| baseline, legacy (runtime-2) / v4.12 (`--expect v412`) / CP12 binary with the VM off | 89/89 each |
| changes `--expect old`, legacy | 25/25 |
| changes `--expect new`, CP12 binary, `NIFT_VM=1` | 6/25 (3 guards + the 3 CP12 captures); the 19 others fall back to legacy (mostly `statement:Try`, `opaque:legacy-runtime-error`) |

**Gate (exit criteria).** (1) baseline on legacy (VM off): 0 FAIL;
(2) baseline with `--vm`: 0 FAIL (differences only on `approved:*`/`site-dx`
cases); (3) changes `--expect new --vm`: 0 FAIL — every old-vs-new case
actually exhibits its new behaviour; (4) `codes_check.py` and
`json_check.py --binary` pass; (5) the zero-false-rejection gate passes in
`check` mode ((f)); VM coverage reported, never a threshold (D-016).

**Make.** `make test-runtime2-diagnostics` (baseline, changes `old`,
codes, schema validation of the JSON fixtures) and
`make test-runtime2-prevalidate-gate` (proxy mode) exist now. They are **not
yet in the certification guard's REQUIRED list**
(`tests/runtime2/certification_guard.sh`); they join it, with the `new`
suite and `check` mode, when the VM work (T2–T4) and the prevalidator (T8)
land.

---

## (c) Structured diagnostics (D-022 #1)

Specified in [`../diagnostics-schema.md`](../diagnostics-schema.md) with the
JSON Schema [`../diagnostics-schema-v1.json`](../diagnostics-schema-v1.json).
Summary: `--format=json` / `NIFT_FORMAT=json`; stderr becomes JSON Lines
only (no colour or formatting), stdout and exit statuses unchanged; record =
`code`, `severity`, `catchable`, `message` (without compatibility
prefixes), primary `span`, `context` (kind, entry, page, `phase`
validate/run, engine), `frames` innermost first with call sites (capped at
64; `frames_complete=false` on legacy), optional `cause` chain and `hint`,
and `text` = the exact text-mode rendering; builds add one summary record;
`eval --json` failures are records (eval contract 2).

How the VM provides the chain cheaply: error sites already stop with
`(message, offset)`; the stop walks `Vm::frames_` once (callee proto name,
caller call site = `Proto::offsets[ret − 1]`), O(depth) on the error path
only, nothing recorded per call. Legacy fills kinds and labels from
`Diagnostic::frames` (callables, imports, `@script`, futures, threads)
without call sites. Text mode is untouched, so the byte-identical gate is
unaffected.

Fixtures: `tests/runtime2/diagnostics/json/` (nested calls, uncaught throw,
caught error, imported code, syntax before execution, `eval`, two page
failures in one build), validated against the schema now; `json_check.py
--binary` checks valid JSON Lines only, no terminal escapes, unchanged exit
and stdout, records as a multiset, `text` byte for byte.

---

## (d) Catchable errors (D-009 #7, D-022 #2, R3)

**CP13: option D (staged) — decided.** The VM reproduces today's split
exactly: catchable = user throws and the recoverable operational failures
(`io.*`, `stream.*`, `json.parse_failed`, `schema.rejected`, package and
FFI loading); everything else is fatal even inside `try`. The catch decision
is one lookup of the raise's catchability, never of its code. On the VM in
CP13 only `user.raised` is reachable as catchable (host operations arrive in
CP16), so the io/json cases stay legacy-routed until then.

**Future language checkpoint (recorded, R3): "catchable runtime errors".**
Both engines (never VM-only, D-018), as its own approved change with
old-vs-new tests. Ordinary runtime errors become catchable Error values with
their codes: `runtime.undefined_name`, `runtime.undefined_callable`,
`runtime.type_error`, `runtime.index_out_of_bounds`,
`runtime.member_not_found`, `runtime.arity_error`,
`runtime.integer_overflow`, `runtime.division_by_zero`,
`runtime.arithmetic_error`, `runtime.invalid_argument`,
`runtime.invalid_operation`, `runtime.mutation_denied`. Stay distinct, never
ordinary exceptions: `internal.*` (VM internal failures, invariants),
resource exhaustion (`limit.exceeded`: recursion budget, memory
budget; `limit.resource_exhausted`), termination/signals, and syntax or
translation errors (rejected before execution). `policy.*` catchability is
a decision for that checkpoint. Affected pinned behaviour:
`semantics/errors_try_catch`, `semantics/errors_catch_index`, baseline
`try_uncatchable_*`, the public "Recoverable errors" page.

---

## (f) Syntax prevalidation on both engines (D-022 #3, R1) — implemented (T8)

**Principle (owner addition 1).** Prevalidation classifies by grammar and
lexical rules only. A program that parses and then fails at run time —
undefined callable, type error, bad index, in code that runs or in code that
never runs — is syntactically valid and is accepted. Legacy's exit status is
recorded as an observation, never as the criterion. A passing check
guarantees only valid syntax.

**Implementation** (this worktree; VM Opaque/fallback semantics unchanged):
`src/rt/prevalidate.{h,cpp}` — `rt::prevalidate(source)` over the CP10
parser (one grammar). A unit is rejected, citing the rule, when:

| Rule | Grammar / lexical rule | Code |
| --- | --- | --- |
| `translator` | a statement form the translator grammar rejects (`fn add(a,b)`, `if(x) stmt`, …); CP10 reproduces legacy's message and position exactly | `syntax.invalid_statement` |
| `deferred-translator` | the same grammar inside a deferred region (function, method, lambda block body) — legacy checks it only when the region runs; recorded by the three deferred sites via `Ast::note_deferred_error` (tree unchanged) | `syntax.invalid_statement` |
| `R1-unterminated-string` | a `"`/`'` literal never closed (quotes pair as legacy reads them) | `syntax.malformed_expression` |
| `R2-unclosed` / `R2-stray-closer` / `R2-mismatched` | brackets `()[]{}` unbalanced in the statement (strings, nested blocks and other Opaque regions are skipped as units) | `syntax.malformed_expression` |
| `R3-empty-element` | an empty element: `,` after an opener or a comma, or before a closer | `syntax.malformed_expression` |
| `R4-missing-operand` | `+ - * / % == != && .` followed by a closer, a comma or the end of the statement | `syntax.malformed_expression` |
| `R5-missing-left-operand` | `* / % == != && || ??` where an operand must start | `syntax.malformed_expression` |

Rules R1–R5 are checked only on the text of `Opaque(LegacyRuntimeError)`
regions; every other Opaque (template text, irregular legacy parses, struct
bodies, nesting limit, well-formed deferred bodies) is valid-but-unsupported
and accepted. Deliberately not rules (accepted although legacy may fail when
the statement runs, because the reading is ambiguous or legacy accepts it):
`x := 1 ||`, `x := 1 ??`, partial ternaries, `+ 2`, relational operators,
`h( )`, `\v`/`\f` and `1abc` tokens.

**The 47 sites.** `tests/runtime2/prevalidate/sites.tsv` lists every
`Opaque(LegacyRuntimeError)` site (file:line, code context): **none rejects by
itself** — a site only says "not a construct the VM compiles"; the statement
is rejected only when one of R1–R5 holds on its text. This is what keeps
`f (1)`, `while (…)`, `{"a": 'single'}`, `"hello".to_string()`,
`throw(1)` as a call, … (26 legacy-accepted shapes measured earlier) valid.

**Diagnostic text.** Same family, position and wording as legacy's run-time
error for the statement (`unknown value or malformed expression: <text from
the offending operand to the end of the statement>` for R2; `expression
cannot be empty` for R3–R5; `object literal: trailing comma`): identical to
legacy on 33 of 35 legacy-failing rule cases (`rules_test.py`), and identical
to the `.new` expectations of all 7 D-022 #3 old-vs-new tests (unchanged);
only the timing changes. The two differences are `(1]` (legacy quotes `(1`)
and `(,1)` (legacy quotes `,1`).

**Tests.** `tests/runtime2/prevalidate/rules.tsv` + `rules_test.py` (85
cases, each run through the prevalidator and against legacy; 89 with the
D-025 rows): every rule
with legacy-failing examples; the D-022 #3 "never executed" cases
(uncalled function, untaken branch, uncalled lambda); and owner addition 2 —
syntactically valid programs that fail at run time (undefined callable,
bad index, division by zero, type errors, throw, overflow, arity) at top
level, in dead branches, in uncalled functions and after `return`, all
accepted. Result: 89/89. `rt-parser-unit` PASS, CP10 error corpus 1,500/1,500
identical, rt-lint clean.

**Units.** Entry points (script file, `-e`, stdin) are validated whole in
`run_script_source` before engine selection — the wiring is a prepared
patch because `src/CLI.cpp` belongs to other agents:
`scratchpad/cp13/cp13-prevalidate-cli-wiring.patch` (applies cleanly;
the patched file compiles; adds `NIFT_PREVALIDATE=only`, exit 0/65, for the
gate's `check` mode). Plan for the rest: `eval` validates its expression
with `rt::parse_expression` + R1–R5 (same function, expression entry); the
legacy import loader (`ParserScript.cpp`, owned by site-dx) calls
`prevalidate` on the module text when it is loaded, reporting through the
import frame (keeps the `import: ` prefix, as the `syn_import_module_syntax`
`.new` expects); page `@script` bodies are validated before the page
renders; `$[...]` template expressions keep legacy timing until CP24. The
VM's script runner needs no change (validation precedes it).

**Zero-false-rejection gate (permanent).** `tests/runtime2/prevalidate/gate.py`
(`make test-runtime2-prevalidate-gate`, `--tool .build/rt-parse-dump`):
every rejection must cite a rule; translator-grammar rejections pass on the
rule; every other rejection needs a reviewed entry in
`expected_rejections.tsv` naming the rule and the reviewer's reading of the
line (legacy behaviour is not a criterion); anything else fails. All
rejections are recorded in `rejections.tsv` (rule, message, legacy
observation, line text). Real projects: 86 units (50 entry scripts, 29
imports / package modules, 7 template `@script` bodies) from **38
repositories** pinned by commit in `repos.tsv` — all 25 nift-packages, the
10 nift-experiments sites, nift-dev nift-dev.github.io and lab — plus the
Labs evidence scripts (28, a local snapshot pinned by sha256; not a public
repository), vendored offline under `projects/`.

Result in strict (tool) mode, after the split and D-025 (provisional
evidence, not certification):

| Corpus | Units | Accepted | Rejected (rule cited) | Unreviewed |
| --- | ---: | ---: | ---: | ---: |
| CP10 legacy cases | 257 | 208 | 49 | 0 |
| CP10 syntax errors | 34 | 0 | 34 | 0 |
| VM corpus | 122 | 120 | 2 | 0 |
| semantics | 57 | 57 | 0 | 0 |
| rt-diff corpus | 15 | 15 | 0 | 0 |
| CP13 diagnostics (incl. D-025 cases) | 119 | 102 | 17 | 0 |
| cp410 probes | 120 | 120 | 0 | 0 |
| tests/*.f | 1 | 1 | 0 | 0 |
| smoke-test extracts | 1,780 | 1,780 | 0 | 0 |
| real projects (38 repos + Labs evidence) | 86 | 86 | 0 | 0 |
| nift-regression-suite extracts | 126 | 126 | 0 | 0 |
| **total** | **2,717** | **2,615** | **102** | **0** |

Rejections by rule: translator 74, R2 15, R3 7, deferred-translator 4,
R1 1, R5 1. The earlier real-project rejection (nift-packages/http
`src/http.f:101`, multi-line `return {`) is resolved by D-025 (section (i)):
the module is accepted and that branch now works on both engines.

--- | --- | --- |
| `syntax.*` | invalid source / grammar | `syntax.invalid_statement` (statement grammar, also in deferred bodies; was `native.translation_error`), `syntax.malformed_expression` (R1–R5; was `native.syntax_error`); `details.rule` |
| `runtime.*` | language run-time failures | new: `runtime.undefined_callable`, `runtime.type_error`, `runtime.index_out_of_bounds`, `runtime.member_not_found`, `runtime.arity_error`, `runtime.integer_overflow`, `runtime.division_by_zero`; renamed from `native.*`: `runtime.undefined_name`, `runtime.arithmetic_error`, `runtime.invalid_argument`, `runtime.invalid_operation`, `runtime.invalid_control`, `runtime.mutation_denied`, `runtime.unsupported_value`, `runtime.invalid_handle`, `runtime.resource_lifecycle`, `runtime.resource_owner_mismatch`, `runtime.resource_transfer_denied` |
| `limit.*` | execution and resource limits (never ordinary exceptions, R3) | `limit.exceeded` (was `native.limit_exceeded`; `details.limit`), `limit.resource_exhausted` (was `internal.resource_exhausted`) |
| `host.*` | host integration | `host.service_failed` (was `native.system_service_failed`), `host.provider_error` (unchanged) |
| `io.*`, `stream.*`, `json.*`, `schema.*`, `process.*`, `policy.*`, `package.*`, `ffi.*`, `user.*` | unchanged | — |
| `internal.*` | genuine Nift implementation failures | unchanged except `resource_exhausted` (moved to `limit.*`) |

The `native.` domain is retired: the word is overloaded in Nift (platform
target, HTTP backend, the VM's built-in table). Mapping, enumerators and
`details` keys: `tests/runtime2/diagnostics/code-taxonomy.tsv`. All renamed
codes were Fatal and stay Fatal; none was visible as a caught Error's `code`.
Each code has exactly one text (the registry test requires unique texts;
statement and expression syntax are two classes for that reason). Mapping of
the legacy messages to codes and the reused codes:
`tests/runtime2/diagnostics/code-table.tsv`.

**Patch prepared, not applied** (Diagnostic.h may be touched by site-dx):
`/tmp/claude-0/-home-claude/5262fcab-c28a-58db-987c-2c4b624387c1/scratchpad/cp13/cp13-diagnostic-codes.patch`
— renames the code texts per the taxonomy (enumerator identifiers unchanged,
so no raising site changes), appends the seven `Runtime*` enumerators (Fatal),
adds `DiagnosticSeverity` and `Diagnostic::{severity, catchable}`
(`make_diagnostic` sets `catchable` from the default disposition, so existing
raises keep their behaviour), and updates `tests/diagnostic_outcome_unit.cpp`
(code count 69, new registry fingerprint) and `tests/source_map_contract.json`
(`syntax.invalid_statement`). `git apply --check` clean; the patched unit test
compiles and passes.

**Table.** `tests/runtime2/diagnostics/code-table.tsv`: ordered regexes over
the message after removing compatibility prefixes; `unknown value or
malformed expression: X` is split by the shape of X (identifier path →
`runtime.undefined_name`, method call → `runtime.member_not_found`, string index
→ `runtime.invalid_operation`, otherwise `syntax.malformed_expression`). The VM sets
codes at the raising instruction (no table); legacy maps in `Parser::fail`
when no explicit code was given (a C++ table generated from, or checked
against, `code-table.tsv`). Coverage on the corpus: 97 rows, 0 unmapped.

---

## (h) The `$`-line drop (D-019: establish before changing)

Minimal reproducer kept (`scripts/syn_dollar_line_silent.f`, baseline,
class `site-dx`). Findings shared with feature-site-dx in
`scratchpad/site-review/dollar-line.md`: the script translator passes any
statement starting with `$` or `@` through verbatim as template text
(`ParserScript.cpp`, intended for `$[...]` values and `@directives`), so
`$o := …`, `$x`, `@nosuchdirective(…)` vanish with exit 0, while `$[nosuch]`
errors. Not documented script syntax; one test
(`tests/v43_cp124_json_object_wall.sh`) passes vacuously because of it.
Assessment: defect candidate. Behaviour unchanged until established; the CP10
parser currently reads such lines as `TemplateText` (valid, legacy-routed),
so if they are ruled invalid the rule changes in the parser (and translator)
and they fail under prevalidation.

---

## (i) Multi-line return values (D-025) — implemented on both front ends

**Exact rule.** `return` takes the text after the keyword up to the first
line break or `;` that is outside quotes **and** outside every `(`, `[`, `{`
the return value itself opened (depth counted from the keyword, quote-aware;
a closer at depth 0 does not reopen anything). So the value continues across
lines only while a bracket, brace or parenthesis it opened is still open;
nothing else continues it — no operator-at-end-of-line, blank-line or
indentation heuristics. A bare `return` at the end of a line keeps its
meaning. An unterminated literal or a bracket never closed still runs to the
end of the region and is a syntax error (prevalidation R1/R2). Unchanged
(recorded for the language-consistency register): a `;` inside a quoted
string at depth 0 still ends the statement (`return "a;b"` fails, before and
after; the prevalidator reports it, R1), and comments are not part of
expression grammar (`return [\n 1, // c` fails exactly like the same
multi-line declaration).

**Implementation (identical in both).** Legacy translator:
`src/ParserScript.cpp`, the `return` branch of the statement loop only (for
integration: site-dx's committed work touches the import-hook area of the
same file, not this branch). CP10 parser: `src/rt/control_flow.cpp`
`StatementParser::return_stmt`. CP10's legacy oracle links the translator, so
it reflects the new rule automatically.

**Tests.** `changes/ret_*` (9 cases, decision D-025): the http.f shape
(struct method, `if` branch), `return [1,\n 2]`, nested literals, strings
containing braces and an escaped quote, a comment inside the value, the same
function called from a loop, bare `return` (guard), `;` handling, and a
parenthesis continuation (which also runs on the VM). Old = legacy before
(`return: malformed parameters` for every multi-line value), new = after;
`.new` captured from the patched binary and reviewed. All 9 pass `--expect
new`, also with `NIFT_VM=1`. The CP10 legacy oracle agrees with rt on all
of them, on `tests/rt/legacy-cases/c1_return_multiline.f` and on
`nift-packages/http src/http.f` (11/11 accepted, 0 mismatched).

**Gates rerun after the change** (`make -j1`; rt objects and
`ParserScript.o` rebuilt):

| Gate | Result |
| --- | --- |
| CP10 accept/reject vs the legacy oracle (`rt_parser_corpus.py`, all corpora + 6,587 generated) | 0 mismatched in every corpus; error corpus 1,500/1,500 and legacy error corpus 1,500/1,500 identical |
| CP10 fuzz (20,000), deep nesting on a 1 MB stack | no crash or hang; 22/22 |
| negative gate (`rt_parser_negative.py`, 1,957 programs) | PASS, 0 failures |
| paren oracle (572 scripts) | 0 divergent |
| `rt-parser-unit` | PASS |
| prevalidation rules (`rules_test.py`) | see (f) |
| strict zero-false-rejection gate | see (f) |

---

## Integration = CP13 phase A (branch `spec-integration`; certified base `runtime-2` `4b2fc909`)

**Scope label.** CP13's roadmap scope is "errors, try/catch, diagnostics
parity". This integration is **CP13 phase A**: syntax prevalidation on both
engines (CLI and import hook), multi-line return values (D-025), the
`syntax.*`/`runtime.*`/`limit.*`/`host.*` code taxonomy, and the
feature-site-dx reconciliation. **CP13 phase B is open**: the VM diagnostic
record (T2), `throw`/`error()` (T3) and try/catch (T4) on the VM, JSON
diagnostics mode (T9), and `eval` prevalidation. Where phase B touches files
the integration owner holds, it follows CP15 and the records20k work.

(History: first integrated speculatively on CP12's tip `6c144bb`, then
rebased onto `bd1c8a9` and `4b2fc909`; the section below keeps its original
wording.)

Integrated after the CP14 wiring by the single integration owner; never
merged before CP12 certifies. Applied: the four CP13 commits (`eaaccba`,
`50cde7a`, `b9eacae`, `9504311`, cherry-picked cleanly; the Makefile merged
automatically), the codes patch and the CLI wiring patch from scratch (both
applied cleanly), plus:

* **Import hook** (design (f), new code): `Parser::execute_import_file`
  prevalidates the module text right after reading it, before any of it runs;
  the error is the module's position through the import frame (`import: ` /
  `@import: ` prefix kept), code `syntax.*`. `changes/syn_import_module_syntax`
  passes `--expect new`.
* **`NIFT_PREVALIDATE=off`** (observation only): skips validation in the CLI and
  the import hook so `rules_test.py` and the gate can record the *engine's*
  own behaviour (their "legacy" column); before, they ran the same binary,
  which now validates first. `NIFT_PREVALIDATE=only` also prints `[code] rule`
  so the gate's `check` mode cites the rule (it reported every rejection as
  `cli`, i.e. unreviewed).
* **site-dx reconciliation.** feature-site-dx (in the base) makes a statement
  whose scan reaches the end of its block inside a quote or an unclosed `[`
  a translator error, in legacy and in the CP10 splitter (the opener of the
  earliest unterminated literal is reported). Such units are now parse errors
  before the R1/R2 checks run, i.e. rule `translator` with site-dx's message;
  legacy fails loudly instead of dropping the rest of the file. The rules rows
  `x := [1, 2`, `print("abc)`, `x := 'abc` (top level) are reclassified
  accordingly (`reject translator fails`). `sites/tpl_unknown_directive` is
  reclassified `site-dx` (site-dx's `unknown directive @inptu` warning; golden
  untouched).
* **Build recovery** (in the base) is unaffected: prevalidation covers script
  entry points and imports; page builds keep their error flow (`@script`
  bodies and `$[...]` keep legacy timing until CP24, as designed).
* **Runner verdicts** (`run.sh`): decisions this binary implements on both
  engines (D-022 #3, D-025) are SUPERSEDED in the baseline with or without
  `--vm` and in `changes --expect old`; VM-only decisions (D-018c, D-001#4,
  D-009#1) are VM-ONLY in `changes --expect new` without `--vm`. With the VM
  off every changes case is checked against exactly one expectation (guards
  against both). `test-runtime2-diagnostics` also runs `--expect new`.
* `changes/ret_multiline_comment.new.stderr` regenerated: with D-022 #3 wired
  the unit is rejected before it runs at the offending token (3:8,
  "expression cannot be empty", no `return:` frame), the convention of
  `syn_malformed_in_uncalled_function`; the earlier `.new` came from a binary
  with D-025 but without the wiring.
* VM corpus: `fallback_parse_error.f`, `fallback_trailing_comma.f` are now
  `prevalidated` (rejected before engine selection, no VM statistics, output
  byte-identical to the goldens); `vm_parity.py` accepts that outcome only
  with no statistics.

Not integrated (no prepared work; open): `eval` expression prevalidation
(needs an expression entry point and old-vs-new tests), JSON diagnostics mode
(T9), the VM diagnostic record (T2), `throw`/`error()` (T3) and try/catch
(T4, needs the phase B resolver). Consequently `changes --expect new --vm`
still fails on the 6 VM-only cases whose units fall back (`statement:Try`
×2, `statement:StructDecl`, `free-name`, `opaque:irregular-legacy-parse`,
`context:assignment-error`), as before integration.

### Integration gate runs (`spec-integration` on `runtime-2` `4b2fc909`, CP14 certified)

2026-10-19, `heavy.sh`, D-031 limits. The code under test is identical to the
CP13 tip (the rebase onto `4b2fc909` changed docs only; verified by diff).

| Gate | Result |
| --- | --- |
| `make test-runtime2-diagnostics` | baseline 89: 75 pass, 0 fail, 12 site-dx, 2 superseded (D-022#3); changes `old` 34: 12 pass, 0 fail, 7 site-dx, 15 superseded (both-engine decisions); changes `new` 34: 26 pass, 0 fail, 8 VM-only; codes and JSON fixture checks PASS |
| baseline `--vm` | 73 pass, 0 fail, 12 site-dx, 4 superseded |
| changes `--expect new --vm` | 28 pass, **6 fail** (VM-only decisions whose units fall back: `statement:Try` ×2, `statement:StructDecl`, `free-name`, `opaque:irregular-legacy-parse`, `context:assignment-error`) — pending T2–T4 / phase B, not a CP13-integration regression |
| `make test-runtime2-prevalidate-gate` | rules 89/89; zero-false-rejection gate (tool) PASS: 102 rejections, each citing a rule, 0 unreviewed |
| gate `--mode check` (the CLI wiring itself) | PASS: 102 rejections, each citing a rule, 0 unreviewed |
| `make test-rt-parser` | CP10 oracle 0 mismatched in every corpus (syntax errors 34/34 identical, legacy cases, VM corpus, semantics, rt-diff, cp410, smoke 1,783, gen 6,587); negative gate 1,957 programs, 0 failures |
| `make test-diagnostics`, `make test-site-dx`, `test-source-map`, `diagnostic-outcome-unit` | PASS |
| `make test-rt-vm` | PASS (rt-diff 815 entries, 0 differing, 7 approved; adversarial 37,470, 0 non-approved; supported generator 500/500; unsupported 500/500; every other step) |

Harness changes needed by the wiring (prevalidation before engine selection
leaves no VM statistics for a syntactically invalid unit): `vm_adversarial.py`
counts a unit with no statistics as the static outcome `prevalidated` only
when `NIFT_PREVALIDATE=only` confirms the rejection (exit 65); anything else
without statistics still fails. The gate's check mode parses the `[code] rule`
suffix across lines (R2 messages quote the rest of the statement).

Coverage and generator rates (before = CP14 certified, after = this stage):
shared corpora 104 units on the VM (84 `vm`, 20 `vm-error`) before and after,
rt-diff 0 differing (the one former `parse-error` corpus script is now
prevalidated); supported generator seed 1: 500/500 before and after (narrowed
shape 6); unsupported 500/500; general generator seed 1: 72/300 before and
after, identical refusal reasons; its 300 mutants: 55 are now `prevalidated`
(syntax errors rejected before engine selection, formerly
`parse-error`/`opaque:legacy-runtime-error` fallbacks), no difference.

## (e) Work breakdown

| Task | Content | Owned files | Depends on | State |
| --- | --- | --- | --- | --- |
| T1 Corpus wiring | make targets; baseline/changes split; codes and JSON fixture checks | `tests/runtime2/diagnostics/**`, `tests/runtime2/prevalidate/**`, Makefile targets | — | **done** (not in REQUIRED yet) |
| T2 VM diagnostic record | code, severity, catchable and frame walk in the stop; colour defect fix; text unchanged | `src/rt/vm.{h,cpp}`, `src/rt/vm_ops.cpp`, `src/rt/proto.h`, `src/rt/script_runner.cpp` | CP12 `VmResult`/`Fault`/`StopReason`, `Vm::Frame`, `Proto::offsets/notes`, `script_runner` reporting | waits for CP12 |
| T3 throw / error() / Error members | compile `throw`, `error(m, code, cause)`, members, `stringify` | `src/rt/compiler_*.cpp` (+ `compiler_try.cpp`), `src/rt/opcodes.h`, `src/rt/error.{h,cpp}`, `src/rt/native.cpp` | CP12 refusal names, `rt/infer.h`, `legacy_names` | waits for CP12 |
| T4 try/catch | handler tables, unwinding, catch scope, rethrow keeps origin, control flow through try; option D catch rule | as T3 + `src/rt/vm.cpp` | T2, T3, CP12 phase B resolver | waits |
| T5 Codes | apply the patch; VM codes at sites; legacy table in `Parser::fail`; `catchable` per raise | `src/Diagnostic.h`, new `src/DiagnosticCodes.{h,cpp}`, `src/Parser.cpp` (`fail`) | site-dx done with diagnostics files; T2 | patch ready |
| T6 Policies and probes | extend `vm_policies.py` D-018c to try bodies; semantics.md Errors rows | `tests/runtime2/vm_policies.py`, `vm_d018.py`, `docs/runtime-2/semantics.md` | CP12 certified | waits |
| T7 Try-body positions (legacy) | `@__try` bodies parsed with their `SourceView` | `src/ParserTemplate.cpp` | feature-site-dx | site-dx |
| T8 Prevalidator | rule-based split (R1–R5 + deferred translator errors; sites never reject by themselves); `rt::prevalidate`; `rt-parse-dump prevalidate`; rules tests; strict gate; CLI wiring as a patch | changed: `src/rt/ast.h`, `src/rt/declarations.cpp`, `src/rt/expr_literals.cpp` (record deferred errors only); new `src/rt/prevalidate.{h,cpp}`; `tests/rt/parse_dump.cpp`; Makefile `RT_PARSER_SOURCES`; `tests/runtime2/prevalidate/**`. Patch for `src/CLI.cpp` | CLI owner applies the wiring patch; import hook after site-dx (`ParserScript.cpp`); eval entry | **done** except wiring
| T9 JSON mode | flag/env, emitter, build summary, eval contract 2, capabilities | new `src/DiagnosticJson.{h,cpp}`, `src/CLI.cpp`, `src/ProjectInfo.cpp`, `src/rt/script_runner.cpp` | T2, T5 | spec done |
| T10 Real-project refresh | re-extract pinned units when a repo moves; review new rejections | `tests/runtime2/prevalidate/{extract_projects.py,projects.tsv,repos.tsv,projects/}` | — | done for the current pins |

Files changed by T8 in this worktree (for integration): `src/rt/ast.h`,
`src/rt/declarations.cpp`, `src/rt/expr_literals.cpp`, new
`src/rt/prevalidate.h`, `src/rt/prevalidate.cpp`, `tests/rt/parse_dump.cpp`,
`Makefile` (`RT_PARSER_SOURCES`/`HEADERS`); everything else is under
`tests/runtime2/` and `docs/runtime-2/`. `src/rt/*.cpp` is linked into
`nift` by wildcard, so `prevalidate.cpp` is compiled into the binary but not
called until the wiring patch is applied.

CP12 interfaces that must be stable before T2–T6: `VmResult`/`Fault` and
`StopReason`; `Vm::Frame` and the frame stack; `Proto` offsets and notes;
`script_runner.cpp` reporting (`error_line`, `internal_failure`, `Stats`);
compiler refusal reasons (`statement:Try`, `statement:Throw`,
`callable:error`); `rt/infer.h`; the phase B resolver scope API;
`vm_policies.py` `classify()`; and, for T8, the `OpaqueReason` enum and the
four parser files if CP12 phase B edits them.

## Risks

* Rule-derived `.new` goldens (D-018c, `syn_malformed_in_*`) may differ in
  span start conventions; they are confirmed or corrected (with a record)
  when the VM/prevalidator first runs them — never by editing the baseline.
* The `LegacyRuntimeError` split is the crux of zero false rejections; the
  26 legacy-OK units show it cannot be done by reason alone.
* Template `$[...]` syntax keeps run-time timing until CP24 (scope note in
  (f)).
* Catchable io errors cannot be exercised on the VM before CP16.

## Deliverables and commits

This record; `../diagnostics-schema.md` and `../diagnostics-schema-v1.json`;
`tests/runtime2/diagnostics/` (baseline 89 cases, `changes/` 25 cases with
`MANIFEST.tsv` and `CODES.tsv`, `code-table.tsv`, `codes_check.py`, `json/`
fixtures, `json_check.py`, `run.sh`); `tests/runtime2/prevalidate/`
(`gate.py`, pinned real-project units, `repos.tsv`, `projects.tsv`,
`extract_projects.py`); Makefile targets; the `Diagnostic.h` patch and the
`$`-line note in scratch. Local commits on `runtime-2-cp13-design`.
