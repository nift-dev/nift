# Nift v4.7.2

Nift v4.7.2 is a maintenance patch fixing a prepared-execution regression present
in v4.7.1.

## Changes

- Fixes argument-taking native method calls executed through prepared loop/body
  execution. Calls such as `string.encode("utf-8")`, `bytes.decode("utf-8")` and
  `bytes.slice(...)` now receive the same arguments and behave the same inside
  prepared execution as they do in ordinary expression evaluation.
- The fix applies to the generic prepared member-call path rather than
  special-casing encode/decode, preserving prepared execution and its
  performance benefits.
- Adds implementation and external-contract coverage for prepared method-call
  argument parity, including valid/invalid UTF-8 handling and multibyte data.

## Compatibility

No language semantics, runtime API, package API, or C ABI changes.
C ABI remains 1.3.