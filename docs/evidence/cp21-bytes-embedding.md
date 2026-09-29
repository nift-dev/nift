# CP21 bytes embedding

## Contract

- The C++ embedding surface remains `nift::Value::Bytes` transported through
  the existing generic `Engine::set`, `Context::set` and `ScriptResult::value`
  APIs. Immutable backing may be shared by C++ value copies.
- C ABI 1.2 adds `nift_engine_set_bytes` and `nift_context_set_bytes`. Inputs
  use pointer plus length, are copied completely before return, accept either
  null or non-null data at length zero, and reject null data at positive length.
- C ABI 1.2 adds `nift_bytes` and `nift_script_result_value_bytes`. The accessor
  accepts only a successful top-level bytes result and returns an immutable
  borrowed view owned by that result. Empty bytes normalize to `{NULL, 0}`.
- A byte view remains valid until `nift_script_result_free`, including across
  later engine calls and engine destruction. Callers copy it to retain it past
  result destruction and must not cast away `const` to mutate it.
- `nift_script_result_value_json` now performs checked JSON conversion lazily.
  Top-level or nested bytes return `NIFT_ERROR_INVALID_ARGUMENT` from that
  accessor without changing the successful result. Successful JSON is cached
  in result-owned storage under a mutex, preserving concurrent result reads.
- A mismatched accessor, unsuccessful result or null result clears a supplied
  output view and returns `NIFT_ERROR_INVALID_ARGUMENT`. Unexpected contained
  C++ failures return `NIFT_ERROR_INTERNAL`.

## Focused evidence

`tests/cp21_bytes_embed.cpp` covers copied engine/context input, source mutation,
empty and 64 KiB values, embedded NUL and high octets, execute/evaluate result
views, repeated access, engine-destruction lifetime, nested-byte JSON rejection,
mismatched and failed-result access, output clearing, and concurrent first JSON
extraction. `tests/c_abi_smoke.c` proves the byte surface with a C compiler, and
`tests/v45_embed_staged_consumer.sh` proves it through staged installed headers,
the library and `pkg-config` metadata.

The durable `test-cp21-bytes` target is included in `make test`, `make
test-embed`, Gate 6B on Linux GCC/Clang, macOS Clang and Windows MinGW, and the
retained Gate 6A-R wall. The existing bytes sanitizer targets execute the CP21
battery under ASan/UBSan and TSan. The target also requires the canonical and
Go-copied C ABI headers to be byte-identical.

## Scope exclusions

No maintained Python, Node, Go or C# bytes API, recursive typed result
traversal, writable view, pinning, retained native pointer, Jsonic extension,
marker encoding, render-result bytes channel or proof-consumer integration is
added. Those remain governed by later checkpoints and Gate 7.
