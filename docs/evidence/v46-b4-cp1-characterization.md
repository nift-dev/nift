# V4.6 Batch 4 CP1 Characterization

Status: complete baseline for the CP2/CP3 refactor. This checkpoint changes no
runtime behavior.

## Focused Wall

Run:

```sh
make test-v46-b4-cp1
```

The target runs the focused checks below plus the established v4.2/v4.3 control
and resource walls, v4.4 process/package walls, JSON/schema, host, FFI,
concurrency, C/C++ embedding, C ABI, and all maintained binding suites. The
focused additions pin the seams that structured outcomes and contextual error
syntax will change:

- prepared callable `Unsupported` versus prepared failure;
- the current native-method ambiguity, where `false` falls back even with an
  error string;
- exact internal message/source/line/column/source-line/span projection for
  direct, function, method, block-lambda, import, thread and future failures;
- repeated failed `join` and `await` observation;
- current `error`, `try`, `catch` and `throw` identifier behavior;
- the pre-CP3 no-op behavior of bare `throw value` and rejection of try/catch;
- RuntimeType ordinals and fingerprints before Error is appended;
- ordinary Error-shaped JSON remaining an ordinary object;
- C++ script/render output and diagnostic projection on failure.

Known current defects are baselines, not the new contract. In particular,
callable diagnostics use defining-source identity with translated `@return(...)`
source text, workers replay flattened `thread: ` / `future: ` strings, scripts
project failures as `<embed>:0:0`, and prepared native methods cannot distinguish
unsupported dispatch from failure. CP2 may intentionally correct internal
origin transport while preserving the compatibility rules in the approved
design.

## Failure-Origin Matrix

| Boundary | Current origin/projection | Characterization owner |
|---|---|---|
| Direct statement | Supplied logical source, exact line/column/source text/span | `parser_statement_state_unit.cpp` |
| Named function | Defining source plus translated `@return(...)` text | `parser_statement_state_unit.cpp` |
| Struct method | Defining source plus translated `@return(...)` text | `parser_statement_state_unit.cpp` |
| Block lambda | Defining source plus translated `@return(...)` text | `parser_statement_state_unit.cpp` |
| Collection callback | Defining source plus translated `@return(...)` text | `parser_statement_state_unit.cpp` |
| `@script` | Script logical source and translated expression text | `parser_statement_state_unit.cpp` |
| One/two-level import | Importer origin and one `import: ` prefix per boundary | `parser_statement_state_unit.cpp`, `script_import_syntax.sh` |
| Template fragment | Logical render source and expression location | `v46_output_embed.cpp` |
| Future/thread | Observer origin and `future: ` / `thread: ` flattened message | `parser_statement_state_unit.cpp`, v4.5 concurrency tests |
| REPL | Expression, malformed statement, failed import, failed save and worker failures retain parser state; current failed-import diagnostic is silent | `v46_b4_cp1_characterization.sh`, `v43_cp88_repl_lifetime_smoke.sh` |
| C++ ScriptResult | `<embed>`, line 0, column 0 | `v46_output_embed.cpp` |
| C++ RenderResult | Logical render source and parser line/column | `v46_output_embed.cpp` |
| C ABI/bindings | Semantic failure remains a result; captured output survives | C ABI and maintained binding suites |

Prepared/legacy execution remains covered by the v4.4 AST differential corpus,
fuzz wall and language-foundation target. CP1 adds failing prepared and
forced-legacy loop twins and verifies their semantic diagnostic parity. The
direct AST unit additionally pins the empty-error fallback convention that CP2
replaces with `Unsupported`.

## Control And Persistent State

The established v4.2/v4.4 walls, now prerequisites of the CP1 target, remain the
oracle for return, break, continue, fragments, loops, methods, lambdas and
prepared/legacy control flow. CP1 adds no new control state.
`parser_statement_state_unit.cpp` proves a persistent parser continues after
direct, callable, import and repeated worker failures.

The current location-return fields (`last_call_return_loc_*` and the
`PendingControl` root/path pair) are implementation identity, not a promise that
ordinary scalar declarations alias a returned array element. CP2 must preserve
their existing behavior and the root/path corruption regression wall.

## Resource And Identity Matrix

| Resource | Registry/identity source | Current failed-import ownership |
|---|---|---|
| Module environments | Parser-local counter | New entries erased; counter restored |
| Lambdas | Parser-local counter | New entries erased; counter restored |
| Struct instances | Parser-local counter | New entries erased; counter restored |
| Prepared callables | Parser-local map | New entries erased |
| FileValues | Parser-local counter | New entries rolled back; caller files survive |
| Timers | Parser-local instances; process-global parser owner ID | Import rollback has no timer checkpoint; enclosing statement rollback removes newly unreachable timers |
| Streams | Parser-local counter | Import rollback does not erase entries; parser destruction closes the native stream |
| Collections | Parser-local counter | Import rollback does not erase entries; entries live until parser destruction |
| Commands | Parser-local counter | Import rollback does not erase entries; entries live until parser destruction |
| FFI libraries/pointers/buffers/callbacks | Parser-local counters | Import rollback does not erase entries; registry/native-owner destructors release them at parser destruction |
| Futures | Process-global monotonic ID plus parser registry | Import rollback does not erase entries; execution finalization waits; ID remains consumed |
| Threads | Process-global monotonic ID plus parser registry | Import rollback does not erase entries; execution finalization joins; ID remains consumed |
| Mutexes | Process-global monotonic ID plus parser registry | Import rollback does not erase entries; entry lives until parser destruction; ID remains consumed |
| Atomics | Process-global monotonic ID plus parser registry | Import rollback does not erase entries; entry lives until parser destruction; ID remains consumed |

The matrix is derived from the import checkpoint implementation and verified at
representative observable boundaries. Focused owners are
`parser_statement_state_unit.cpp` for import,
FileValue and package-lock rollback; `v46_timer_unit.cpp` and
`v46_timer_smoke.sh` for timer checkpoints; the v4.3 FileValue/stream walls; the
v4.5 FFI and concurrency walls; and `v46_import_worker_ownership_smoke.sh` for
successful import-created workers. CP3 acceptance tests must expand failed-import
ownership to every registry according to the approved fatal lifetime rule.

Parser-local counters may be restored at rollback. Process-global monotonic IDs
must never be rewound or reused.

## Existing Taxonomy Baselines

- Process nonzero exits, POSIX 126/127, launch status, stdout and stderr remain
  structured results under the v4.4 execution/shell wall.
- Runtime malformed JSON, invalid schema definitions and valid schema rejection
  remain distinct under the JSON/schema walls.
- CLI package transactions remain outside language catch surfaces; package
  metadata, provenance and import failures remain separately tested.
- Host Found/NotFound/Error and standard/non-standard host exceptions remain
  fatal through the host seam and embedding tests.
- FFI load/symbol/signature/pointer/callback behavior remains under the v4.5 FFI
  and adversarial runtime walls.
- Stream lifecycle behavior and Linux backend read/write/flush/close detection
  remain under the v4.3 stream wall; the probes explicitly retain known missing
  read/close detection for CP4.
- Output captured before failure remains available across C++, C ABI and every
  maintained binding.

These established walls run again at the combined Batch 4A gate. CP1's focused
target is deliberately additive rather than a duplicate aggregate suite.
