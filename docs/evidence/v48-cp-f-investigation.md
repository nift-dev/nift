# v4.8 CP-F: diagnostic source-location investigation and redesign review

**Historical investigation at f2f243c. The source-map design was subsequently
approved and implemented. See [implementation and certification](v48-cp-f-final.md).**

Starting accepted runtime: `ab687d07d4f987b09ac0277142e2aaf1c0602af6`.
Investigation HEAD: `f2f243c92e083bbab46099a07c84a6f6ec856f16` (test-only follow-up).
At this investigation checkpoint, no CP-F runtime changes, public header changes, commits or pushes were made.
ABI remains 1.3. Packages and benchmark repositories were not changed.

The supplied outline explicitly requires: **“If fixing this cleanly requires
broad source-map infrastructure: STOP AND REPORT BEFORE A LARGE REDESIGN.”**
This investigation reaches that condition. A small origin structure alone is
insufficient: multiple transformations discard nonconstant position information,
prepared imported callables lose the defining source path, and the two existing
error projections can disagree. The concrete proposed design below is ready
for review before implementation.

## CP-E and performance closeout

CP-E is accepted and closed with Candidate A, no runtime/ABI/semantic changes.
`docs/ffi.md` now states Parser/persistent Engine ownership, handle disappearance,
alias/deepcopy identity, native pointer lifetime, accumulating allocations,
reuse guidance and no release API. It also records failed-import rollback as a
resource invalidation boundary. Automatic release, explicit release and runtime
`malloc_trim` remain excluded from v4.8.

The [final f2f243c wall](https://github.com/nift-dev/nift/actions/runs/37701366305)
is green, including all five jobs and the serial non-destructive boundary proof.
Performance remains closed at the accepted runtime ab687d0; f2f243c changes only
test integrity scanner visibility. No Release artifacts were dispatched.

## Reproduction results

[Sources, machine-readable diagnostics and runner](v48-cp-f/README.md) contain
45 cases: 44 expected failures and one successful try/catch control. Each failure
records both `RenderResult.error` and typed `Diagnostic.origin`, exact code,
message, frames/order/prefix/site and original source coordinates. The harness
reads these fields directly; it does not parse numbers out of human error text.

The current canonical location check fails **39 cases**. Six failure cases have
matching public path/line; only five have matching typed diagnostic path/line.
One block-lambda public line happens to match while its typed origin is wrong.
Matching lines alone do not certify a location. The table below distinguishes
those projections and marks columns that can be asserted exactly. Other columns
are source anchors for review, not a claim that every error must highlight the
innermost token instead of its containing expression/directive.

Representative defects:

- Top-level multiline undefined binding: actual `1:14`; original `3:1`.
- Prepared while: public `1:10`, typed origin `1:1`; original `3:5`.
- Prepared for: public `1:14`, typed origin `1:1`; original `3:5`.
- Nested if in while: public `1:10`; original `4:9`.
- Function → for → while → if → expression: public `1:75`; original `5:17`.
- Native template for: public loop header `2:1`, typed fragment `1:1`; original interpolation `3:5`.
- Nested template control flow: public `2:1`, typed fragment `1:1`; original `4:9`.
- Imported function called from a prepared loop: public and typed diagnostic both identify the **caller file** at `1:28`; error originates in `module_function.n` on line 2.
- Direct failed import: public projection identifies import in the caller at `2:1`; typed diagnostic identifies the right module but its translated `1:14`, instead of module `3:1`.
- Malformed translation (unclosed while block): `native.translation_error` with empty path and `0:0`; original while begins at `2:1`.

Controls that work: direct template interpolation, direct included-file
interpolation, content-file interpolation, multiline template directive start,
and explicit throw line/column. Explicit throw already embeds original line and
column in translated metadata, but its excerpt remains generated text. Template
undefined text is permissive in the ordinary template path, so arithmetic failure
is used there; this investigation does not turn permissive text into a name error.

The callback matrix covers map/filter/reduce/sort_by, expression/block lambdas,
callable-as-value, identical cached lambda text at different source locations,
and the B5 numeric-plan failure/fallback path. All reproduce attribution defects;
no B5 architecture was changed. The try/catch control still catches
`ffi.library_load_failed` and exits successfully. Message and recoverability
changes are outside CP-F.

## Location pipeline and exact loss points

| Stage | Existing representation | Loss / required responsibility |
| --- | --- | --- |
| Native script translation (`ParserScript.cpp:41–235`) | String input → generated template-language string | Inter-statement whitespace discarded; `$[...]`, `@fn`, `@return`, control wrappers inserted; recursive bodies rewritten. No output→input map or translation-error offset returned. Generated offsets cannot be corrected with one base/line delta. |
| Parser source context (`Parser.h:197–201`, `ParserTemplate.cpp:163–174`) | Path + file-backed/in-memory provenance | Carries authority/provenance, not original document, fragment base or transformed offset map. Keep provenance semantics separate from diagnostic coordinates. |
| Control normalization (`ParserHelpers.cpp:498–556`) | Body string + multiline flag | Removes structural first/last lines and per-line indentation. Multiline columns therefore require more than adding one parent byte offset. |
| Prepared body creation (`ParserTemplate.cpp:874–910`) | Reparse `$[...]`, conditions, recursive body substrings into ASTs | Substrings reset coordinates; synthesized control `Stmt` nodes lack useful absolute source spans. Expression spans are local to newly parsed text. |
| Expression/statement parser (`Ast.cpp`, `Ast.h:12–19,45–47`) | Relative `SourceSpan` on syntax nodes | Call arguments are extracted, trimmed and reparsed separately; their spans restart at zero. Statement parsing trims and reparses RHS/conditions. Parent offsets must compose at each extraction. |
| AST execution (`Ast.cpp` / `ast::Context`) | Value/error bool, string message; some typed outcome propagation | Bare arithmetic/name/member failures return message only. There is no general deepest failing-node origin propagation. Legacy callback takes expression text without origin. |
| Legacy expression evaluator (`ParserExpression.cpp:859+`) | Recursive strings, trimmed/substrings; boolean failure | No original expression origin contract. A local AST fix alone cannot locate multiline fallback subexpressions or cached lambda bodies. |
| Callable/closure storage (`Parser.h:215,243+`) | Body, source path/provenance, captures | Defining body offset/document map absent. Prepared dispatch pushes callee source context, but originless errors are later attributed at caller. Expression lambdas can report invocation location. |
| B5 syntax/plan caches (`ParserExpression.cpp:1417+`, callback dispatch) | Source-independent immutable syntax; fresh closure instances | Per-instance defining origin must remain outside global/thread-local syntax cache identity. Identical text at different definitions must not inherit cached coordinates. |
| Failure creation/projection (`Parser.cpp:745–814`) | Public error built from current text/offset; typed origin may be propagated | Existing nonempty typed origins are preserved, but the public error copies typed origin only for recoverable diagnostics. Fatal public/typed locations diverge. A general fix must decide one canonical projection. |
| Native translation postprocessing (`ParserScript.cpp:394–422`) | Import-specific line/column heuristic + fallback origin filling | Repairs import spelling only; cannot recover erased positions generally. Do not add a second family of per-diagnostic heuristics. |
| File boundaries (`ParserScript.cpp` import; template input/content) | Correct loaded file path often retained; wrapper frames appended | Direct include/content controls pass; imported prepared callee path fails. Restore callee origin, preserve frame ordering and import compatibility prefix. |

This is not one global “add the loop's starting line” bug. For example, the
translator turns original multiline text into
`$[first := 1]$[missing_value]`, and the normalizer removes indentation separately
on each line. Neither transformation is affine across all source positions.
Furthermore, correcting an outer loop offset cannot recover the defining path
of an imported prepared function.

Existing tests explicitly encode parts of the old representation:
`tests/parser_statement_state_unit.cpp:45–123` asserts line/column `1:1` and
excerpts such as `$[missing_value]` / `@return(1 / 0)`. Correcting source attribution
requires intentionally updating those location/excerpt expectations, while
preserving messages, diagnostic codes and frame checks. Merely adding new tests
alongside those assertions cannot establish the new contract.

## Proposed canonical model for review

Use an immutable **original source document + original byte offset**, with sparse
composable transformation spans. A document contains its diagnostic path,
original text and line-start index. Source authority/provenance stays in the
existing separate context. Derive line, column and excerpts once from the
original document; do not store independently accumulating line/column deltas.

An origin view references the document plus an offset mapping for the currently
interpreted text. Identity input uses no map. A contiguous slice adds its base.
Translation records output spans mapped to original ranges; generated wrappers
anchor to the originating statement/directive. Dedentation records retained
segments or per-line trim boundaries. Nested slicing/translation composes views
rather than re-deriving positions from error messages or searching for text.
Sparse spans avoid per-byte metadata on large files; their cost must be measured.

AST children must preserve/rebase spans when parsing trimmed call arguments,
RHS, conditions and body fragments. A failure path carries the deepest valid
origin without overwriting an origin from a nested callable/import. Legacy
fallback receives the same origin view as prepared evaluation. The final public
error and typed diagnostic must project that one origin consistently, including
fatal errors. Existing compatibility prefixes and frame order remain unchanged.

Callable/lambda instances retain defining document/body views. Cached B5 syntax
and numeric plans remain immutable and source-independent; bind defining origin
on each fresh closure instance or execution view. This avoids contaminating
identical-text caches with Parser state and avoids retaining mutable contexts.
Shared immutable documents also need safe lifetime across worker/callback copies.

Columns continue using the current byte-count convention; tabs count as one
source byte and are expanded only for diagnostic display. No new Unicode/tab
column convention is introduced. Direct template directives retain their
established directive-start anchoring. Strict bare undefined expressions have a
stable token anchor. Compound legacy failures may use an expression anchor
unless a specific subexpression origin is available; the expected scope must be
explicit rather than a guessed column.

This proposal crosses five coupled implementation areas: translated-source
mapping, normalized fragments, AST/legacy failure origins, closure source
lifetime and unified public/typed projection. A single optional struct field
cannot restore information those stages have already discarded. It is the
broad source-map infrastructure that triggers the requested review boundary.

## Recommended implementation sequence after review

1. Agree canonical origin/excerpt and stable-column contract; establish the
   existing before matrix as the message/code/frame baseline.
2. Implement immutable source document/views and composable translation/
   normalization mapping with focused mapping tests (including tabs, CRLF,
   nested fragments and repeated identical source text).
3. Rebase AST children and preserve prepared/legacy failing origins; bind source
   views to callable/lambda instances without changing cache identity/captures.
4. Project both diagnostics consistently, preserving recoverability and frame
   ordering. Update old generated-fragment coordinate expectations explicitly.
5. Add the matrix to a meaningful core test gate, run the requested full local
   certification, verify origin, commit/push normally, then hosted certification.
   No Release artifacts. Stop after CP-F is green.

Do not claim a safe partial loop-only fix closes CP-F: it would leave top-level
multiline, callee path, normalization, nested argument and fallback defects.
Diagnostic wording differences already present between ordinary and prepared
execution are recorded in the raw data; polishing them is separate work.

## Verification and boundary

- Investigation harness compiles and captures 45 structured cases; 39 fail the
  requested canonical location check. This is an intentional failing baseline,
  not a green regression suite.
- Existing diagnostic smoke suite: PASS, including tab-expanded caret alignment
  and bounded long-line excerpts.
- Existing parser statement-state baseline: PASS (fresh build, assertions enabled).
- Existing diagnostic outcome baseline: FAIL at `tests/diagnostic_outcome_unit.cpp:37`: its fixed snapshot expects 61 codes while `DiagnosticCode::Count` is 62. The preceding registry uniqueness checks pass; the fingerprint check follows the failing count assertion and was not reached. This is present in the unchanged accepted-runtime header, not caused by CP-F. Classify and repair the registry test separately before claiming new CP-F certification. No test weakening or snapshot change was made.
- No runtime fix exists yet; full `make test`, GCC/Clang warning walls, focused
  sanitizers, NRS/PRS and hosted CP-F certification have **not** been represented
  as passing new CP-F evidence. Those remain required after implementation.
- The accepted performance/NRS/PRS/Deep Guards certification remains unchanged;
  no new workflow was dispatched, no release/tag/version work started.

**Required decision:** review the canonical source-map design and scope above
before implementation. CP-F remains the last open feature checkpoint; feature
freeze/stabilization cannot yet be declared complete. This report stops at the
explicit redesign review boundary, not at a completed CP-F release gate.

## Full before matrix

| Case | Original line:anchor column | Public error before | Diagnostic origin before | Code |
| --- | --- | --- | --- | --- |
| top-level | 3:1 (exact) | same file 1:14 | same file 1:14 | internal.legacy_failure |
| top-level-tabs | 2:3 (exact) | same file 1:14 | same file 1:14 | internal.legacy_failure |
| while | 3:5 (exact) | same file 1:10 | same file 1:1 | internal.legacy_failure |
| for | 3:5 (exact) | same file 1:14 | same file 1:1 | internal.legacy_failure |
| nested-if | 4:9 (exact) | same file 1:10 | same file 1:1 | internal.legacy_failure |
| function-loop | 4:9 (exact) | same file 1:68 | same file 1:1 | internal.legacy_failure |
| function-nested-loops | 5:17 (exact) | same file 1:75 | same file 1:1 | internal.legacy_failure |
| prepared-callable | 2:5 (anchor only) | same file 1:27 | same file 1:27 | internal.legacy_failure |
| array-index | 4:5 (anchor only) | same file 1:14 | same file 1:12 | internal.legacy_failure |
| object-member | 4:5 (anchor only) | same file 1:14 | same file 1:17 | internal.legacy_failure |
| array-expression | 3:14 (anchor only) | same file 1:14 | same file 1:14 | internal.legacy_failure |
| object-expression | 3:16 (anchor only) | same file 1:14 | same file 1:14 | internal.legacy_failure |
| multiline-args | 4:9 (anchor only) | same file 1:14 | same file 1:14 | internal.legacy_failure |
| multiline-array | 4:5 (anchor only) | same file 1:14 | same file 1:14 | internal.legacy_failure |
| multiline-object | 3:10 (anchor only) | same file 1:14 | same file 1:14 | internal.legacy_failure |
| multiline-arithmetic | 3:9 (anchor only) | same file 1:14 | same file 1:14 | internal.legacy_failure |
| multiline-boolean | 3:13 (anchor only) | same file 1:14 | same file 1:14 | internal.legacy_failure |
| nested-calls | 3:8 (anchor only) | same file 1:23 | same file 1:23 | internal.legacy_failure |
| chained-index | 3:1 (anchor only) | same file 1:34 | same file 1:34 | internal.legacy_failure |
| malformed | 2:1 (anchor only) | same file 1:14 | same file 1:14 | internal.legacy_failure |
| invalid-callable | 3:5 (exact) | same file 1:14 | same file 1:14 | internal.legacy_failure |
| invalid-collection | 3:5 (exact) | same file 1:11 | same file 1:11 | internal.legacy_failure |
| legacy-fallback | 3:5 (anchor only) | same file 1:14 | same file 1:14 | internal.legacy_failure |
| expression-lambda | 2:15 (anchor only) | same file 1:48 | same file 1:48 | internal.legacy_failure |
| block-lambda | 3:5 (anchor only) | same file 3:3 | same file 1:1 | internal.legacy_failure |
| named-callable-value | 2:5 (anchor only) | same file 1:39 | same file 1:1 | internal.legacy_failure |
| callback-map | 2:5 (anchor only) | same file 1:28 | same file 1:1 | internal.legacy_failure |
| callback-filter | 2:5 (anchor only) | same file 1:28 | same file 1:1 | internal.legacy_failure |
| callback-reduce | 2:5 (anchor only) | same file 1:30 | same file 1:1 | internal.legacy_failure |
| callback-sort_by | 2:5 (anchor only) | same file 1:28 | same file 1:1 | internal.legacy_failure |
| numeric-plan-fallback | 2:21 (anchor only) | same file 1:14 | same file 1:14 | internal.legacy_failure |
| template-expression | 3:1 (exact) | same file 3:1 | same file 3:1 | internal.legacy_failure |
| template-for | 3:5 (exact) | same file 2:1 | same file 1:1 | internal.legacy_failure |
| template-nested | 4:9 (exact) | same file 2:1 | same file 1:1 | internal.legacy_failure |
| template-script | 3:5 (exact) | same file 1:1 | same file 1:1 | internal.legacy_failure |
| template-include | 3:1 (exact) | same file 3:1 | same file 3:1 | internal.legacy_failure |
| import-module | 3:1 (exact) | wrong/missing file 2:1 | same file 1:14 | internal.legacy_failure |
| recoverable | 3:5 (anchor only) | same file 1:14 | same file 1:14 | ffi.library_load_failed |
| explicit-throw | 2:1 (anchor only) | same file 2:1 | same file 2:1 | user.raised |
| imported-callable | 2:5 (anchor only) | wrong/missing file 1:28 | wrong/missing file 1:28 | internal.legacy_failure |
| same-lambda-text | 4:1 (anchor only) | same file 1:65 | same file 1:65 | internal.legacy_failure |
| try-catch | 4:9 (anchor only) | caught successfully | not applicable | — |
| translation-malformed | 2:1 (anchor only) | wrong/missing file 0:0 | wrong/missing file 0:0 | native.translation_error |
| template-multiline | 2:1 (exact) | same file 2:1 | same file 2:1 | internal.legacy_failure |
| content-file | 3:1 (exact) | same file 3:1 | same file 3:1 | internal.legacy_failure |
