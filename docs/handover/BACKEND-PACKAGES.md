# Backend package development

This is the canonical handover for process, FFI and future native backends in
Nift packages. Source and tests remain authoritative for current behaviour.
This document records the cross-package rules that should remain stable while
individual implementations evolve.

## Status

- Current checkpoint: CP02, curl backend facade stabilized.
- Last review gate: none; Review Gate 1 follows CP03.
- Packages adopting this contract first: `curl` and `sqlite`.
- New backend packages must wait until Review Gate 1 approves the convention.
- Nift core, the package resolver and the package manifest do not select
  implementation backends.

## Backend terminology

Packages use these names consistently:

| Name | Meaning |
|---|---|
| `auto` | Deterministically select an available backend supported by the package. |
| `process` | Invoke an external executable or helper process. |
| `ffi` | Load a C ABI library through Nift's dynamic FFI. |
| `native` | Use a future compiled Nift native-module mechanism distinct from ordinary FFI. |

`auto` is a selection mode, not a concrete backend. `backend()` therefore
reports the selected concrete backend, never `auto`.

Do not use `cli` as the generic name: a process backend may use a helper that is
not a user-facing command-line tool. `external` is ambiguous, while `embedded`
incorrectly suggests that a library is part of Nift itself.

## Package facade

A package that can have multiple implementations should export one
package-named facade and provide:

```nift
package.backends()             // usable concrete backends, for example ["process"]
package.backend()              // selected concrete backend
package.use_backend("process") // select before the package is first used
```

Packages may additionally expose `available()`, `capabilities()`, `version()`
or package-specific configuration. They must document their exact capability
shape rather than implying that all backends support the same optional
features.

Selection rules:

1. Selection is package-local. The package manager does not understand backend
   variants.
2. Selection is lazy unless a package documents a reason to resolve it at
   import time.
3. `auto` uses a documented deterministic order and selects only an available
   backend.
4. The first operation or resource creation freezes the selected backend for
   that imported package instance.
5. `use_backend()` after selection fails without changing the selected backend.
6. Requesting an unknown or unavailable backend fails without falling back.
7. A package never retries an uncertain side effect through another backend.
8. A long-lived resource records its concrete backend when created and remains
   pinned to it until closed.

An environment override such as `NIFT_SQLITE_BACKEND` may be added later for CI
or deployment, but it is not required by this contract and must not supersede
an explicit `use_backend()` call.

## Public API boundary

- Application APIs describe package-domain operations, not implementation
  commands, executable names, temporary paths, FFI symbols or native pointers.
- Process temporary files are private implementation details. They use unique
  paths, fail closed when creation fails and are removed on ordinary success
  and error paths.
- FFI and native handles do not appear in ordinary application values.
- Resource identity, closed state and backend ownership are package semantics;
  their process or native representation is not.
- Backend-specific functionality is reported through capabilities and fails
  explicitly when unavailable. A package must not silently approximate a
  stronger contract.
- `timeout` bounds elapsed work. It is not a cancellation token and does not
  imply that every underlying operation can be interrupted immediately.

## Results and errors

Nift does not yet have a universal recoverable `Result` type. Packages should
not invent one in core for this work, and domain result objects do not need to
be identical. Failed package operations should nevertheless provide these
stable fields where they return a result object:

```text
ok            boolean package-level success
error         human-readable message, empty on success
error_code    stable package-defined semantic code, empty on success
backend       concrete backend that performed or attempted the operation
```

Backend-specific diagnostics may be additive, for example `exit_code` for a
process backend or `backend_code` for a library. Application control flow
should use the stable fields rather than backend codes.

Domain semantics remain distinct. An HTTP 404 is a successfully received HTTP
response, not a transport failure. A SQLite constraint failure is a failed
database operation even when its helper process launched successfully.

Invalid package usage may remain a Nift runtime error when returning a result
would hide a programming mistake. Each package must document which failures
are returned and which are hard errors.

## Binary data and streaming

- Text fields are documented as text. Binary safety must not depend on bytes
  being valid UTF-8.
- Until a package has a stable byte value, files are the preferred
  backend-neutral path for large or arbitrary binary data.
- A buffered operation remains buffered when other backends are added.
- A future streaming API must deliver data incrementally, apply bounded
  buffering/backpressure and define cleanup. Writing a complete body to a
  temporary file and reading it back is not streaming.
- File-backed compatibility does not prevent a later native backend from using
  zero-copy or incremental I/O internally.

## Differential testing

Every backend implements the same semantic conformance cases where its declared
capabilities overlap. Tests select concrete backends explicitly and compare
application-visible results, not implementation metadata. Backend-specific
tests additionally cover discovery, launch/load errors, cleanup and native
lifetime rules.

## Deferred questions

- Whether environment backend overrides are useful in addition to the package
  API.
- Whether Nift needs a non-throwing FFI library probe before an `ffi` backend
  can participate safely in `auto` selection.
- Native artifact selection and a versioned native-module ABI.
- A common byte-buffer value, external future completion, retained callbacks,
  cooperative `await`, cancellation and deterministic native-resource scopes.

These remain deferred until package prototypes provide concrete evidence. No
HTTP, SQLite, curl or TLS concept should be added to Nift core to solve them.

## Checkpoint record

### CP01 - backend package contract

Accepted:

- Package-local `auto` / `process` / `ffi` / `native` terminology.
- One package facade with inspectable, frozen backend selection.
- Resource backend pinning at creation.
- Stable semantic error fields without a universal core result type.
- Explicit binary, buffering and capability contracts.

Known limitations:

- Current FFI loading is not a non-throwing availability probe.
- Current packages are process-only and do not yet provide differential backend
  evidence.
- Review Gate 1 must validate this design against both stateless curl requests
  and stateful SQLite logical handles before a new HTTP package is created.

Next approved checkpoint: CP03 (`sqlite`), followed by Review Gate 1. HTTP work
is not approved before that gate.

### CP02 - curl backend facade

Implementation commit: `nift-packages/curl` `578285a`.

Accepted:

- `curl` owns the durable request and verb API; direct verb exports remain
  deprecated v0.x compatibility aliases.
- `auto` resolves to the sole usable `process` backend and freezes on the first
  request.
- HTTP responses, including 4xx/5xx, remain successful transfers. Backend,
  timeout, transport and file failures use stable package error codes.
- Header values are arrays so repeated fields remain distinct, and redirect
  header blocks do not leak into the final response.
- `output` is the binary/file path and no longer reloads the completed file into
  `body`; buffered bodies remain text-oriented.

Evidence:

- The offline dogfood covers all verb helpers, JSON PUT/PATCH preservation,
  redirects, repeated headers, output files, backend locking, missing/disabled
  process execution and the fallback path without `mktemp`.
- `python3 tests/dogfood.py <nift> <curl-package>` passed on Linux during CP02.

Known limitations:

- Nift has no atomic package-visible temporary-file primitive. The process
  backend prefers `mktemp` or Windows PowerShell and otherwise uses a checked
  counter fallback with a documented cross-process race.
- Streaming, multipart, connection reuse and libcurl remain deferred.
- macOS and Windows execution still require CI evidence; CP02's local evidence
  is Linux-only.

Next approved checkpoint: CP03 (`sqlite`), followed by Review Gate 1.
