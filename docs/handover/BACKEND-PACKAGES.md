# Backend package development

This is the canonical handover for process, FFI and future native backends in
Nift packages. Source and tests remain authoritative for current behaviour.
This document records the cross-package rules that should remain stable while
individual implementations evolve.

## Status

- Current checkpoint: CP09 and Review Gate 3 complete; hard stop before CP10.
- Last review gate: Review Gate 3 selected qualified A (the process architecture
  may proceed to persistent-worker design after explicit resumption, but CP09 is
  only a development/low-traffic prototype backend, not a production server).
- Packages adopting this contract first: `curl` and `sqlite`.
- Review Gate 1 approved later package prototypes with the documented stateful
  handle constraint; HTTP is now implemented through CP09.
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

Implementation commits: `nift-packages/curl` `578285a`, followed by the
selection-pinning correction `ca4469e` discovered before CP03.

Accepted:

- `curl` owns the durable request and verb API; direct verb exports remain
  deprecated v0.x compatibility aliases.
- `auto` resolves to the sole usable `process` backend and freezes on the first
  request.
- The selected backend is cached, so later executable/PATH changes do not
  silently retarget an already-used package instance.
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

CP03 and Review Gate 1 are complete. See their records below.

### CP03 - SQLite stateful backend facade

Implementation commits: `nift-packages/sqlite`
`99f3d6b901c739db70e0cad212ecf8d74e3976df`, followed by the handle-path
pinning correction `b57d7ebc21c5d0c5cea76700ea1ecc5295054dfb` found during Review Gate 1.

Accepted:

- `sqlite` owns backend selection and all database operations. `auto` resolves
  to the currently usable `process` backend, and the first `open()` freezes the
  package selection.
- `open()` returns a logical database handle. Package maps own its identity,
  backend, canonical path and open/closed state. Assignment and shallow copies
  refer to the same logical resource; altered and unregistered handles fail as
  `invalid_handle`.
- `close()`, `is_open()`, `same_handle()` and `handle_backend()` expose resource
  semantics without exposing a process or native handle.
- Operations return stable `ok`, `error`, `error_code` and `backend` fields;
  `exit_code` remains process diagnostic metadata.
- Transactions retain string-statement compatibility and additionally accept
  `{sql, params}` descriptors. Prepared statements, BLOBs, cancellation and
  persistent connections are explicitly unsupported.
- Temporary query output prefers `mktemp`, then Windows PowerShell, then a
  checked fallback path, and is removed on ordinary success and error paths.

Evidence:

- `python3 tests/dogfood.py /home/nick/Repositories/nift/nift/nift
  /home/nick/Repositories/nift/nift-packages/sqlite` passed on Linux. It uses a
  deterministic fake `sqlite3` because the host has no `sqlite3` executable.
- `NIFT=/home/nick/Repositories/nift/nift/nift
  SQLITE_PACKAGE=/home/nick/Repositories/nift/nift-packages/sqlite bash
  tests/package_sqlite_dogfood.sh` passed from the Nift repository.
- Coverage includes discovery, explicit/failed selection, selection locking,
  handle identity and copied aliases, close behavior, altered/invalid handles,
  binding, transaction descriptors, database failures, missing process backend
  and temporary-file cleanup.

Known limitations:

- An imported instance of a package-private Nift struct cannot dispatch its
  methods because its type is unavailable in the caller. Exporting the type
  makes dispatch possible but its methods then cannot resolve package-private
  state/helpers. Therefore stateful handles are ordinary namespaced objects and
  their behavior remains on the `sqlite` facade.
- Nift objects do not have private fields. `_handle_id` is visible language
  data, but it is only package logical identity, not a backend/native handle;
  registry and canonical-path checks reject unknown or altered handles. This is
  not intended as a security boundary.
- The process backend does not provide a persistent SQLite connection, and the
  non-atomic fallback temporary-name race documented by CP02 also applies.
- Real `sqlite3` execution and macOS/Windows behavior still require CI evidence.

### Review Gate 1 - curl and SQLite

Reviewed revisions and initial worktree states:

| Repository | Revision | State |
|---|---|---|
| `nift` | `80014191a58a6f115aa3ca25aa1354937b14f9c7` | clean |
| `nift-packages/curl` | `ca4469e05b6b80d0d10199c0293c4cb19c57c267` | clean |
| `nift-packages/sqlite` | `b57d7ebc21c5d0c5cea76700ea1ecc5295054dfb` | clean |

Validation:

- `make -j2` in `nift`: passed (`Nothing to be done for 'all'`).
- Curl offline dogfood: passed the basic matrix, output-file behavior, private
  exports, disabled/missing process backend, availability and no-`mktemp`
  fallback cases.
- SQLite deterministic dogfood and the pre-existing Nift package SQLite
  dogfood: passed.

API evidence:

```nift
@import("curl")
curl.use_backend("process")
response := curl.get("https://example.test/data")

@import("sqlite")
sqlite.use_backend("process")
db := sqlite.open("app.db")
rows := sqlite.query(db, "SELECT ? AS value", "A")
sqlite.close(db)
```

Findings:

1. The package-local facade, deterministic `auto` selection, explicit concrete
   selection, freeze-after-first-use rule and stable semantic errors work for
   both stateless requests and stateful logical resources without core changes.
2. Backend behavior belongs on the package facade. Treating methods on a
   package-private resource struct as a required convention is not viable with
   current import/type scope behavior.
3. The process implementations establish semantic APIs, but do not prove FFI
   parity, native lifetime handling, binary buffers, streaming or cancellation.
4. Local evidence is Linux-only. Curl and the new SQLite suite use deterministic
   fixtures. The existing SQLite smoke passed, but its real-database branch was
   skipped because this host has no `sqlite3` executable.

Recommendation B: adopt the package-local backend convention and permit later
package prototypes, with stateful resource operations kept on the package
facade and the struct/import limitation documented. Do not add core resource,
future, callback or cancellation machinery based on these process-only
prototypes. This review ends at the requested pre-HTTP stop boundary; no HTTP
package work has started.

Gate decision: accepted. The imported package-defined struct limitation remains
a possible general package/module encapsulation improvement, separate from HTTP
and from the deferred async FFI/future work. HTTP must not depend on resolving
it and no Nift core change is approved for it here.

### CP04 - HTTP repository and helper bootstrap

Implementation commit: `nift-packages/http`
`b06c3ecebe7ee20b106ea271a58aec2d07665f31`.

Repository: `https://github.com/nift-packages/http` (independent local `main`
worktree with `origin` configured; publication waits for the checkpoint batch).

Accepted:

- The package exports one `http` facade with `backends()`, `backend()`,
  `use_backend()`, `capabilities()`, `server()` and blocking `listen()`.
- `auto` resolves to the usable `process` backend. Creating a server freezes
  package selection and records the concrete backend in the facade-managed
  server handle.
- Stateful server behavior remains on the facade rather than relying on an
  imported package-defined struct.
- The process topology is client -> Python HTTP helper -> one fresh Nift
  application invocation per request -> helper -> client.
- The helper owns the listening socket, strict HTTP parsing, finite limits,
  worker lifecycle and response serialization. The application worker owns
  future route/handler behavior.
- Worker protocol framing is private JSON metadata in a per-request exchange
  directory. Worker stdout/stderr are not protocol channels, and body objects
  reserve `file`/`stream` evolution without promising permanent UTF-8 buffering.
- Defaults are loopback binding, one request per connection, an 8 KiB request
  line, 32 KiB headers, 100 headers, a 1 MiB body and a 30 second worker limit.
  Transfer encoding, duplicate content lengths, folded headers, malformed
  targets and invalid percent escapes are rejected before worker launch.
- TLS, streaming, multipart, WebSockets, worker pools, FFI and native modules
  remain deferred.

Evidence:

- `python3 tests/bootstrap.py /home/nick/Repositories/nift/nift/nift
  /home/nick/Repositories/nift/nift-packages/http` passed on Linux.
- The test installs the package into a fresh project, verifies concrete backend
  reporting and post-resource locking, sends a real loopback request, observes
  the intentional CP04 bootstrap 501 from a fresh Nift worker, waits for clean
  helper exit and verifies that private package helpers do not leak.

Known limitations and decisions affecting later checkpoints:

- The initial helper uses Python 3's standard library and a package-owned strict
  socket parser. Linux is the only tested platform; Python discovery and child
  process-tree handling require explicit macOS/Windows evidence.
- Nift does not expose a package-root intrinsic. The process backend locates its
  installed helper under the same `.nift/packages/http` layout used by package
  import. This is an implementation workaround, not public API.
- Route closures cannot be serialized into another Nift process. A worker
  therefore reruns the application script, rebuilds its route table, and has
  `http.listen(app)` switch to request-dispatch mode via private environment and
  file protocol state. Persistent workers remain deferred until measurements.

Next approved checkpoints: CP05 routing and CP06 minimum HTTP semantics,
followed by Review Gate 2. Do not begin CP07 before that gate.

### CP05 - HTTP methods and routing

Implementation commit: `nift-packages/http`
`a9c2a1c9a2a651ebdb33e418c482cb7cf25db9b9`.

Accepted:

- The actual stateful API is facade-oriented:
  `http.get(app, path, handler)`, with corresponding POST, PUT, PATCH, DELETE,
  HEAD and generic `route()` registration. This follows the Gate 1 package
  encapsulation finding rather than simulating methods on an imported struct.
- Server state and callable routes are package-owned maps keyed by a logical
  server ID. The application script rebuilds this table in each fresh worker.
- Static routes and `:name` segments are matched in Nift. Decoded parameters
  are exposed as `request.params`; HEAD falls back to GET while the helper
  suppresses response bytes and retains the GET `Content-Length`.
- Request objects expose method, target, decoded path/segments, query values,
  lowercase array-valued headers, buffered body metadata, parsed JSON when
  available and remote address.
- `http.text()` and `http.json()` produce backend-neutral body descriptors.
  The helper, not the application, owns HTTP wire framing.

Evidence:

- CP04 bootstrap regression passed after routing replaced the intentional 501
  with normal 404 behavior for an empty route table.
- `python3 tests/routing.py /home/nick/Repositories/nift/nift/nift
  /home/nick/Repositories/nift/nift-packages/http` passed on Linux.
- Coverage includes GET, POST, PUT, PATCH, DELETE, HEAD fallback, multiple
  routes, a decoded path parameter, query access, request-header access, JSON
  response serialization and 404.

Discovered constraints:

- A facade callable cannot mutate a plain object argument by reference, while
  Nift's built-in map has identity and supports package-owned mutation. The
  public server handle therefore remains a small object while private mutable
  state lives in package maps, matching SQLite's accepted logical-handle model.
- Route parameter object keys can be computed, but the key expression must be
  bound before bracket assignment. This affects implementation style only; the
  application still receives natural `request.params.id` access.
- Worker diagnostics remain outside protocol framing. On a nonzero worker exit
  the helper emits at most the first 8 KiB of captured diagnostics to its own
  stderr and returns a clean 500 to the client.

Next approved checkpoint: CP06 minimum HTTP semantics, followed by Review Gate
2. Do not begin CP07 before that gate.

### CP06 - minimum HTTP semantics

Implementation commit: `nift-packages/http`
`d000621670e2a5715760e9fdc498f7a623ba509a`, followed by Gate 2 correction and
measurement commit `1df68b663e5f378f84c797429f5b5bc4c8bf2216`.

Accepted:

- GET and POST application paths, buffered text bodies, helper-parsed JSON
  requests, JSON/text responses, custom status and response headers, 404, 405
  with `Allow`, multiple routes and repeated sequential requests work through
  the real helper/worker topology.
- `http.server_backend(app)` reports the concrete backend pinned when the
  server was created. Post-resource package selection remains locked.
- Parser and worker limits are finite and can be lowered per server:
  request-line bytes, total header bytes/count, body bytes, client deadline,
  worker deadline and listen backlog.
- HTTP/1.1 requires exactly one non-empty Host header. Transfer encoding,
  duplicate Content-Length, folded/invalid headers, invalid target encoding,
  malformed JSON and oversized bodies fail before application dispatch.
- Application `print()` output is isolated from protocol framing. Worker
  runtime failure returns a bounded 500; worker timeout terminates the POSIX
  process group and returns 504.
- The helper monitors its Nift parent, closes and removes its private temporary
  root when that parent disappears, and removes each request directory on
  ordinary success and failure paths.
- Helper process-launch, startup/bind and disabled execution failures remain
  package-level structured errors (`helper_launch`, `helper_failed` and
  `backend_unavailable`) rather than exposing helper-specific control flow to
  applications.

Evidence:

- The CP04 bootstrap, CP05 routing and CP06 dogfood suites all passed
  sequentially on Linux.
- `tests/dogfood.py` covers GET, POST echo, JSON request/response, multiple and
  parameterized routes, query/header access, custom 201/header, 404, 405,
  repeated requests, malformed request syntax, duplicate framing, malformed
  JSON, non-finite JSON, request-line/header/body limits, invalid path/query
  escapes, Host and transfer/framing rules, binary rejection, explicit HEAD and
  HEAD error framing, 204 framing, invalid response metadata, disconnected
  clients, worker error/timeout/descendant cleanup, clean finite shutdown,
  forced parent shutdown, temporary-root cleanup, helper bind failure and
  disabled process execution.
- Private helper leakage and post-server backend locking remain covered by the
  CP04 regression.

Known limitations:

- Request and response bodies implemented in CP06 are buffered UTF-8 text/JSON.
  The protocol reserves file and stream body descriptors, but route-level
  binary/file APIs and streaming are not implemented or advertised.
- The server is sequential and starts one complete Nift process per dispatched
  request. This is intentionally unoptimized until Review Gate 2 measurements.
- HTTP parsing is a deliberately strict HTTP/1.1 subset: one request per
  connection, `Connection: close`, Content-Length only. Chunking, keep-alive,
  TLS, multipart and WebSockets are deferred.
- POSIX process groups are implemented and tested on Linux. Windows Job Object
  ownership is not implemented, and neither macOS nor Windows has execution
  evidence.

### Review Gate 2 - minimum HTTP backend

Reviewed HTTP revision:
`1df68b663e5f378f84c797429f5b5bc4c8bf2216`. Nift core was not modified for
HTTP, and no `net`, `process`, `websocket` or `openssl` repository was created.

Actual application source:

```nift
@import("http")

app := http.server({"host":"127.0.0.1","port":8080})

http.get(app, "/", (request) => http.text("Hello from Nift"))
http.post(app, "/echo", (request) => http.text(request.body.text))
http.post(app, "/json", (request) => http.json({
    "received": request.json.value
}))
http.get(app, "/users/:id", (request) => http.json({
    "id": request.params.id,
    "query": request.query.q,
    "header": request.headers.get("x-test")[0]
}, {"status":201,"headers":{"x-created":"yes"}}))

result := http.listen(app)
```

API ergonomics:

- The facade form `http.get(app, ...)` is less fluent than `app.get(...)`, but
  it is direct and consistent with the Gate 1 stateful-package finding. It does
  not leak helper, IPC, process ID or temporary-file details.
- `listen()` is blocking because Nift has no package-visible persistent process
  handle. This is acceptable for the initial server entry point.
- Each worker reruns the application script to reconstruct route callables.
  Route registration must therefore be deterministic and any other top-level
  side effect must be guarded or moved elsewhere. This is the most important
  current application/helper separation cost.
- The helper parses JSON because Nift has no runtime JSON-string parser. The
  application receives ordinary Nift values and produces backend-neutral body
  descriptors, so this does not become public process-backend API.

Final CP06 topology:

```text
Nift server application (blocking http.listen)
  -> Python HTTP/1.1 helper (socket, limits, lifecycle)
      -> one fresh Nift application process per dispatched request
          -> private request/response JSON envelope
      -> serialized HTTP response
  -> client
```

The helper handles clients sequentially. Each connection is closed after one
request. Worker stdout/stderr are isolated from protocol framing. Request IDs
pair envelopes, and private request directories are removed after every
ordinary success/failure path.

Linux measurements on 2026-09-28:

| Measurement | Result |
|---|---:|
| User-visible listen startup, 10 runs | 61.213 ms median (57.079-80.112 ms) |
| Standalone one-shot worker, 20 runs | 8.973 ms median, 9.709 ms p95 |
| Cold launch through first response | 81.479 ms |
| Repeated sequential requests, 30 runs | 14.230 ms median, 17.702 ms p95 |
| Idle Nift parent + helper RSS | 27,164 KiB |
| Active parent + helper + worker RSS | 35,052 KiB |

These are local architectural measurements, not a competitive benchmark or
service-level guarantee. Gate interpretation used deliberately modest criteria:
no lifecycle/correctness blocker after recertification, median startup below
100 ms, repeated p95 below 20 ms and active topology RSS below 64 MiB. The
observed one-worker model is costly compared with an in-process server but
sufficiently practical for the next package semantics checkpoints.

Correctness and lifecycle:

- The corrected strict parser rejects malformed request lines/headers, invalid
  percent encoding, missing/duplicate Host, transfer encoding, duplicate or
  malformed Content-Length, non-standard/malformed JSON and configured limit
  violations without terminating the listener.
- Methods remain case-sensitive. Explicit HEAD routes take precedence over GET
  fallback, implicit HEAD appears in `Allow`, and HEAD errors suppress bodies.
  204/304 and invalid application response metadata are bounded correctly.
- Worker runtime failure returns 500, timeout returns 504, and POSIX worker
  groups are terminated after success, failure or timeout so descendants do not
  escape. Broken clients do not terminate the helper.
- Normal finite shutdown, Nift-parent death, bind failure and worker failures
  leave no observed helper, worker or `nift-http-*` temporary root on Linux.
- One slow client or handler can occupy the sequential helper until its finite
  elapsed deadline. Concurrency and persistent workers remain later work.

Portability evidence:

- Linux: implemented and tested locally, including POSIX process groups,
  lifecycle tests and `/proc` RSS/process checks.
- macOS: not tested. No support claim beyond source-level intent.
- Windows: not tested. Job Object descendant ownership is not implemented, so
  Windows lifecycle support is specifically unproven.

Decision: **A - CONTINUE PROCESS BACKEND**. The gate initially found parser,
framing, pinning and descendant-cleanup defects; all blocking findings were
corrected and regression-tested before this decision. The measured helper and
one-worker-per-request model is sufficiently useful to proceed to CP07-CP09
when explicitly resumed. Do not infer that FFI/native comparison, persistent
workers, streaming, TLS or hostile-input certification is complete.

Hard stop: Review Gate 2 is complete. Do not begin CP07 automatically.

Gate 2 was accepted and work explicitly resumed through CP09, followed by a
hard stop at Review Gate 3. Persistent workers remain deferred to CP10.

### CP07 - forms and cookies

Implementation commit: `nift-packages/http`
`a4fd025b2e89e2d15d48bee90a942865d6df9b02`.

Accepted:

- `application/x-www-form-urlencoded` populates `request.form` only for that
  media type. Single names are strings, repeated names become ordered arrays,
  blank values survive, `+` means space and UTF-8 percent decoding is strict.
- UTF-8 is the only accepted form charset. Duplicate Content-Type, malformed
  escapes/encoding, excessive field count and oversized names/values fail
  before worker launch under finite configurable limits.
- Request Cookie fields populate `request.cookies` with the same
  single-string/repeated-array convention. Parsing is conservative, ASCII-only,
  does not percent-decode values and rejects malformed pairs/control bytes.
- `http.cookie()` creates structured low-level response descriptors. The helper
  serializes Path, Domain, Max-Age, Expires, Secure, HttpOnly and SameSite and
  enforces `__Secure-`/`__Host-` rules.
- Structured cookies produce one `Set-Cookie` header each and cannot be mixed
  with a raw `set-cookie` response header. Sessions, authentication, CSRF and
  cookie business meaning remain outside the package.

Evidence:

- `python3 tests/forms_cookies.py /home/nick/Repositories/nift/nift/nift
  /home/nick/Repositories/nift/nift-packages/http` passed on Linux.
- Coverage includes repeated/blank/UTF-8 fields, content-type parameters,
  non-form bodies, malformed percent/UTF-8, form count/name/value limits,
  repeated and malformed cookies, repeated Set-Cookie attributes, invalid
  prefixed cookies and duplicate Content-Type.
- The expanded CP06 dogfood regression passed after CP07.

Architectural question retained for Review Gate 3: Python 3 remains the helper
runtime. Do not replace it during CP08-CP09; assess whether it is acceptable
long-term or should eventually become a compiled compatibility helper using
implementation complexity, portability, startup/RSS, deployment and security
maintenance evidence.

Next approved checkpoints: CP08 body storage/multipart and CP09 files/ranges/
CRUD dogfood, followed by Review Gate 3. Do not begin CP10 before that gate.

### CP08 - body storage and multipart uploads

Implementation commit: `nift-packages/http`
`6f310f070582bb2c7f1a3fc5804d92811c72edff`.

Accepted:

- Small UTF-8 and JSON bodies retain buffered convenience. Arbitrary non-UTF-8
  bodies become opaque `spooled` descriptors and can be copied during the
  request with `http.save_body()`.
- `multipart/form-data` populates `request.form`, `request.uploads` and
  `request.files`. Single file fields are upload objects and repeated names are
  ordered arrays.
- Public upload descriptors expose logical identity plus field name, client
  filename, content type and size. Helper-generated spool paths are removed
  from the request before handler dispatch.
- `http.save_upload(upload, destination)` copies bytes to an explicit
  application-selected destination while the request worker is alive. Supplied
  filenames are metadata only and never select spool/save paths.
- The bounded parser rejects invalid/missing/unfinished boundaries, folded or
  duplicate part headers, transfer encoding, nested multipart, missing/invalid
  disposition/name and invalid text-field UTF-8.
- Finite limits cover aggregate body, total temporary bytes, parts, files,
  per-part header bytes/count, individual file bytes, filename bytes and text
  field bytes. Request cleanup owns every original spool on all ordinary paths.

Evidence:

- `python3 tests/multipart.py /home/nick/Repositories/nift/nift/nift
  /home/nick/Repositories/nift/nift-packages/http` passed on Linux.
- Coverage includes binary NUL/non-UTF-8 data, ordinary and repeated fields,
  one/multiple/repeated files, empty and repeated filenames, `../`, absolute and
  encoded-separator filename metadata, explicit byte-preserving saves, generic
  binary-body save, forged handle, boundary-like payload bytes, malformed and
  missing closing boundaries, part/file/header/body limits, partial disconnect,
  handler failure, timeout and final temporary-root cleanup.
- CP07 forms/cookies and the adjusted CP06 dogfood regressions passed after the
  body model changed from rejecting binary to opaque spooling.

Known limitation retained for Gate 3: the helper currently receives the bounded
aggregate request body in memory before writing raw/per-file spools. The public
contract does not expose this and remains compatible with incremental receive,
but upload peak-memory behavior must be measured and reported rather than
described as streaming.

Next approved checkpoint: CP09 files, ranges, limits and CRUD dogfood, followed
by Review Gate 3. Do not begin CP10 before that gate.

### CP09 - files, ranges, limits and CRUD dogfood

Implementation commit: `nift-packages/http`
`117822f810bbdd79759bb735896c6bc8de4717f6`.

Accepted:

- `http.file(path)` describes an application-authorized file response.
  `http.file_from(root, relative)` is the facility for untrusted relative route
  input: it rejects absolute, empty, dot and backslash components and uses
  descriptor-relative no-symlink component opens on POSIX.
- The helper validates one regular-file descriptor, derives framing from that
  descriptor and transfers bytes with `socket.sendfile()` or bounded chunks.
  File bytes never enter a Nift string or the JSON worker envelope.
- GET and HEAD support closed, open-ended and suffix single byte ranges with
  generated `Accept-Ranges`, `Content-Range` and `Content-Length`. Invalid,
  unsatisfiable and multiple ranges return 416. Empty files, missing files,
  directories and application disconnects have bounded behavior.
- `max_file_response_bytes` bounds served files and `response_timeout_ms`
  bounds client writes. Open descriptors are closed on success, HEAD, range
  rejection, helper errors and disconnects.
- The CRUD dogfood exercises JSON create/update, URL-encoded create, list/get,
  structured cookies, delete, error responses, multipart attachment save,
  full/ranged attachment download and deterministic persisted restart state.
  Two process-local counter requests both return one, directly demonstrating
  that each request reconstructs application and route state in a fresh worker.

Evidence:

- `python3 tests/files_ranges.py /home/nick/Repositories/nift/nift/nift
  /home/nick/Repositories/nift/nift-packages/http` passed on Linux.
- `python3 tests/crud.py /home/nick/Repositories/nift/nift/nift
  /home/nick/Repositories/nift/nift-packages/http` passed on Linux.
- The complete CP04-CP09 HTTP regression sequence passed after the change:
  `bootstrap.py`, `routing.py`, `dogfood.py`, `forms_cookies.py`,
  `multipart.py`, `files_ranges.py` and `crud.py`.
- Rooted-file coverage includes raw/encoded traversal, repeated encoding,
  absolute paths, alternate separators, symlinks, directories and nonexistent
  files. Range coverage includes GET/HEAD parity, clamping and 416 framing.

Known limitations:

- The host still has no `sqlite3` executable. CRUD persistence therefore uses
  an ordinary deterministic Nift value file. This proves HTTP application and
  restart semantics, not real HTTP+SQLite integration or concurrent database
  behavior.
- File persistence is safe only under the current serialized request model; it
  is not a substitute for transactional storage once concurrency is added.
- Multipart receive remains aggregate-buffered before per-file spooling, and
  multipart byte ranges are not supported.

### Review Gate 3 - ordinary website backend viability

Gate command:

```text
python3 tests/gate3.py /home/nick/Repositories/nift/nift/nift \
  /home/nick/Repositories/nift/nift-packages/http
```

The retained run used Linux 7.0.0-29-generic x86_64 and Python 3.14.4. The
concurrency workload used one warmed listener and a representative 40 ms
application command per request. It deliberately measured the existing
sequential helper rather than adding pooling or concurrency.

| Simultaneous clients | Batch | Median | p95 | Throughput | Errors | Direct workers | Peak topology RSS |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 66.860 ms | 66.603 ms | 66.603 ms | 14.957 req/s | 0 | 1 | 48,016 KiB |
| 8 | 527.087 ms | 296.023 ms | 525.137 ms | 15.178 req/s | 0 | 1 | 47,964 KiB |
| 32 | 2,109.766 ms | 1,084.490 ms | 2,041.160 ms | 15.168 req/s | 0 | 1 | 51,040 KiB |

Idle parent/helper RSS was 8,472/21,180 KiB. Peak topology process count was
five: Nift server parent, Python helper, one Nift worker and the worker's shell
plus sleep process. There was never more than one direct worker. Throughput
therefore remains flat while queued-client latency grows approximately
linearly. The result is architecture evidence, not a service benchmark.

Representative resource probes:

| Operation | Elapsed | Helper peak RSS | Peak topology RSS | Peak helper temp |
|---|---:|---:|---:|---:|
| 1 MiB multipart upload | 224.305 ms | 24,512 KiB | 48,120 KiB | 2,098,129 bytes |
| 4 MiB slow-read file download | 309.843 ms | 21,696 KiB | 38,552 KiB | 431 bytes |

The resource server's idle parent/helper RSS was 8,476/21,036 KiB. Upload
temporary usage is approximately the aggregate wire body plus the per-file
spool, confirming the documented buffering model. The 4 MiB download increased
helper RSS by only 660 KiB and did not create a payload-sized spool, confirming
helper-side file transfer. Both operations preserved exact binary bytes, had no
request error and left no `nift-http-*` root after finite shutdown.

Viability finding:

- The facade can express a recognizable ordinary site backend: routing,
  parameters/query/headers, JSON and form input, cookies, persisted CRUD,
  bounded multipart uploads, binary downloads, ranges and ordinary 4xx/5xx
  responses. No helper path, command, PID or wire envelope enters the public
  API. The process boundary is not currently an application-expressiveness
  blocker.
- The current runtime is suitable for development, integration dogfood and
  deliberately low-traffic deployment behind an appropriate frontend. It is
  not a general production website server: one slow client or handler queues
  every other client, every request pays application reconstruction cost,
  connections always close, TLS is absent and only Linux has lifecycle evidence.
- Persistent workers may remove reconstruction/startup cost but do not by
  themselves solve serialized client I/O. Any later concurrency checkpoint
  must also define persistence synchronization, backpressure, cancellation and
  bounded in-flight work.

Python dependency classification: **acceptable prototype/compatibility backend
but probably replace later**. Python 3's standard library enabled a strict,
dependency-free prototype quickly and is common on development systems, but it
breaks Nift's otherwise standalone-binary deployment expectation, contributes
about 21 MiB idle helper RSS, has only Linux evidence and leaves 959 lines of
security-sensitive HTTP/process code maintained in a second runtime. This is
not yet a deployment blocker for the stated prototype scope, but it is not the
preferred long-term production boundary.

Helper component classification:

| Component | Classification | Reason |
|---|---|---|
| Route matching, handler invocation, application persistence and response descriptor construction | Can live in Nift today | These already use ordinary Nift values and package facade code. |
| Persistent worker/control channel, child ownership and cancellation | Requires better process API | Current `run()` is blocking and exposes no durable child/stdin/stdout lifecycle handle to package code. |
| Listener, accept, socket receive/send and zero-copy file transfer | Requires socket/net package | Nift packages do not currently own listening sockets or `sendfile`-equivalent operations. |
| Concurrent clients, deadlines, backpressure and bounded in-flight scheduling | Requires async runtime improvement | Moving blocking socket calls alone would reproduce the same serialized behavior. |
| Strict HTTP framing, multipart parsing, cookie serialization, rooted descriptor opens and wire validation | Better left helper/native-side | These are security-sensitive protocol/OS boundary operations and do not improve by being rewritten in facade code. |
| Temporary exchange/spool ownership and parent/worker teardown | Helper/native-side today; better process API later | The helper can currently guarantee cleanup and POSIX process-group teardown more reliably than package code. |

Decision: **qualified A - CONTINUE THE PROCESS-BACKEND PROGRAM TO CP10 ONLY
AFTER EXPLICIT RESUMPTION**. CP09 validates the public API and a realistic
workflow, so replacing the facade or abandoning the process experiment is not
justified. CP10 should be treated as architecture work toward a useful
compatibility backend, not as evidence that the present server is production
ready. A compiled/native socket-side implementation remains the likely
long-term direction.

Hard stop: Review Gate 3 is complete. Do not begin CP10 automatically.
