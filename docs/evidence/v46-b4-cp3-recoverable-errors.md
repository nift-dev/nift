# V4.6 Batch 4 CP3 Recoverable Errors

Status: complete. CP3 implements the Error value, explicit recoverable outcomes,
contextual `throw` and `try`/`catch`, worker replay, import lifetime rules, and
public-boundary containment approved by the Batch 4 design.

## Certification

Run:

```sh
make test-v46-b4-cp3
```

The target passed on 2026-10-02. It includes the complete CP1 and CP2 walls,
runtime-value and diagnostic-outcome units, C and C++ embedding, C ABI, Go,
C#, Node and Python bindings, the AST differential corpus, resource and
concurrency suites, and the focused CP3 shell and embedding tests.

Focused repair verification also passed:

```sh
make -j2
make test-runtime-value
.build/diagnostic-outcome-unit
.build/v46-b4-cp3-embed
bash tests/v46_b4_cp3_recoverable_errors.sh
git diff --check
```

## Contract Coverage

- `RuntimeType::Error` is appended without changing existing ordinals. Error
  data is immutable, validates `user.*` codes and bounded causes, preserves its
  first trusted origin, and has canonical object-shaped diagnostic rendering.
- `EvalOutcome`, `ExecOutcome`, and `WorkerCompletion` distinguish recoverable
  Error propagation from fatal diagnostics and unsupported prepared dispatch.
- Contextual syntax preserves ordinary bindings, members, callables and
  compound assignments named `error`, `throw`, `try`, and `catch`.
- Fatal failures bypass catches. Rethrow, callable, prepared loop, future,
  thread, and import propagation preserve the defining Error and source origin.
- Futures and threads replay recoverable completion on repeated `await` and
  `join`; fatal completion remains uncatchable.
- Every failed worker-owning import becomes `internal.import_lifecycle`, bypasses
  catch, and retains its complete ownership graph until top-level worker
  finalization. Cleanup and parser-local identity rollback occur afterward.
- Error values cannot escape through rendering, interpolation, direct template
  joins, files, streams, script returns, CLI eval, public Value conversion,
  Engine/Context bindings, or host callables, including nested arrays and
  objects.

## Independent Review

Independent blocker reviews exercised exact callable and worker origins,
prepared call/method/loop propagation, repeated worker observation, malformed
cause graphs, all failed-import validation branches, nested active-module struct
imports, direct template joins, CLI interpolation, and recursive public graph
containment. The final review reported no checkpoint-blocking findings.

CP4 remains responsible for converting approved native failure producers from
`internal.legacy_failure` to the CP2 registry. CP3 does not begin that migration.
