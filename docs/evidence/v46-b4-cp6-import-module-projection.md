# v4.6 Batch 4 CP6 import/module/source-origin/frame reconciliation

Status: complete. Reconciles and certifies the import/module/template/REPL
source/origin/frame model and rollback-before-catch guarantees. One genuine
REPL routing defect was found and fixed; the remaining behavior is certified
and pinned.

## Reconciliation model

Every failure path now has a stable answer to: defining source, observer
source, diagnostic/Error origin, frame stack, rollback point, and uncaught
message.

- **Recoverable failures** (explicit `throw`, and the CP4/CP5 operational
  families): the Error value and its projection use the **defining source**
  origin (e.g. a `throw` inside `leaf.f` observed across two import
  boundaries reports `leaf.f`, with `import: import: <message>` frames).
- **Fatal failures**: the structured diagnostic origin is the **defining
  source**; the legacy projection keeps the CP1-pinned importer/call-site
  location (`error: <importer>: import: import: <message>`) as the
  compatibility surface.
- **Import frames**: one `import: ` frame per boundary, appended rather than
  flattened; nested failures show `import: import: ...`. Frames describe
  propagation; they do not overwrite the defining origin.
- **Imported callables**: a recoverable failure preserves the defining source
  in the Error (verified); the fatal projection shows the call site (existing
  legacy compatibility surface; the structured origin is the defining source).
- **Rollback-before-catch**: struct/type/callable/variable registrations from
  a failed import are rolled back before the importer's `catch` runs; the
  worker-owning failed import remains deterministically fatal
  `internal.import_lifecycle` and bypasses catch even when the underlying
  failure is recoverable, with cleanup deferred to top-level worker
  finalization.
- **Relative import ownership**: relative imports resolve from the defining
  module, never the consumer/project root (Batch 3 rule preserved).
- **In-memory sources** use the synthetic `<command-line>` label; no CWD
  fallback is introduced.
- **Workers/async from imported code**: recoverable failures in imported
  callables propagate through `join`/`await` with the defining origin.
- **Prepared/legacy**: imports execute once (side-effect fixture proves a
  single execution); no duplicate frame/rollback.

## Defect found and fixed

**REPL modern `import(...)` was misrouted as an external command.** The REPL
shell loop calls `shell_tokens` which strips quotes, so `import("./x.f")`
became a token `import(./x.f)` containing `/`, and the executable-path
fallback (`toks[0].find('/')`) classified it as a command. `execute_shell_command`
then reassembled it as `import(./x.f)()` and swallowed the failure silently:
the module never ran, exports never installed, and no diagnostic was shown
(while legacy `@import(...)` worked). This also explained why the CP1
characterization recorded a "silent failed REPL import diagnostic" baseline.

Fix (`src/CLI.cpp`, REPL routing only): the executable-path fallback now
requires the first token to contain no `(` — a paren-bearing token is Nift
call/statement syntax, never a path command. Modern REPL imports now execute,
install exports, and report their own failures; `@import` and ordinary command
fallback are unchanged (verified by a differential against baseline).

The CP1 characterization test was updated to pin the corrected behavior: a
failed REPL import now reports its own `import: ...` diagnostic while still
not installing later exports, and the REPL stays usable. This was a bug
baseline (the CP1 evidence itself notes known defects are baselines, not the
new contract); no other pinned message changed.

## Wall

```sh
make test-v46-b4-cp6
```

runs the full Batch 4B/CP5a aggregate gate, then the focused CP6 wall. The wall
covers: direct/nested import frames; recoverable defining-origin across
imports; imported-callable recoverable origin; relative import ownership;
`module_path` provenance; rollback-before-catch (struct/type/callable/var
removed); worker-owning failed import fatal rule; `@script` recovery and
template-import projection; worker/async from imported code; in-memory source
label; single import execution; REPL modern import execution, failed-import
recovery, and stale-state-free continuation; and uncaught compatibility.

The established Batch 3 provenance/resource walls, package/import walls,
FFI wall, concurrency smoke, and REPL walls remain green.