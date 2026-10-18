# Machine-readable diagnostics — schema 1 (D-022)

Status: specification (CP13). Implementation: CP13 tasks T2, T5, T9
(`checkpoints/cp13-diagnostics-design.md`). JSON Schema:
[`diagnostics-schema-v1.json`](diagnostics-schema-v1.json). Fixtures:
`tests/runtime2/diagnostics/json/`, checked by
`tests/runtime2/diagnostics/json_check.py`.

## Enabling

| Mechanism | Effect |
| --- | --- |
| `--format=json` (global option, before the command or script) | JSON diagnostics for this invocation |
| `NIFT_FORMAT=json` | same, for every invocation in the environment |
| `--format=text` / `NIFT_FORMAT=text` / unset | text mode (default) |

The option wins over the variable. Any other value is a usage error (text,
exit 2). Discoverable through `nift eval --capabilities`, which gains
`"diagnostics": {"formats": ["text", "json"], "schema": 1}`, and through
`nift help` (one line).

Text mode is unchanged byte for byte (the CP13 gate); call chains, codes,
spans and hints appear only in JSON (D-022 #1).

## Stream contract

* Diagnostics go to **stderr** as JSON Lines: one object per line, UTF-8,
  nothing else on stderr — no colour, no excerpts, no progress text. Warnings
  are records too (`severity: "warning"`).
* **stdout is unchanged** (program output, build progress lines, `eval`
  values).
* **Exit statuses are unchanged** (1 for program/build errors, 2 for `eval`
  failures and usage errors, 70 for VM internal failures).
* A failing invocation always produces at least one valid diagnostic record
  (never an empty stderr with a non-zero exit, never a partial line), even
  for internal failures (`internal.*` codes).
* `build` emits one record per failed page — in no specified order — followed
  by one summary record. Scripts, `-e` and `eval` emit no summary.

## Diagnostic record

| Field | Meaning |
| --- | --- |
| `nift_diagnostic` | schema version, `1` |
| `severity` | `error`, `warning`, `note` |
| `code` | stable identity from `src/Diagnostic.h` (`domain.name`), or the `user.*` code of a thrown Error |
| `catchable` | whether a `try` around this raise would catch it under the current language rules (separate from `code` and `severity`; D-022 refinement) |
| `message` | message **without** legacy compatibility prefixes (`return: `, `import: `, `thread: `, `future: `, `lambda body error: `); the structure they encode is in `frames` |
| `span` | primary span: `source` (`file`, `command-line`, `eval`, `stdin`), `file` (path exactly as text mode prints it), 1-based `line`/`column` (byte columns, as text mode) and `end_line`/`end_column` (one past the last byte). The failing expression (D-018c); `null` only when there is no source position |
| `context` | `kind` (`script`, `command-line`, `eval`, `build`, `page`, `import`, `repl`), `entry` (entry unit), `page` (tracked name in builds), `phase` (`validate` = rejected by syntax prevalidation before anything ran, D-022 #3; `run`), `engine` (`legacy`, `vm`, `none`) |
| `frames` | call chain, innermost first: `kind`, `name` (`<top>` for the unit's top level), `call_site` (where the frame was entered; `null` for the outermost frame or when unknown). At most 64 (first 32 + last 32) |
| `frames_omitted` | frames dropped by the cap |
| `frames_complete` | `false` when the engine could not record call sites (legacy evaluator: kinds and names only) |
| `cause` | optional: cause chain of a thrown Error (code, message, span; ≤ 16) |
| `hint` | optional fix hint, `null` if none (never part of `message`) |
| `text` | the exact text-mode rendering of this diagnostic (multi-line for build diagnostics) — consumers can always fall back to it; the corpus checks it byte for byte |

Caught errors produce no record (they are values; `e.stringify()` is
unchanged). An uncaught `throw` is `catchable: true` — it could have been
caught — with the Error's own code.

## Summary record (build)

`{"nift_summary": 1, "command": "build", "exit_status": 1, "errors": 2,
"warnings": 0, "pages_failed": ["a", "b"], "pages_built": 1}`.

## `eval --json`

Success: unchanged (value as JSON on stdout, eval contract). Failure: one
diagnostic record on stderr (`context.kind: "eval"`, `span.source: "eval"`)
instead of `eval: <message>`, exit 2. `eval --capabilities` reports
`"contract": 2`. `eval --json` implies JSON diagnostics for that invocation.

## How each engine fills it

| Field | VM | Legacy (until CP25) |
| --- | --- | --- |
| `code` | set at the raising instruction/native (cold path) | `fail_recoverable`/`fail_fatal` codes; otherwise the message-to-code table (`tests/runtime2/diagnostics/code-table.tsv`) |
| `span` | `Proto::offsets` + the failing node's extent | `Parser::fail` origin (`source_length`) |
| `frames` | walk of the heap frame stack at the stop: callee proto name, caller call site = `offsets[ret - 1]` — O(depth), error path only | `Diagnostic::frames` kinds and labels (callables, imports, `@script`, futures, threads), `frames_complete: false` |
| `catchable` | the catch rule applied to this raise | `active_recoverable_` set for this raise |
| `text` | the text line the engine prints | same |

## Versioning

Additive fields keep `nift_diagnostic: 1`; consumers must ignore unknown
fields. Removing or changing the meaning of a field, or of a code, needs
`nift_diagnostic: 2`. Codes are append-only: a code is never renamed or
reused; adding a code never changes catchability.

## Tests (requirement: nested calls, caught and uncaught errors, imported code, several page failures)

`tests/runtime2/diagnostics/json/`: `nested_calls` (two-deep callee error,
full chain), `uncaught_throw_in_function` (user code, catchable but
uncaught), `caught_error` (no record, exit 0), `imported_error` (import
frame, prefix only in `text`), `syntax_before_execution` (phase `validate`,
no output), `eval_error` (eval contract 2), `sites/multi_page_failures` (two
page records + summary). `json_check.py --binary ./nift` runs each with
`NIFT_FORMAT=json` and `--format=json` and checks valid JSON Lines only, no
terminal escapes, schema validity, unchanged exit status and stdout, records
as a multiset, and `text` byte for byte. Codes for the whole corpus:
`tests/runtime2/diagnostics/changes/CODES.tsv`.
