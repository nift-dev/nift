# CP13 — Errors, try/catch, diagnostics parity (design)

Status: **design and fixtures only** (no runtime code). Branch
`runtime-2-cp13-design`, base `runtime-2` @`8de2049`. Observations of the VM
are from the CP12 phase A working binary (`nift-cp12/nift`; branch
`runtime-2-cp12-dev`, HEAD `0bd5f06` at capture time; D-018, not yet certified); legacy from `runtime-2` @`8de2049` and v4.12.0
(`ce8cd36`). checkpoints.md row: *CP13 | Errors, try/catch, diagnostics
parity | CP12 | Diagnostic corpus byte-identical*.

Inputs: D-009 #1/#5/#7, D-013–D-021, `semantics.md` (Errors), CP11/CP12
records, `src/Diagnostic.h`, legacy error paths (`Parser::fail`,
`fail_recoverable`, `@__try`/`@__throw` in `ParserTemplate.cpp`, script
translation in `ParserScript.cpp`, `run_script_source` in `CLI.cpp`,
`ProjectInfo::print_build_error`), the VM's `script_runner.cpp`, Assessment A
§4.2/§4.10/§8.1–3 and site review part 2.

## Objective

1. Every error class a CP13-scope program can raise is reported by the VM
   exactly as legacy reports it, except where an owner decision approves a
   change (D-009 #1, D-001 #4, D-018c), and the gate proves it on a fixed
   corpus, byte for byte.
2. `throw`, `try`/`catch`, `error()` and Error values run on the VM with one
   meaning everywhere (D-018).
3. The internal diagnostic the VM produces carries what structured output
   and stacks need (code, span, frames) at zero hot-path cost, so the
   owner can choose the user-visible expansion (section c) without
   re-plumbing.

Out of scope here: anything that changes the default text of a diagnostic
beyond approved decisions; template execution on the VM (CP24); host-op
errors on the VM (CP16).

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
| S1 | Syntax / translation (`syn_*`) | Most syntax errors are found **at run time** by the textual evaluator: `syn_unclosed_paren` prints `before`, then fails at 2:6 with a message that quotes the **rest of the file** (multi-line message); `syn_stray_brace` likewise. `fn add(a,b)` → `fn requires '(name(args))'`; `if(x) stmt` → `function if requires a block`. **Silent:** unterminated string/array → exit 0, rest of file dropped (Assessment A #1); a script line starting with `$` is template text and is silently dropped (`syn_dollar_line_silent`, new finding) | `parse-error` or `opaque:legacy-runtime-error`: falls back, so legacy's run-time timing is preserved | Decision needed (Q5): the VM detects syntax errors before execution, so the `before` line would not be printed. Today the compiler refuses such units, which keeps parity at the price of never running them |
| S2 | Names (`name_*`) | `unknown value or malformed expression: x`; `undefined callable: f`; `assignment to undefined binding`; `binding already declared in this scope`; no `assert` | fallback (`unresolved-name`, `callable:*`, `assign-free-name`, `static-error:redeclared`) | Phase B resolver decides which names are statically unresolved; the errors are then run-time errors at the use (parity) or refusals |
| S3 | Types (`type_*`) | Binding type/const errors; concatenation with a callable; `print` of an aggregate; ordering comparison message names the internal **`@if`** directive; unknown method reported as `unknown value or malformed expression: s.nosuch()`; **silent coercion** `1 + "s"` → `1s` (Assessment A #3) | `type_binding_mismatch`, `type_add_int_string` on the VM, identical; others refused (`type:*`, `method:*`, `expression:Lambda`, `declare-const`) | Parity for refused ones as the type rules are admitted; `1 + "s"` belongs to site-dx/D-019 |
| S4 | Arithmetic (`arith_*`) | division/modulo by zero, int64 overflow, fractional modulo | VM, identical (overflow case refused: `number-literal`) | — |
| S5 | Index/member (`idx_*`) | canonical messages (`JSON array index 5 is out of range in 'a[5]'`, `JSON object has no key`, `value has no member`, negative index, element/member assignment) | VM, identical where compiled | — |
| S6 | Calls (`call_*`) | Errors in a callee called from top level are reported at the **`return` statement** with one `return: ` prefix per return passed (`call_error_two_deep`: `return: return: `); a statement error inside the callee is at the failing expression; loop-called callees at the failing expression; lambda errors carry a `lambda body error: ` prefix; arity `callable argument count mismatch: f`; `print(a, b)` → `print: expected one value`; recursion limit 64 | Two-deep case ran on the VM: `2:10: value has no member: missing` (approved D-018c); recursion completes (D-001 #4); the rest refused (`free-name`, `statement:StructDecl`, `expression:Lambda`, `static-error:arity`) | r2 goldens fix the D-018c intent for 4 more cases; the prefixes other than `return: ` need a decision (Q7) |
| S7 | Loops (`loop_*`) | Output before the error kept; legacy loop wording `cannot change binding type: x` | VM: canonical wording (approved D-009 #1), `for` refused (`statement:For`) | — |
| S8 | `throw` (`throw_*`) | `throw error(m)` uncaught → `error: …:<throw line>:<col>: m` (position baked in at translation: accurate, also inside functions and loops); `throw "x"` → `throw expression must evaluate to Error` (reported at the enclosing `try`, 1:1); invalid custom code → `error: invalid user Error code` | **refused**: `statement:Throw`, `callable:error` (`error()` itself is not compiled) | Compile `throw` and `error()` |
| S9 | `try`/`catch` (`try_*`) | Catch binding exposes `message`, `code`, `category`, `source` (absolute path), `line`, `column`, `cause`; `stringify()` gives JSON. Custom `user.*` codes and cause chains (≤16). Rethrow keeps the **original** origin. Nested try, try in functions/loops, `break`/`return` through `try` all work. Output printed inside the try before the throw is kept. The catch binding is scoped to the catch block. **Defect:** errors inside a `try` or `catch` body get positions relative to the body text (`try_error_in_catch` 1:21 for 5:9) | **refused** (`statement:Try`) | Compile try/catch; positions accurate (r2) |
| S10 | Catchability (`try_uncatchable_*`, `try_catch_*`) | Catchable = diagnostics whose code has disposition `Recoverable` (`Diagnostic.h`): `user.raised`, `io.*`, `stream.*`, `json.parse_failed`, `schema.rejected`, `package.not_installed`, `package/io.import_source_unreadable`, `ffi.library_load_failed`, `ffi.symbol_not_found` — mechanically, `fail_recoverable` sets `active_recoverable_` and `@__try` catches only when it is set. Everything else is fatal **even inside `try`** (undefined callable, index, binding type, division by zero, callee errors): D-009 #7 "A for now". **Defect:** those fatal errors are reported at body-relative positions (`1:1`, `1:10`, `1:12`), and a caught io error's `line:column` is body-relative (`1:10`, `1:1`; site review #3) while one raised in a callee is right (`3:13`) | refused | Reproduce the split exactly (option D, section d); positions accurate |
| S11 | Imports, threads, processes | `import: ` frame prefix with the module's own position; missing import catchable; thread failure `thread: return: …` at the `return`; `run("missing")` → `launched:true`, exit 127, empty stderr (Assessment A #4) | refused (`statement:Import`, `callable:thread`, `callable:run`) | Phase B (imports), CP16 (processes), CP22 (threads) |
| S12 | `-e` / `eval` | see surfaces | `-e` runs on the VM | `eval --json` errors are text (Q8) |
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
`statement:Throw` (3), `parse-error` (2), `type:*` (4). With
`--expect r2` 11 cases fail, all because the unit falls back and legacy
prints the old text (4 D-018c callee positions, 5 positions of fatal
errors inside `try`, 2 try/catch-body position defects).

---

## (b) The diagnostic corpus and "byte-identical"

**Corpus.** The gate corpus is:

1. `tests/runtime2/diagnostics/` (this checkpoint; scripts, `-e`/`eval`
   argument cases, site cases);
2. the error probes already pinned: `tests/runtime2/semantics/errors_*`,
   `*_error_location`, `loop_type_mismatch`, `recursion_limit`, and every
   `tests/runtime2/vm-corpus/*/` case whose golden exit status is non-zero;
3. the CP10 parser error corpus (`rt_parser_negative.py`) for S1.

**What is compared.** stdout, stderr and exit status, as bytes, from one run
in a fresh copy of the case directory with a fixed environment (`env -i`,
`PATH`, temporary `HOME`/`TMPDIR`, `LANG=C`, `LC_ALL=C`, `TZ=UTC`, stdin
`/dev/null`, stdout/stderr not a TTY, so no colour). The only normalisation
is replacing the copy's real path by `$ROOT` (as `vm_corpus.sh`). Site
cases also drop `time taken:` lines and append the published files listed in
`<case>.publish` and the presence of `.nift/.unfinished`.

**Which expectation.** Per case: `<case>.r2.{stdout,stderr,exit}` when it
exists, else the legacy golden `<case>.{stdout,stderr,exit}`. An `.r2`
golden exists only for a change an owner decision approves and names in
`MANIFEST.tsv` (`approved:D-018c`, `approved:D-009#1`, `approved:D-001#4`;
`catchability` and `defect` cases carry r2 goldens that change positions
only). Three goldens were captured from the CP12 VM and match the approved
shapes; the others are written from the D-018c rule (failing expression's
start, no `return: ` prefix) and verified against the source. After D-018c
"byte-identical" therefore means: identical to legacy **except** an
enumerated set of position/prefix/wording changes, each pinned to exact
bytes, never a pattern-based approval. (The pattern-based D-018c policy in
`vm_policies.py` stays for generated programs; the corpus is stricter.)

**Gate (proposed exit criteria).**

* `run.sh --binary ./nift --vm --expect r2`: 0 FAIL. A case that falls back
  passes only if legacy's output already equals the expectation, so every
  `.r2` case must actually run on the VM (or legacy must be fixed).
* `run.sh --binary ./nift --expect legacy` with the VM off: 0 FAIL (legacy
  unchanged, D-006), and the same on the v4.12 binary with `--expect v412`.
* VM coverage of the corpus reported, never a threshold (D-016).
* `site-dx` cases are reported as `EXPECTED-CHANGE` and do not fail the
  run; when site-dx changes legacy, its branch regenerates those goldens.

**Not covered by byte comparison:** stdout/stderr interleaving (captured
separately); TTY colour (VM defect listed above, needs a pty test);
messages containing absolute paths outside the case directory.

---

## (c) Structured diagnostics proposal

### What exists to build on

`Diagnostic` (code, message, origin with span, frames with kind/label/site),
the code taxonomy with dispositions, `eval --json`/`--capabilities` contract
v1, `NIFT_VM_STATS` and `NIFT_TRACE_HOST` JSONL, and on the VM: heap frames
(`Vm::frames_`: return pc + proto per activation), `Proto::offsets` (source
offset of every instruction, read only on the cold path) and
`Proto::notes` (expression text, binding/function names).

### Proposed interface (one discoverable mode)

* `--format=json` (global option) or `NIFT_FORMAT=json`; listed in
  `nift --capabilities` / `eval --capabilities` as
  `"diagnostics": {"formats": ["text","json"], "schema": 1}`.
* Diagnostics go to stderr as JSON Lines, one object per diagnostic; stdout
  is unchanged; exit statuses unchanged. A build emits one object per failed
  page plus one `{"nift_summary":1,…}` object.
* Schema v1:

```json
{"nift_diagnostic": 1,
 "severity": "error",
 "code": "native.name_error", "disposition": "fatal",
 "message": "unknown value or malformed expression: undefined_name",
 "span": {"file": "/abs/b.f", "line": 3, "column": 14, "length": 14},
 "context": {"kind": "script", "page": null, "unit": "/abs/b.f"},
 "frames": [
   {"kind": "callable", "name": "bad", "file": "/abs/b.f", "line": 3, "column": 14},
   {"kind": "script", "name": "<top>", "file": "/abs/b.f", "line": 5, "column": 7}],
 "frames_omitted": 0, "frames_complete": true,
 "hint": null,
 "text": "error: /abs/b.f:3:14: unknown value or malformed expression: undefined_name"}
```

`text` is the exact text-mode line, so a consumer can always fall back to it
and the corpus can check JSON and text from one run. Frames: innermost
first; capped at 64 (first 32 and last 32, `frames_omitted` counts the
rest; deep recursion stays bounded). `context.kind`: script, eval, page,
template, import, thread.

### How the VM provides it cheaply

* Error sites already stop with `(message, offset)`; add a `DiagnosticCode`
  per raising opcode/native (a byte in the fault record) — no hot-path
  instruction.
* On a stop, walk `frames_` once: for each activation the callee proto
  name, and the caller's call site = `offsets[ret - 1]` in the caller's
  proto. O(depth) on the error path only; nothing is recorded per call.
* Spans: `offsets` gives the start; the length comes from re-parsing the
  failing node on the cold path (the AST is cheap to rebuild) or from a
  parallel `ends` array (+4 bytes per instruction; not recommended).
* Caught errors: the same walk fills the Error value's origin (and could fill
  a `stack` member later, see expansion).

### How legacy would emit it

`Parser::fail` already builds a `Diagnostic`; a JSON printer at the three
report points (`run_script_source`, `ProjectInfo::print_build_error`,
`eval`) serialises it. Gaps: codes are `internal.legacy_failure` for most
language errors (needs a message→code table over the message families in
`ParserExpression.cpp`, `ParserTemplate.cpp` and `Ast.cpp`; the fixtures
cover the common ones), frames have no sites (legacy emits
`frames_complete: false` with kind and label only), and try-body positions
are wrong until fixed (S9/S10). Since CP25 deletes legacy, legacy should get
only the printer and the code table, not frame sites.

### Cost and trade-offs

| Item | Cost | Risk / trade-off |
| --- | --- | --- |
| Codes + frames inside VmResult (no output change) | ~1–2 days; cold path only | none for parity; needed by catchable errors anyway |
| Message→code table (VM direct; legacy by message family) | ~2 days | codes become a public contract; taxonomy gaps (no index/type code) |
| JSON printer + flag + capabilities | ~2–3 days incl. schema tests | another contract to keep stable; `eval --json` errors change (contract v2) |
| Full stacks in JSON | small on the VM; legacy incomplete | VM and legacy differ in `frames_complete` until CP25 |
| Stacks in default text output | small | breaks byte parity of every in-function error; needs new r2 goldens corpus-wide |
| Hints (did-you-mean, canonical syntax) | medium; needs phase B scope data | wording churn; keep out of `message` |

### Scope split

**In CP13 gate scope (no new user-visible behaviour):** codes and frames in
the VM's internal diagnostic and in VM Error values; the classification
table shared by catchability and (later) JSON; the corpus and runner; the
VM `error:` colour fix.

**Proposed expansion, needs owner approval (D-019):**
E1 `--format=json`/`NIFT_FORMAT` with schema v1 for script, `-e`, `eval`
and build; E2 full call chains in JSON (VM complete, legacy partial);
E3 taxonomy additions; E4 `eval --json` errors as JSON (contract 2);
E5 text-mode stack lines (`  at bad (b.f:3:14)`), opt-in or default;
E6 hints. Recommended: E1+E2+E3 in a CP13b slice after the gate passes
(text unchanged, so the gate is unaffected); E4 with E1; E5 opt-in only;
E6 after phase B.

---

## (d) Catchable errors (D-009 #7)

Today's rule (documented on the public "Recoverable errors" page):
operational failures are recoverable; programmer, invariant and policy
failures are fatal. D-009 #7 keeps that for the initial migration and
records the objective that ordinary runtime failures become structured,
catchable exceptions while internal errors, memory exhaustion and
termination stay distinct.

| Option | Behaviour | Compatibility | Assessment |
| --- | --- | --- | --- |
| A — exact split | VM catches exactly the Recoverable codes | none | required for the gate |
| B — VM-only widening | VM catches ordinary runtime errors, legacy does not | a program's meaning depends on the engine | rejected: violates D-013/D-018 |
| C — widen everywhere | `try` catches `native.name_error`, `native.invalid_argument`, `native.invalid_operation`, `native.arithmetic_error`, index/type errors (new codes, E3) on both engines; never `internal.*`, `native.limit_exceeded` (recursion, memory), `policy.*`, syntax/translation errors | programs that today stop inside `try` would run their `catch`; probes `errors_try_catch`, `errors_catch_index` and the `try_uncatchable_*` fixtures change; the public page's "what stays fatal" changes; legacy needs `fail()` to raise recoverable values for those codes, including its prepared loop evaluator (`ExecOutcome::Recoverable`) | a language change (D-015 list) needing owner approval |
| D — staged (recommended) | CP13 implements A with the catch decision as one table lookup on the code's disposition; C later flips the table (plus legacy) as its own approved change with old/new tests | none now | keeps CP13 a parity checkpoint |

Further notes: on the VM, in CP13 only `user.raised` is reachable as a
catchable error, because io/json/ffi errors come from host operations that
the VM refuses until CP16; the io fixtures stay on legacy until then.
Rethrow must keep the original origin; output already printed is never
withdrawn; `break`/`continue`/`return` leave a `try` normally; the handler
table is per proto (zero cost on entry), unwinding pops VM frames to the
handler's frame.

---

## (e) Work breakdown

| Task | Content | Owned files | Depends on |
| --- | --- | --- | --- |
| T1 Corpus wiring | `make test-runtime2-diagnostics`; add (b)'s other corpora; pty colour test | `tests/runtime2/diagnostics/**`, one Makefile target | none (can start now) |
| T2 VM diagnostic record | code + frame walk in the fault/`VmResult`; colour fix; text unchanged | `src/rt/vm.{h,cpp}`, `src/rt/vm_ops.cpp`, `src/rt/proto.h`, `src/rt/script_runner.cpp` | CP12: `VmResult`, `StopReason`, `Vm::Frame`, `Proto::offsets/notes`, `error_line`, `NIFT_VM_STATS` schema frozen |
| T3 throw / error() / Error members | compile `throw`, `error(m, code, cause)`, members, `stringify` | `src/rt/compiler_*.cpp` (new `compiler_try.cpp`), `src/rt/opcodes.h`, `src/rt/error.{h,cpp}`, `src/rt/native.cpp` | CP12 refusal-reason names, kind inference (`rt/infer*.h`), `legacy_names` |
| T4 try/catch | handler tables, unwinding, catch binding scope, rethrow, control flow through try; option A table | same as T3 + `src/rt/vm.cpp` | T2, T3; CP12 phase B resolver (catch binding scope) |
| T5 Code table | message family → `DiagnosticCode` for VM sites (and legacy, if E1) | new `src/rt/diagnostic_codes.{h,cpp}`; `src/Diagnostic.h` only if E3 approved | T2 |
| T6 Policies and probes | extend `vm_policies.py` D-018c to try bodies; r2 probes in `semantics.md` | `tests/runtime2/vm_policies.py`, `vm_d018.py`, `semantics.md` | CP12 certified (it owns these files now) |
| T7 Try-body positions in legacy | pass the try/catch `SourceView` into `parse()` (`ParserTemplate.cpp` `@__try`) | `src/ParserTemplate.cpp` | site-dx (owner of the legacy fix) or Q6 |
| T8 Syntax-error timing | per Q5 | `src/rt/script_runner.cpp`, `src/rt/parser.cpp` | Q5 |
| T9 Expansion (E1–E6) | JSON printer, flag, capabilities | new `src/DiagnosticJson.{h,cpp}`, `src/CLI.cpp`, `src/ProjectInfo.cpp`, `script_runner.cpp` | owner approval; T2, T5 |

CP12 interfaces that must be stable before T2–T6 start: `VmResult`/`Fault`
and `StopReason`; `Vm::Frame` and frame-stack layout; `Proto` offsets and
notes; `script_runner.cpp` reporting (`error_line`, `internal_failure`,
`Stats`); compiler refusal reason strings (`statement:Try`,
`statement:Throw`, `callable:error`); `rt/infer.h`; phase B resolver scope
API; `vm_policies.py` `classify()`. T1 can proceed now; T7 belongs with
site-dx.

## Questions for the owner

1. Q1 Approve E1 (JSON mode, schema v1) and E2 (full chains in JSON) for a
   CP13b slice?
2. Q2 Stacks in text output: JSON only (recommended), opt-in text, or default
   text (breaks byte parity)?
3. Q3 Taxonomy: add `native.index_error`/`native.type_error`, or map onto
   `native.invalid_argument`/`native.invalid_operation`?
4. Q4 Catchability: confirm option D now (C later as its own change)?
5. Q5 Syntax errors: keep legacy's run-time timing (refuse such units) or
   report before execution on the VM (prior output not printed)?
6. Q6 Try/catch body positions: legacy fix under site-dx (recommended) or a
   D-018c extension on the VM only?
7. Q7 Prefixes other than `return: ` (`lambda body error: `, `thread: `,
   `import: `, `future: `): keep, or move into frames?
8. Q8 `eval --json` errors as JSON (eval contract 2) together with E1?

## Risks

* The r2 goldens written by rule (not yet produced by a VM) may differ in
  span start conventions; they are reviewed when T3/T4 run them.
* Catchable io errors cannot be tested on the VM before CP16.
* The legacy try-body defect affects positions of fatal errors too; if
  site-dx fixes it, legacy goldens change (expected).

## Deliverables and commits

This design, `tests/runtime2/diagnostics/` (scripts, sites, goldens,
`MANIFEST.tsv`, `run.sh`). Local commit on `runtime-2-cp13-design`.
