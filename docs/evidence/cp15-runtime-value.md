# CP15 RuntimeValue substrate evidence

Date: 2026-09-29

Status: implementation candidate under review; Review Gate 6A is pending. CP16
and bytes APIs have not started.

## Architecture

- `nift::RuntimeValue` recursively owns null, boolean, ordinary number,
  exact-number spelling, string, array and insertion-ordered object values.
- Evaluator variables and locations, AST literals/results, callable arguments,
  collections, mutex/thread/async payloads, Engine defaults/results and public
  C++ `Value` storage use `RuntimeValue` rather than `json::Document`.
- Embedded host-callable arguments and results cross `RenderHost` directly as
  runtime values rather than being serialized through the JSON DOM.
- Page metadata, hierarchy member results and environment snapshots also cross
  `RenderHost` as runtime values. JSON remains at true data boundaries.
- `runtime_from_json` and `runtime_to_json` are the explicit recursive bridge.
  Strict JSON parsing, JSON Schema validation, front matter, project models,
  configuration/tracking and persisted-data caches remain `json::Document`.
- Jsonic and Minify++ sources and type enums are unchanged. There is no bytes
  tag, constructor, operation, I/O API, embedding API or package/HTTP change.
- Exact Jsonic `StrNumber` spelling and numeric value survive both conversion
  directions. Runtime text/JSON output remains behavior-compatible.
- Spread passes runtime elements directly. Destructuring classifies extracted
  values directly. Nested compound/increment assignment uses a runtime temporary
  binding. `distinct` uses recursive runtime fingerprints as candidate buckets
  and confirms duplicates with runtime structural equality. These paths no
  longer serialize a runtime value and parse it back.
- Project runtime models are lazy, generation-scoped and shared. Their complete
  transient JSON model is discarded after conversion; only the runtime tree and
  compact fingerprint are retained.
- ProjectInfo content-model and hierarchy indexes are also lazy, mutex-protected
  generations. Config/build schema and taxonomy reloads reset the content model;
  track, untrack and watch reconciliation reset hierarchy state. Both propagate
  invalidation to the project runtime model and fingerprint.
- Strictly parsed `@json` and contract values have shared runtime caches scoped
  to the build generation, immutable snapshot or Engine/standalone host cache
  generation.

## Focused evidence

All commands passed in this workspace:

```text
make -j2 nift
make test-runtime-value
make test-engine-bindings
tests/v44_ast_constant_fold_smoke.sh
make test-engine-loaders test-engine-concurrency
make test-project-state test-project-host
NIFT_BIN="$PWD/nift" tests/v43_cp176_cp182_collection_algebra_smoke.sh
make test-v43-language
make test-v44-language-foundation
NIFT_BIN="$PWD/nift" tests/v43_surface_robustness_smoke.sh
NIFT="$PWD/nift" tests/v44_cp4_spread_smoke.sh
NIFT_BIN="$PWD/nift" tests/v44_element_assignment_smoke.sh
NIFT_BIN="$PWD/nift" tests/v43_final_language_smoke.sh
NIFT_BIN="$PWD/nift" tests/collection_ops_smoke.sh
NIFT_BIN="$PWD/nift" tests/v44_root_path_corruption_reproducers.sh
bindings/python/build.sh
(cd bindings/python && python3 -c 'import nift; print(nift.Engine)')
(cd bindings/python && python3 -m unittest tests.test_nift)
bindings/node/build.sh
(cd bindings/node && node -e "require('./build/nift_node.node'); require('./lib/nift.js')")
(cd bindings/node && node --test test/nift.test.js)
make test-build-boundary
```

`test-runtime-value` recursively checks JSON ingress/egress, nested ownership,
copy independence, duplicate-key equality/fingerprints, positive and negative
2^53 boundaries, adjacent positive/negative 30-digit integers, underflow-scale
exponents, equivalent decimal/exponent spellings, arbitrary-length exponents,
NaN and infinities. The collection wall checks `unique_by` with exact huge,
underflow-scale and equivalent-spelling keys. Engine-visible coverage checks all
six NaN equality/relational operators, infinity equality/order, and both
`unique_by` and global `distinct` over repeated scalar/nested NaNs and
infinities. Two independent NaNs are explicitly checked to share a fingerprint
while remaining structurally unequal.

## Compatibility walls

All completed successfully:

```text
make test-json test-json-schema test-engine test-public-header
make test-c-abi test-c-abi-c-smoke
make test-engine-project test-conformance
make test-json-binding test-json-schema-integration test-markup-json-directives
make test-v44-language-foundation
make test-v45-integration-dogfood test-v45-adversarial-runtime
make test-v43-language
make test
make test-embed
make test-project-state test-project-host
```

The v4.3 wall includes exact-number, object-expression, JSON, destructuring,
collection, hierarchy, typed-content and scaling coverage. The latest project
dependency scaling result was 0.050 s for 1,000 pages and 0.180 s for 4,000
pages (3.6x for 4x pages), with at most five dependencies per page. These are
local regression data, not cross-machine performance claims.

An initial parallel invocation of independent Make targets produced a transient
`Text file busy` while two processes relinked `nift`; the affected wall was
rerun sequentially and passed. An initial implementation converted the project
model once per page and failed the scaling wall at 16.5x; conversion was moved
to the immutable host/project cache, after which the complete wall passed.

## Review repairs

- Replaced non-resettable ProjectInfo project/runtime `once_flag` caches with a
  mutex-protected generation cache. Track/untrack invalidation, tracking/config
  reload and build-cache reset invalidate the runtime model and fingerprint. A
  persistent ProjectInfoHost regression exercises track, untrack, reload and a
  targeted build generation through the same host instance.
- Replaced the remaining content-model and hierarchy `once_flag` caches with
  mutex-protected shared generations. Persistent-host coverage changes schema
  fields and taxonomy hierarchy across a build generation, and checks hierarchy
  children after track/untrack through the same host without eager construction.
- Made ProjectHost project binding materialization lazy. ProjectInfo and
  ProjectState retain no complete project JSON tree alongside RuntimeValue.
- Migrated host callables, page metadata, hierarchy members and environment
  snapshots to direct RuntimeValue transport.
- Added shared runtime caches for `@json` and contracts, with reset/snapshot
  lifetimes matching each host. Schema reads retain strict JSON caches.
- Centralized Number/StrNumber comparison and numeric fingerprints around a
  normalized arbitrary-length decimal coefficient and signed exponent. This
  distinguishes adjacent integers beyond int64 and underflow-scale values while
  unifying equivalent decimal/exponent spellings. Host NaN is unequal to every
  value; infinities equal only the same infinity and are ordered without integer
  casts. `unique_by` consumes the centralized numeric fingerprint while keeping
  its existing resource and collection key handling.
- Split user-visible numeric relations from deterministic internal ordering.
  Relational comparability rejects NaN, making `<`, `<=`, `>`, and `>=` false,
  while sort/min paths retain a safe deterministic order suitable for
  strict-weak sorting. `unique_by` now
  treats fingerprints as buckets and confirms candidates with structural
  equality, so unequal NaNs and hash collisions cannot collapse values.
- Applied the same fingerprint-bucket plus structural-equality rule to global
  `distinct`; first occurrence order and expected near-linear lookup behavior
  are preserved without treating a fingerprint as an equality identity.
- Added `RuntimeValue.cpp` to every independent source list that builds the
  migrated Parser/Value/Ast graph: direct Python and Node binding builds,
  Python `setup.py` staged wheel/sdist compilation, the release Engine
  benchmark, and the direct AST constant-fold smoke linker. The main Makefile
  already contained it. Python `MANIFEST.in` already recursively includes all
  staged native `.cpp` files, while the Node package manifest intentionally
  ships only the built addon, so neither package manifest needed a change.
- Direct Python and Node build/load checks passed. Python's 23 focused tests and
  Node's 26 focused tests passed. RuntimeValue, Engine binding, AST, global
  collection, collection-algebra and non-destructive clean-tree build-boundary
  checks also passed.
- Corrected duplicate-key object equality as order-independent matching of all
  key/value occurrences. Equality no longer performs redundant `has` and lookup
  scans; insertion-ordered vector storage and linear lookup remain unchanged
  rather than introducing a broad object redesign.
- Restored HEAD public Value mutation exception text and made numeric object
  index diagnostics independent of local, host, contract or `@json` provenance.

Review Gate 6A remains pending. Remaining `json::Document` uses in
evaluator-adjacent code are strict parse, persistence, schema, project-model or
explicit JSON output boundaries. Jsonic and Minify++ have no source changes.

CP15 intentionally does not establish bytes storage, bytes semantics,
serialization rejection, binary I/O, FFI bridges, embedding exposure or
binding API changes. Hosted cross-platform and sanitizer jobs remain
release-level coverage rather than local Gate 6A evidence.
