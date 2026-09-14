# JavaScript optimizer phase 0 evidence

Phase 0 establishes permanent correctness gates before optimizer work changes emitted JavaScript.

## Regression coverage

- JavaScript identifier roles distinguish bindings, references, member properties,
  object keys, labels, and contextual identifiers.
- Object shorthand expansion is limited to object literals and object patterns.
- Meaningful empty statements, including loop bodies such as `while (condition);`,
  remain covered by the structured semantics suite.
- The twelve benchmark artifacts are checked in conservative, structured, and
  aggressive modes for a total of 36 parse validations.
- When the sibling `minification-benchmarks` checkout and its dependencies are
  available, the same 36 outputs are also run through each artifact's benchmark
  validator. The gate skips explicitly when those optional fixtures are absent.

Run the permanent real-world gate with:

```sh
make test-real-bundles
```

The full correctness baseline remains `make test`, supplemented by Test262,
TypeScript JSX/TSX conformance, sanitizer, and generated differential runs
documented with subsequent optimizer phases.
