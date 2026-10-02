# v4.6 Batch 4 CP8 embedding / C ABI certification

Status: complete. Certifies that every new v4.6 recoverable/fatal outcome
projects correctly through the existing public embedding/binding boundary
without changing the ABI. Certification-oriented; no new public Error
transport, no C ABI Error object, no struct-layout change, no binding
result-model redesign, and no public exception transport were introduced.

## Boundary contract preserved

| Outcome | Public projection |
| --- | --- |
| successful execution | existing successful result/value |
| uncaught recoverable failure | existing failed `ScriptResult` / render result / binding error (message + origin) |
| fatal failure | existing failed projection |
| value graph containing Error (recursive) | rejected at the boundary — no stringify/coercion fallback |

`err.stringify()` / `err.prettify()` are ordinary string data and may cross
the boundary. `RuntimeType::Error` remains interpreter-internal.

## Certified (tests/v46_b4_cp8_embed.cpp)

- Uncaught built-in recoverables through `nift::Engine::execute` project as
  ordinary failed results with a non-empty human-readable message and origin:
  filesystem `open` failure, stream `ifstream` open failure,
  `validate()` schema rejection (`expected string, received number`), FFI
  library-load failure (`ffi.library_load_failed`), `throw error(...)`, and a
  deliberately missing FFI symbol against the reused known-good fixture
  (`ffi.symbol_not_found`). No Error value escapes.
- `json.parse_failed` through the template render boundary: an `@json(name,
  path)` directive reading malformed runtime data (the exact CP4c-classified
  producer) yields a failed `RenderResult` with a `failed to parse` message and
  a populated source, and a subsequent render on the same Engine succeeds.
- Import-source recoverable through a file-backed embedded project
  (`import("./missing.f")`) -> failed result with `not readable`; embedded
  hosted mode performs no shell package resolution, so a package-name import
  projects the unresolvable import source as an ordinary failed result
  (`package.not_installed` is CLI/package-host-side and certified by the CP5b
  wall).
- Worker outcomes through embedding: a built-in recoverable produced inside an
  async worker and observed uncaught at `await` projects as a failed result;
  the same worker recoverable caught in-script lets the script then return a
  normal success (`42`); a worker fatal failure (`division by zero`) bypasses
  in-script catch and projects as a failed result at the boundary.
- Context reuse: a subsequent successful `execute` after both a recoverable and
  a fatal failure returns a normal success result (`7`, `8`).
- Recursive Error rejection: a returned graph containing a nested Error
  (`{"ok": true, "inner": [error(...)]}`) is rejected with an
  `Error value` message (no stringify/coercion). The recursive
  public-boundary rejection matrix is split deliberately: CP3's embed wall is
  the base matrix (direct Error, array, object, collection, struct); CP8
  re-certifies and adds the nested-combination graph. `err.stringify()` /
  `err.prettify()` remain ordinary string data at the boundary.

## Maintained external consumers

All four maintained language consumers plus the staged embedding consumer were
built, linked and their contract tests executed explicitly against the
unchanged C ABI (no binding source changes were required):

| Consumer | Result |
| --- | --- |
| Python (`make test-python-binding`) | 25 tests OK |
| Node (`make test-node-binding`) | 1 test, 0 failures |
| Go (`make test-go-binding`, `-race`) | ok (embed + harness packages) |
| C# (`make test-csharp-binding`) | 29 passed, 0 failed |
| staged embedding consumer (`make test-v45-embed-staged-consumer`) | staged C + C++ consumers PASS |

Each consumer sees the same public value/result model and reports failures
through its existing error/result convention; none required changes for the
v4.6 Error model.

## ABI compatibility

- `include/nift/*.h` public headers are byte-identical to the prior approved
  baseline (`4368dea`), and `src/embed/c_abi.cpp` + `include/nift/c_abi.h` are
  unchanged across CP7/CP8.
- The exported `nift_*` C symbol surface from `libnift_c.a` exactly matches the
  frozen ABI 1.1 baseline (an exact set match catches both removals and
  additions). No removed/renamed symbol, changed signature, struct layout,
  enum ordinal, or ownership/allocated-string-lifetime change.
- The internal recoverable/fatal distinction neither turns failures into
  success, leaks Error as a value, throws across the C ABI, nor changes return
  codes or binding exception/error conventions.

## Sanitizers

The existing ASan/UBSan and TSan gates (which cover error projection,
allocated error strings, repeated calls after failure, context reuse, worker
failure through embedding, and recursive Error rejection paths) are re-run as
part of the CP8 review gate; no leak/use-after-free/double-free findings.

## Wall

```sh
make test-v46-b4-cp8
```

runs the full Batch 4B/CP5a/CP6/CP5b/CP7 aggregate gate, then the CP8 wall:
the focused CP8 embed binary (with the reused FFI fixture for
`ffi.symbol_not_found`), the existing embedding/C ABI walls
(`test-c-abi`, `test-c-abi-c-smoke`, `test-engine`, `test-engine-bindings`,
`test-engine-concurrency`), the maintained external consumers
(`test-node-binding`, `test-python-binding`, `test-go-binding`,
`test-csharp-binding`) and the staged embedding consumer
(`test-v45-embed-staged-consumer`), the public-header-vs-baseline check, and
the frozen exported-symbol surface match.

The maintained language-binding consumers (Python/Node/Go/C#) are thin
adapters over this unchanged C ABI and are exercised by the existing binding
walls in the wider suite.