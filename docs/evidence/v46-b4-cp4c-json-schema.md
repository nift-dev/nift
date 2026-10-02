# v4.6 Batch 4 CP4c runtime JSON parse and schema rejection

Status: complete. Converts only the approved runtime JSON parse failure and
valid-schema rejection to recoverable Errors. No filesystem/stream conversion
here (CP4a/CP4b); malformed project/config metadata, invalid schema
definitions, argument/type errors, and authority/policy denials stay fatal.

## Converted producers and codes

| Producer | Failed operation | Code |
| --- | --- | --- |
| `@json(name, path)` | malformed JSON in a data file | `json.parse_failed` (recoverable) |
| `validate(schema, value)` | a valid schema rejects the candidate | `schema.rejected` (recoverable) |
| `@json(name, schema, path)` / `@json(name, schema){...}` | a valid schema rejects the instance | `schema.rejected` (recoverable) |

## Kept fatal

- Malformed inline `@json { ... }` (template-authored static JSON) -> plain
  fatal render error.
- Invalid schema definition (bad shape, unsupported keyword): `validate()`
  and `@json` both report `schema.definition_invalid` (fatal) and bypass
  `catch`.
- Schema or instance values that cannot be projected to JSON
  (`runtime_to_json` failure): `schema.definition_invalid` for the schema and
  `native.unsupported_value` for the candidate in `validate()`; fatal in
  `@json`.
- Argument/arity errors, path escape/authority denials, missing files.

`jsonschema::schema_valid()` was added so producers distinguish a malformed
schema definition (fatal) from a valid schema rejecting an instance
(recoverable) instead of relying on `jsonschema::validate`'s conflated result.

## inject()

`inject(path)` remains unchanged. It is a dual-mode loader (strict-JSON fast
path plus the Nift-expression compatibility path for `.expr` files), so a
malformed-JSON classification would require a content-shape discriminator that
could misclassify Nift expression-valued object/array literals. Malformed
`inject` input continues to fail fatally through the expression path; this is
a documented limitation rather than a catchable `json.parse_failed` producer
in CP4c.

## Wall

```sh
make test-v46-b4-cp4c
```

runs the immutable Batch 4A gate, pre-CP4, CP4a, CP4b, the stream-operator
wall, and the focused CP4c wall. The CP4c wall covers: `validate()` rejection
catchable with code/category/origin/message; candidate pass-through; invalid
schema fatal and not caught; uncaught compatibility (message preserved, no
Error serialization); `@json` malformed file and schema rejection catchable
via `@script`; `@json` invalid schema fatal; malformed inline `@json` fatal;
and argument/authority misuse fatal.

The established JSON/schema walls (`json_binding_smoke`,
`json_schema_integration_smoke`, `markup_json_directives_smoke`,
`v43_cp124_json_object_wall`, `v43_cp127_eval_json_smoke`) remain green.
(`v44_json_inject_fastpath_smoke.sh` is an orphaned pre-existing stale test
using `nift run` syntax that the current CLI does not provide; it fails
identically on the baseline revision and is not part of any gate.)