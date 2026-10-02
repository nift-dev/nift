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
  library-load failure, and `throw error(...)`. No Error value escapes.
- Import-source recoverable through a file-backed embedded project
  (`import("./missing.f")`) -> failed result with `not readable`; embedded
  hosted mode performs no shell package resolution, so a package-name import
  projects the unresolvable import source as an ordinary failed result
  (`package.not_installed` itself is certified on the CLI side by the CP5b
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
  `Error value` message; the CP3 embed wall already covers direct/array/object/
  collection/struct forms.

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
the focused CP8 embed binary, the existing embedding/C ABI walls
(`test-c-abi`, `test-c-abi-c-smoke`, `test-engine`, `test-engine-bindings`,
`test-engine-concurrency`), the public-header-vs-baseline check, and the frozen
exported-symbol surface match.

The maintained language-binding consumers (Python/Node/Go/C#) are thin
adapters over this unchanged C ABI and are exercised by the existing binding
walls in the wider suite.