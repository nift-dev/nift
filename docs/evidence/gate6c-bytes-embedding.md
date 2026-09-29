# Review Gate 6C - bytes embedding contract

Result: **pass** on 2026-09-30 for CP21 commit
`1872d4a1fce90deb7218373b282b3d76f46c4eeb`.

## Frozen surface

- Public C++ remains `nift::Value::Type::Bytes`, `nift::Value::Bytes`,
  `Value::is_bytes()`, `Value::bytes()` and the existing generic `Engine::set`,
  `Context::set` and `ScriptResult::value` transport. No redundant named C++
  byte setters are added.
- The additive C ABI version is 1.2. Its frozen byte names are `nift_bytes`,
  `nift_engine_set_bytes`, `nift_context_set_bytes` and
  `nift_script_result_value_bytes`.
- Byte setter inputs are pointer plus length and are copied before return.
  Null data with length zero is empty; non-null data with length zero is also
  empty; null data with positive length is `NIFT_ERROR_INVALID_ARGUMENT`.
  Binding names must be non-null, non-empty valid Nift identifiers and must not
  be structural built-ins.
- `nift_bytes` is `{const uint8_t* data, size_t length}`. A successful
  top-level bytes accessor returns a borrowed immutable view owned by the
  `nift_script_result`; empty bytes are `{NULL, 0}`. The view survives later
  engine calls and engine destruction, and expires when the result is freed.
  Callers copy to retain and never mutate through the pointer.
- Byte extraction is top-level only. Recursive array/object typed traversal,
  writable views, pinning and retained native pointers are not ABI 1.2.
- Execute/evaluate retain successful bytes results without eager JSON
  serialization. `nift_script_result_value_json` converts lazily and caches
  successful JSON under a result-local mutex. Top-level or nested bytes keep
  `nift_script_result_ok(result) == 1`, while the incompatible JSON accessor
  clears its output and returns `NIFT_ERROR_INVALID_ARGUMENT`. The C++ checked
  JSON diagnostic remains `bytes values are not JSON serializable`.
- Mismatched accessors, null results and unsuccessful results clear supplied
  output views and return `NIFT_ERROR_INVALID_ARGUMENT`; unexpected contained
  C++ failures return `NIFT_ERROR_INTERNAL`.
- `include/nift/c_abi.h` is canonical. The Go cgo copy at
  `bindings/go/cabi/include/nift/c_abi.h` must remain byte-identical; staged
  development/release prefixes copy the canonical public headers.

## Defect review

Independent review initially found one medium portability defect: a null,
zero-length binding name could reach `std::string(nullptr, 0)`. The shared
binding validation now rejects null and empty names before construction, with
focused Engine and Context regressions. Re-review found no remaining blocker,
high or medium issue.

Residual low risks are accepted: C ABI JSON rejection classification recognizes
the frozen C++ bytes-serialization diagnostic, and native macOS/Windows proof is
provided by hosted execution rather than the local Linux machine.

## Certification

Local walls passed:

- `make test`
- `make -j2 test-embed`
- `make -j2 test-bindings`
- `make -j2 test-cp18-bytes-sanitize`
- `make -j2 test-cp18-bytes-tsan`
- `make test-test-integrity test-guarantee-registry-ci`
- `git diff --check`

Exact-SHA hosted runs for `1872d4a` all passed:

| Workflow | Run |
|---|---|
| Checkpoint 10 cross-platform equivalence | `36599462222` |
| Init targets | `36599462100` |
| v4.4 cross-platform | `36599462329` |
| Hosted certification diagnostic | `36599462356` |
| Performance regression guards | `36599462255` |
| Packaging matrix | `36599462308` |
| Gate 6A-R vendored libffi | `36599462341` |
| Gate 6B bytes value semantics | `36599462263` |
| Test integrity guards | `36599462206` |

## Decision

Gate 6C is closed. CP22 may expose top-level bytes through the maintained
bindings without redesigning this surface. Gate 7 remains the hard stop before
HTTP, WebSockets, networking, crypto, SQLite or any other proof consumer uses
bytes.
