# nift-go — Go binding for Nift Embed

A thin, ownership-safe, concurrency-safe Go binding over the frozen Nift Embed C
ABI (`include/nift/c_abi.h`). It deliberately **does not reimplement any Nift
semantics**: Go translates types, ownership and callbacks; parsing/rendering
semantics live in the canonical C++ Embed engine behind the ABI.

```
Go API
   ↓
C ABI (nift_engine_*)
   ↓
canonical C++ Embed
```

## Building

The binding links `libnift_c.a` (built by `make libnift_c.a` at the repository
root) and the public header.

```bash
make libnift_c.a
cd bindings/go
go test ./...          # unit / lifetime / callback / pagination tests
go test -race ./...    # race detector is an acceptance gate
go build -o embed-harness ./cmd/embed-harness   # neutral-protocol harness
```

## API

- `Engine`: `NewEngine` / `OpenEngine` / `Close`, `SetRoot`, `Reload`,
  `SetLoader` / `SetEnvironmentProvider`, `SetString/Int/Number/Bool/JSON`,
  `Render` / `RenderSources` / `RenderPartial` / `RenderPage`.
- `Context`: page identity, current output, title, bindings.
- `Result`: `OK`, `Output`, `Error`, `Dependencies`, `Requirements`, `Pages`.
- `HostResult`: `Found(value)` / `NotFound` / `Error(diagnostic)`.

Host failure during rendering is a rendering outcome: the render returns a
`Result` with `OK == false` and the diagnostic; the Go error is reserved for
mechanical ABI failures (invalid arguments, internal).

## Ownership

Everything borrowed from C is copied into Go-owned memory before the C lifetime
can end. A Go `Result` stays valid after subsequent renders, reloads, and engine
destruction.

## Callbacks

Loader/environment providers map to `Found/NotFound/Error`. A Go panic inside a
callback is caught at the exported cgo boundary and becomes a controlled host
failure (never unwound through C). The C++ pagination worker threads may invoke
the Go environment provider; this is covered by `TestPaginationWorker*` and the
race detector.

## Shared corpus

`embed/adapters/go-embed` in `nift-embed-regression-suite` runs the frozen
29-case implementation-neutral corpus through this binding, requiring Go ==
frozen expectation alongside C++ / nift-rs / C ABI.

## Known limitations

Host-provider callback output is copied into C buffers retained until the engine
is closed (concurrent pagination-worker callbacks make freeing a peer's buffer
unsafe). Callback memory grows with total host invocations over an engine's
lifetime and is reclaimed at `Close`; a per-render buffer pool is deferred to the
hardening campaign.

`HostResult.Error` is exposed for a host failure diagnostic, but the current
frozen C ABI callback signature transports only a generic failure: the C++ side
maps any hard callback status to the fixed "host callback failed" message. The
full `ERROR(diagnostic)` host-resource contract is preserved for native C++
providers; a foreign binding's diagnostic is pending the smallest proposed ABI
repair (reuse the callback's `out` as the failure diagnostic; no signature
change). See the CP11.1 review proposal.
