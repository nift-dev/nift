# Nift v4.5.0

Nift v4.5.0 turns the native language and shell into a supported reusable
scripting and embedding runtime. It keeps Nift's dependency-aware website build
core while adding direct script execution, a persistent interactive shell,
native concurrency, C-ABI FFI and a supported host-owned Engine/C ABI surface.

## Scripts and interactive shell

- **Direct invocation.** Run a script with `nift script.f`. Plain `nift` opens
  the persistent interactive shell; the old `nift run` and `nift sh` wrappers
  are removed. `-e`/`-c`, `-i`, stdin via `nift -` and executable shebang
  scripts provide consistent non-project execution paths.
- **Stable invocation context.** Script code receives immutable `cmd` and
  `args` values with defined identities for files, inline source, stdin and the
  REPL. `--` preserves literal option-looking script arguments.
- **Host and target introspection.** `os()` and `arch()` report the host using
  stable vocabularies. Runtime-owned `platform()` reports the explicitly
  selected runtime target; it is isolated per runtime and is distinct from
  `init --target`, which continues to select a hosting/deployment provider.
- **Shell job control.** On supported POSIX terminals, background `&`, `jobs`,
  `fg`, `bg` and `wait` use real process groups and terminal handoff. Windows
  keeps the portable foreground process surface and reports POSIX job control
  as unsupported rather than emulating it inaccurately.

## Native concurrency and asynchronous work

- **Threads and shared state.** `thread(...)`, replayable `join`,
  `hardware_concurrency()` and mutex-backed explicitly shared state provide
  native parallel execution with bounded lifetime and teardown rules.
- **Atomics.** `atomic<int>` and `atomic<bool>` provide sequentially consistent
  scalar access. Integer atomics support assignment, `++`, `--`, `+=`, `-=`,
  `&=`, `|=`, `^=` and `%=` operations.
- **Async functions and futures.** Async functions run on a bounded native
  worker pool and return replayable futures. `await future` and
  `await async_fn(...)` propagate results and errors while nested awaits retain
  forward progress. Runtime teardown waits safely for owned work.

## FFI and embedding

- **C-ABI FFI.** Scripts can load native `.so`, `.dylib` and `.dll` libraries
  and call explicitly typed C ABI symbols. The supported surface includes
  scalar values, C strings, pointers, Nift-owned buffers, declared struct
  layouts and bounded synchronous callbacks. Invalid signatures, closed
  handles and unsupported calling patterns fail explicitly.
- **Supported embedding runtime.** `nift::Engine` now supports persistent script
  execution/evaluation alongside template rendering, host values and functions,
  named Nift callable invocation, isolated Engine instances and defined
  concurrency/lifetime behavior.
- **C ABI 1.1 and bindings.** Staged C/C++ headers, static/shared libraries and
  `pkg-config` metadata ship with C ABI 1.1. Maintained Go, Python, Node and C#
  bindings track the supported runtime surface and share conformance coverage.

## Compatibility and platform notes

- Existing project build, template, JSON/schema, typed-content, taxonomy,
  package and incremental-build behavior remains supported. Direct invocation
  replaces only the removed `run`/`sh` CLI wrappers.
- POSIX interactive job control is intentionally platform-specific. Portable
  process execution, scripts, concurrency, FFI and embedding are certified on
  the supported Linux, macOS and Windows release targets, with platform-native
  library naming/loading behavior.
- FFI remains an explicitly typed unsafe boundary: callers are responsible for
  matching native signatures and documented ownership rules. Callbacks are
  synchronous and lifetime-bounded; unsupported ABI guesses are rejected.

## Certification

The release candidate passed the complete independent 77-module contract
(including 574 historical assertions), warning-clean optimized GCC and Clang
builds, maintained binding and staged-consumer walls, ASan/UBSan/LSan, TSan,
Valgrind, real-terminal shell/job-control dogfood, performance/resource gates,
the synchronized 96-file public website build and hosted Linux/macOS/Windows
matrices. The complete manually dispatched Deep guards workflow passed parser
fuzzing, sanitized core lifecycle, watch endurance, shared-data isolation and
incremental clean-build equivalence at the certified product commit.

Release archives contain the CLI binary, license and supported distribution
layout for Linux x86-64, macOS arm64, macOS x86-64 and Windows x86-64. Package
manager publication is a separate post-release phase.
