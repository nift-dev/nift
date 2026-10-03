# v4.6 HTTP1 — production-grade curl client evidence

Status: complete. Implementation lives in the `curl` package repository
(`nift-packages/curl`); this document is the Nift-side handover/evidence only.
No Nift core code was changed.

## Scope

HTTP0 (the audit, `docs/handover/V4.6-HTTP-PACKAGE-AUDIT.md`) established that
`nift-packages/http` is the **server** and `nift-packages/curl` is the
**client**. HTTP1 evolves only the client package. The server (Python helper)
is a separate track and was not touched.

Package commit:

```text
b6ed36f  feat: make curl a production-grade HTTP client   (nift-packages/curl)
```

## Exported API (frozen)

Facade methods:

```text
curl.request(url, opts?) / curl.get / curl.post / curl.put / curl.patch /
curl.delete / curl.head
curl.download(url, path, opts?)      // GET to file
curl.upload(url, path, opts?)        // PUT file
curl.session(opts?) / curl.session_close(s)
curl.available() / curl.backends() / curl.backend() / curl.use_backend(name)
curl.capabilities() / curl.version() / curl.features()
```

Deprecated v0.x compatibility aliases retained and still exported: `request`,
`get`, `post`, `put`, `patch`, `delete`, `head` (delegating through the facade).
No compatibility alias was removed. `headers_as_args` (which mutated a
caller-supplied array) was replaced by `headers_args` returning a new array —
this also fixed a latent bug where custom headers were never actually sent
(Nift arrays/maps are pass-by-value).

## Response contract

```text
ok, status, headers, body, body_bytes, output, url, effective_url,
method, error, error_code, backend, exit_code
```

- HTTP 4xx/5xx: `ok:true`, `status` populated; not a package error.
- DNS/connect/TLS/timeout/redirect/file: `ok:false`, `status:0`, stable
  `error_code` (`timeout` | `transport_failure` | `file_error` |
  `backend_unavailable` | `temporary_file` | `invalid_header` |
  `invalid_cookie` | `invalid_method` | `invalid_session`).
- HTTP status is never converted into a Nift language exception.

## Feature matrix (HTTP1)

| Capability | Status |
| --- | --- |
| GET/HEAD/POST/PUT/PATCH/DELETE/custom | yes |
| request/response headers (arrays) | yes |
| query encoding | yes |
| application/x-www-form-urlencoded | yes |
| multipart/form-data + file parts | yes |
| raw body / body file / body bytes | yes |
| JSON body | yes |
| upload (`-T`) / download (`-o`) | yes |
| connect + total timeout | yes |
| redirects + max redirects | yes |
| Basic / Bearer / raw Authorization | yes |
| request cookies + persistent cookie jar | yes |
| proxy | yes |
| CA bundle/path, client cert/key, insecure (opt-in) | yes |
| compression (`--compressed`) | yes |
| HTTP version selection (1.1/2/2-prior/3) | yes |
| binary response bytes (`body_bytes`) | yes |
| truthful capability reporting | yes (from `curl --version`) |
| streaming / SSE / WebSockets | no (documented blocker) |
| async composition | no (core blocker, below) |
| libcurl via FFI | no (documented blocker) |

## Binary behaviour

Request bytes use a byte-safe temp write and `--data-binary @file`; response
bytes use `open_bytes` into a Nift `bytes` value (`response.body_bytes`).
Neither path uses a shell or text formatting. `output` lets curl write the
body directly to a caller file. Text responses read `response.body`.

## Session / cookie behaviour

`curl.session(opts)` carries package-managed defaults (headers, timeout, auth,
redirect policy, cookies) merged per request, with per-request overrides
(`headers`/`cookies` deep-merged; per-request `cookie_jar` overrides the
session jar). `persist_cookies: true` creates a session-owned temp cookie jar
so `Set-Cookie` persists across requests; `session_close` removes an owned jar
and rejects reuse. This is documented honestly as request state, **not**
in-process connection-pool/keepalive reuse (each request is a separate curl
process).

## Async result

Blocked by a **Nift core limitation**, not solvable at package level. A
struct facade (like `curl`) is non-transferable and cannot be referenced from
a `thread()`/`async` worker closure:

```text
thread(() => curl.get(url))          -> unknown value or malformed expression: curl
@fn[async](f()) { return curl.x }    -> unknown value or malformed expression: curl
async_fn(curl, ...)                  -> async function argument contains a non-transferable resource
```

Package-level alternatives tried and rejected: inline worker closure,
top-level lambda `fetch := (url) => curl.get(url)` invoked on a worker, and
passing the facade as an argument. A local `struct` instance shows the same
behaviour, while a plain map is capturable — so this is a general
worker-closure/transferability limitation. The synchronous client works on the
main thread. A general core change (worker-capturable package facades) would be
required; it is not justified solely for HTTP and is left as a later blocker.

## Streaming result

`curl` can write a response directly to a file (`output`) and decode
`--compressed`, but incremental Nift chunk callbacks require a process-stream
read primitive that `run()` does not expose. SSE/WebSockets are deferred and
`capabilities().streaming`/`websocket` are `false`. Not advertised as
supported.

## Cross-platform

The package uses `mktemp` on POSIX and the PowerShell temp fallback on Windows,
with a checked package-local fallback (now robust when `TMPDIR`/`TEMP`/`TMP`
are all unset). No shell is invoked; argv is structured. The test fixture is
Python (test-only); the production package has no Python dependency. CI
coverage for the client remains the core `test-v44-packages` combined dogfood
(which imports `curl`); the package-local suites are `tests/dogfood.py` and
the new `tests/http1.py`.

## Tests / pass count

- `tests/http1.py` — self-contained offline local fixture: **34 PASS**.
  Covers verbs/status/headers/effective_url, custom headers + user agent,
  query encoding, forms, multipart, Basic/Bearer auth, request cookies,
  session cookie persistence + close, redirects + max_redirects + timeout,
  binary response/request bytes, download (byte-exact) + upload, compression,
  capabilities, `--no-process`, HEAD with non-zero Content-Length, repeated
  Set-Cookie, CRLF header injection rejection, early-failure method, temp
  fallback with TMPDIR unset, connection refused, malformed URL, missing body
  file, invalid session, empty binary body.
- `tests/dogfood.py` — original contract preserved: **10 PASS** (including the
  fake-curl temp-sequence and shared-state/lexical-isolation checks).

## Independent review findings (repaired)

A separate adversarial review found and these were fixed before commit:
temp_root null-binding crash; `head()` used `-X HEAD` (hangs on a legal
non-zero Content-Length HEAD) → now `--head`; repeated headers beyond the
second dropped → now preserved; CRLF header/cookie/method injection → now
rejected; per-request cookie jar ignored when a session jar existed → now
overrides; `response.url` was the pre-query URL → now the merged URL; early
failures hardcoded `method: "GET"` → now the intended method;
`version/features/capabilities` aborted under `--no-process` → now safe.

## Core blockers discovered

1. Worker closures cannot capture struct facades (async composition).
2. No process-stream read primitive (true streaming/SSE).
3. FFI cannot express libcurl (`curl_easy_setopt` variadic; only `i64(i64)`
   callbacks; no closure/userdata; no retained callbacks; no `curl_slist`).

None were worked around by modifying Nift core; all are documented and left as
later blockers.

## HTTP1a — cross-platform certification

The client is now certified by a package-owned GitHub Actions workflow
(`.github/workflows/http1.yml`) that builds `nift-dev/nift` main and runs all
three suites on Linux, macOS and Windows. Curl repository commits
(`nift-packages/curl`, pushed `origin/main` = `71d7eca`):

```text
b6ed36f  feat: make curl a production-grade HTTP client
75edfca  test: add HTTP1 option-construction, session-lifecycle, and cleanup coverage
d47d592  ci: certify curl HTTP client across supported platforms
1f79f27  test: keep package test work dirs outside the package root
d3f8258  fix: verify temp-file helper paths are reachable before use
6cfbbe7  ci: use native Windows curl and a Windows temp dir
e7149de  test: decode nift subprocess output as UTF-8
71d7eca  fix: use the PowerShell temp helper on Windows; quiet fixture aborts
```

Certification run `37136678653` — all jobs green:

| Platform | curl | dogfood | http1 | options |
| --- | --- | --- | --- | --- |
| ubuntu-latest | 8.5.0 (x86_64-pc-linux-gnu) | PASS | PASS | PASS |
| macos-latest | 8.7.1 (x86_64-apple-darwin) | PASS | PASS | PASS |
| windows-latest | 8.16.0 (Windows) | PASS | PASS | PASS |

Per-suite checks: `dogfood.py` 10, `http1.py` 37 + summary, `options.py` 22 +
summary (71 `PASS` lines total per platform). The macOS/ubuntu runners also
carry the shared POSIX suites unchanged.

### Platform fixes required to reach green

Cross-platform execution exposed four genuine issues (all package/test, no
Nift core change):

1. `nift add <local path>` aborted on Windows (`0xC0000409`): a local package is
   staged by `fs::copy(recursive)` when directory symlinks are unavailable, and
   the test work dir was nested inside the package, so the copy recursed into
   itself. Fixed by moving the test work dirs to a system temp dir
   (`1f79f27`).
2. `open: cannot open path`: on Windows the MSYS2 `mktemp` returns a POSIX path
   a native `nift.exe` cannot open. Fixed by verifying the helper path is
   reachable before use (`d3f8258`), then by preferring the PowerShell temp
   helper on Windows so a rejected `mktemp` cannot orphan its file (`71d7eca`).
3. The MSYS2/cygwin `curl` mishandles native Windows paths passed to
   `--data-binary @file`; the CI now uses the native Windows curl and a Windows
   temp dir (`6cfbbe7`).
4. Windows decoded subprocess output as cp1252, mis-comparing the UTF-8
   response body; the harness now decodes as UTF-8 (`e7149de`).

These are exactly the platform-specific process/temp-file behaviours the
certification was meant to exercise; none required weakening a test.
