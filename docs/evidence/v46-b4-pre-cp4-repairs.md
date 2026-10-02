# v4.6 Batch 4 pre-CP4 repairs

Status: complete. Small independent reviewed checkpoint before the CP4 producer
conversions begin. No catchability is added here.

## P1 - Error persistence-boundary repair

`serialize_value` can either render an Error into its canonical diagnostic
object form (the `stringify()`/`prettify()` presentation surface) or reject
it. Generic persistence APIs must never implicitly convert an
interpreter/control-flow Error into an ordinary persisted object, because the
type distinction would be silently lost.

Behavior:

- `file.write_val(...)` and `stream.write_val(...)` now reject an Error value
  that is direct or recursively nested inside an array, object, collection, or
  struct, with `Error values are not serializable`.
- The deliberate presentation surface is unchanged: `err.stringify()` and
  `err.prettify()` still produce the canonical object-shaped diagnostic
  representation, and code may write that text explicitly (for example
  `file.write(err.stringify())`).
- `serialize_value` gained an internal `reject_errors` flag (default false) so
  the REPL display path and the presentation chain keep their existing
  behavior.

Tests: `tests/v46_b4_precp4_repairs.sh` covers direct, array, object,
collection and struct containment for both FileValue and stream `write_val`,
plus the explicit `stringify()` write path.

## P2 - remove dead incorrect `evaluate_outcome()` wrapper

`ast::evaluate_outcome()` had no callers and its implementation did not
integrate the recoverable/unsupported distinction used by the production
`ast::evaluate()` + `call_outcome`/`native_method_outcome` mechanism. It was
removed (declaration and definition). The tested production AST dispatch is
unchanged.

## P3 - failed-try render-buffer semantics

Frozen as the current behavior and pinned by test:

- External effects already emitted are not rolled back: `print("already-sent")`
  inside a try body survives a later throw and remains visible to stdout.
- Buffered render output produced inside a try body that later throws is
  discarded when the recoverable failure is caught: `$[1 + 1]` before a throw
  does not commit "2" to the render output.

This is a deliberate transactional distinction between the uncommitted render
buffer and effects already sent outside the parser. No semantic change was
required; the behavior was characterized and pinned.

## Gate

```sh
make test-v46-b4-pre-cp4
```

runs the complete immutable Batch 4A CP3 gate (CP1/CP2 prerequisites, runtime
value and diagnostic-outcome units, CP3 embed, CP3 shell wall, AST
differential coverage, C ABI, and maintained bindings) and then the focused
pre-CP4 repair wall above.