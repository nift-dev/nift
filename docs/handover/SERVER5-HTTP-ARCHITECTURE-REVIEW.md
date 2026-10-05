# SERVER5-HTTP-ARCHITECTURE-REVIEW

Investigation/benchmarking/design checkpoint. **No Nift source/package/runtime
change was made.** Nift is frozen at `6e121e5`. This document recommends; it does
not implement.

## Baseline

- Nift local HEAD: `6e121e5` (development `Nift v4.7.0`; C ABI 1.3).
- Nift `origin/main`: `52536b9` (unpushed; 8 local commits).
- http package: `4defe98`; socket package: `0244721`; Strut: `74d4611` (v0.0.3-167).
- Nothing pushed; Snap/Homebrew/Strut untouched; C ABI unchanged.

## Historical HTTP work (reconstructed)

- The Nift `http` package is an **HTTP server** facade (the client is the separate
  `curl` package). Two backends in `src/http.f`: **process** (default; spawns
  `helper/http_helper.py`, a stdlib Python HTTP/1.1 server) and **native**
  (pure Nift over the `socket` package).
- `socket` is TCP over Nift FFI (libc/libSystem/ws2_32), non-blocking, plain
  transferable map handles; certified Linux/macOS/Windows.
- The optional `src/native_parse.c` (`build_native_parser.sh`) is a single
  `http_parse` C function; opt-in via server config `native_parser_lib`
  (experimental prototype, not a default).
- SERVER1–SERVER4 built the socket + native backends; **SERVER4 concluded PAUSE**
  (native is correct/self-contained but architecturally limited). SERVER5 was to
  address concurrency/admission/fault isolation (a worker/pool + keep-alive).
- Retained historical figures: native interpreted ~18–24 req/s; FFI-parser
  ~27–36 req/s; process-persistent ~608 req/s (`tests/compare.py`). Benchmark
  harnesses: `http/tests/benchmark.py`, `http/tests/compare.py`.

## Fresh benchmarks (this review, same machine)

`http/tests/benchmark.py` (native, interpreted Nift parser, `/text`,
`Connection: close`):

| concurrency | v4.6.0 `bb6e9f2` | current `6e121e5` |
|---|---:|---:|
| 1 | 19.4 req/s | 19.8 req/s |
| 4 | 22.4 | 22.4 |
| 16 | 22.4 | 22.4 |
| 32 | 27.8 | 24.6 |

`http/tests/compare.py` (current `6e121e5`):

| config | text c=1 | text c=4 | text c=16 |
|---|---:|---:|---:|
| native (FFI parser, conc 16) | 37.2 | 41.0 | 46.5 |
| process_default (conc 1) | 51.5 | 53.0 | 55.8 |
| process_persistent (conc 4, pool 4) | 172.0 | **680.2** | **763.5** |

Disposable poll-sensitivity harness (`/tmp/opencode/http_poll_bench.py`, native,
interpreted): `poll_timeout_ms` = 10 / 1 / 0 all give ~15–24 req/s — the poll
interval is **not** the bottleneck.

## Runtime-campaign effect on HTTP

**None measurable.** `benchmark.py` on v4.6.0 and on the post-campaign runtime is
identical within noise (~20 req/s across concurrency). The runtime campaign's
gains were in bare function-call statements and `map.set`/map iteration in
script loop bodies — patterns the native HTTP path does not exercise (its cost is
socket/connection handling and per-byte request parsing, plus map *literals*).
The HTTP conclusion is **unchanged**: HTTP is socket/protocol/connection-bound,
not interpreter-hot-path-bound.

## Current bottleneck breakdown (current runtime)

```text
interpreted native   ~20 req/s  (50 ms/request)
FFI-parser native    ~37-46     (the per-byte request parser is ~half the cost)
process_default      ~51-56     (a fresh Nift+Python process per request is
                                 FASTER than the native single-threaded loop)
process_persistent   ~680-763   (the retained reference)
poll_timeout_ms      no effect
```

- **Request parsing** (interpreted, per-byte) ≈ 50% of native latency; the FFI
  `http_parse` primitive removes most of it.
- **Connection/poll/event-loop + per-request Nift work** (accept/recv/send/close,
  map construction, response serialization) is the rest. A fresh process per
  request beating the native loop shows the single-threaded poll architecture
  itself is the dominant cost, not Nift evaluation.
- **Head-of-line blocking** (SERVER-BENCH): a slow handler stalls others ~4×.
- **No keep-alive**: every request is a new connection.

## Nift http/socket assessment

- `socket`: solid FFI transport (non-blocking, cross-platform, byte-safe, cached
  recv buffer). Constraint: the cached `ffi_buffer` handle lives in the
  execution context's FFI storage → worker-clone ownership hazard; no DNS/IPv6/
  TLS. Socket API returns maps per call (accept/recv/poll) → per-call allocation.
- `http` native backend: single-threaded poll loop, `Connection: close`, text/
  json/bytes responses only (forms/multipart/file/stream absent), no signal
  handling/observability. The process backend has the full feature set + limits
  + shutdown + overload 503 and is the default oracle.
- The parts worth keeping in Nift regardless of backend: routing, request/
  response descriptors, handlers, limits table, error-status mapping.

## Strut capability assessment (native-helper direction)

Strut **already has** a production, cross-certified TCP + HTTP/1.x server stack
(listen/accept/read/write, timeouts, keep-alive, pipelining, chunked, bounded
worker pool, futures/channels/atomics, `bytes`/streams, graceful shutdown).

**Decisive blocker:** Strut's FFI is **one-way (Strut → C only)**. There is no
`extern "C"` *definition*/export form (`src/parser.cpp:552` rejects a body), the
compiler only emits executables (no `-shared`/library mode, `src/codegen.cpp:1892`),
there is no C-ABI façade over Strut runtime types, and C callbacks are deferred.
Therefore **Nift cannot load a Strut-built shared helper via FFI**, and a Strut
helper could not call back into Nift handlers. Classification: **ARCHITECTURE
BLOCKER as designed**; making it viable is a *significant Strut prerequisite*
(export/embedding layer), not an HTTP task.

## Architecture options

| Option | Throughput | Nift change? | Verdict |
|---|---|---|---|
| A. Pure Nift native (as-is) | ~20 (interp) / ~40 (FFI parser) | no | architecture-limited; not production-viable |
| A′. Pure Nift + keep-alive + worker/pool | ? (est. ~100s+) | **YES** (http pkg + facade transferability) | needs Nick approval |
| B. Nift + FFI parser helper (exists) | ~40 | no | parser only; does not fix the loop |
| C. Nift + compiled C/C++ socket/server helper | potentially 10⁴+ | no (new C artifact) | plausible; new component; per-request Nift callback is the open question |
| D. Process backend (Python, persistent) | ~680–763 | no | retained production path today |
| E. Nift + Strut native helper (FFI) | — | Strut change | **BLOCKED** (one-way FFI) |

## Recommendation

**PAUSE the pure-Nift-native path as a production backend.** The evidence says
the native Nift HTTP regression/limitation is **not** the interpreter (the
runtime campaign changed nothing), so a further Nift-runtime investment will not
move HTTP. A production native path requires architectural changes that are out
of scope for SERVER5:

- **NIFT CHANGE REQUIRED (finding #1)** — a pure-Nift native backend needs
  keep-alive, a worker/pool (SERVER4/SERVER5), and a transferable http facade.
  Those are Nift//package changes. Details:
  - required change: HTTP-layer keep-alive + bounded worker/pool + transferable
    handler/facade across workers (or an admission/backpressure primitive);
  - why SERVER5 needs it: the single-threaded, connection-per-request poll loop
    is the bottleneck; a fresh process per request already beats it;
  - current workaround: none in pure Nift (the http facade is non-transferable);
  - expected benefit: removes HOL blocking + per-request connection setup;
  - public/C ABI impact: none expected, but M4/C-ABI-neutral;
  - recommended Nift checkpoint: separate, after approval.
- **STRUT CHANGE REQUIRED (finding #2)** — a Strut native helper needs a Strut
  export/embedding layer (export syntax + library output + C ABI façade). This
  is a significant Strut project, not an HTTP task. Not implemented.

Given both, the retained production path remains the **process backend**
(~680–763 req/s). The only path that needs *no* Nift change and could plausibly
reach production native throughput is **Option C (a compiled C/C++ server
helper)**, whose open question is per-request callback into Nift (C ABI 1.3
embedding: `nift_engine_*` exists; engine concurrency per request is unproven).

## Benchmark/certification plan (if pursued)

- Native decoded: `/text`, `/json/:id`, `/echo` 1k/64KiB, `/large` 128KiB, at
  c=1/4/16/32; plus keep-alive vs connection-per-request; latency percentiles;
  throughput; peak RSS; CPU; slowloris + slow-reader adversarial.
- Comparisons: v4.6.0, current `6e121e5`, process-default, process-persistent,
  and (if built) the compiled helper.
- Gates: `http/tests/{benchmark,compare,native_test,differential}.py`,
  `socket/tests/socket_test.py`; `2xx==0` invalid-run rejection retained.

## Recommended SERVER5 campaign

Recommendation is **PAUSE** (no implementation). If Nick chooses to proceed on
Option C (compiled helper, no Nift change), the campaign would be:

```text
SERVER5-CP0  architecture decision + harness (no code): confirm Option C vs D;
             freeze the benchmark matrix + semantics list; decide the Nift↔helper
             boundary (C ABI 1.3 embedding vs out-of-process).
SERVER5-CP1  boundary prototype (disposable, outside Nift): a minimal C server
             that embeds libnift (C ABI 1.3) and calls one Nift handler; prove
             per-request callback + engine concurrency/isolation.
SERVER5-CP2  implement keep-alive + parsing + connection lifecycle in the helper;
             parity against the process backend for routing/limits/errors.
SERVER5-CP3  concurrency/admission/backpressure + adversarial hardening.
SERVER5-CP4  differential + benchmark + cross-platform certification.
SERVER5-FINAL  decide native-vs-process; document residuals.
```

Each CP: accept = throughput/latency gain with process-backend parity and no
regression; rollback = the helper is a separate artifact (Nift untouched).

## Required Nick approvals

- Proceed with Option C (compiled helper) vs keep Option D (process).
- Any Nift/package change for a pure-Nift path (finding #1) — not implemented.
- Any Strut export/embedding work (finding #2) — not implemented.

## Deferred Nift hardening (recorded, not started)

Deep chained binary expressions (e.g. `1 + 1 + ...`) eventually exhaust the
recursive parser and segfault; ~4000+ terms is an implementation artifact, not a
specification. Future checkpoint: investigate iterative/flattened parsing, then
set an intentional boundary *if useful*, and guarantee a controlled diagnostic
(never a segfault). Not part of SERVER5.

## Status

- Nift modified: **NO** (HEAD `6e121e5`, clean, 0 commits).
- http/socket modified: **NO**; Strut modified: **NO**.
- Pushed: **NO**. Snap/Homebrew untouched.
- Findings surfaced (not implemented): Nift change for pure-Nift native (#1);
  Strut export/embedding (#2).
