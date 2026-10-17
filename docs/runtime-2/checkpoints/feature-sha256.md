# feature-sha256 — Native SHA-256

Status: implemented on branch `feature-sha256` (from `runtime-2` @`eaee971`);
same-model self-review done; independent adversarial review pending; not
certified (`make certify` is run by the owner). Isolated from the CP11 VM work:
no file under `src/rt/` changes.

## Objective

A permanent, native, high-performance SHA-256 capability for release
manifests, artifact verification, integrity checks, binary data and content
addressing. The Labs filesystem-automation benchmark marks Nift "not
available" for "Native SHA-256 / 10k files"; the core handover there lists
"native SHA-256 or a named cryptographic hash API" as capability work.

## API and rationale

| Built-in | Input | Result |
| --- | --- | --- |
| `sha256(value)` | string (its UTF-8 bytes as stored) or bytes | 64 lowercase hex characters |
| `sha256_file(path)` | a file path | 64 lowercase hex characters of the file's raw bytes |

User documentation: `docs/HASHING-API.md`; website queue:
`docs/handover/PENDING-WEBSITE.md`.

- **Conventions followed.** Built-ins are free snake_case functions
  (`open_bytes`, `is_file`, `make_dir`, `secure_random_bytes`); file access
  takes a path string resolved by the shared `checked_path` (relative,
  absolute, `~`, `--fs-root`), not a `file()` object (`file().open()` loads the
  whole file into the object's working buffer, which streaming must avoid).
  `sha256_file` mirrors `open`/`open_bytes` errors exactly: recoverable
  `io.open_failed` ("cannot open path") for missing/unopenable files,
  recoverable `io.read_failed` ("input failure") for read errors, a plain error
  for directories. Type errors are plain errors with the built-in's name, as
  for `html_escape`/`bytes`.
- **String vs file content are separate names.** `sha256("x.txt")` hashes the
  text `x.txt`; `sha256_file("x.txt")` hashes the file. No overload on
  argument type, so a path string can never be mistaken for content.
- **No normalisation.** Strings are hashed as stored (line endings, trailing
  whitespace, Unicode forms all matter). Non-string/bytes values are refused
  instead of being stringified implicitly (`1` vs `1.0`, object key order):
  callers write `.stringify()` so the hashed spelling is explicit. Handle
  strings (callables, structs, files, …) are refused as their `type()` is not
  `string`.
- **Hex result.** The bytes campaign states that no hex convention exists for
  bytes values; this API does not add one to bytes. The digest is returned in
  its universal interchange spelling (sha256sum, manifests, the benchmark's
  rows), which is what every identified use needs. A raw-digest variant is
  deliberately deferred (no consumer; it can be added additively under its
  own name).
- **Small surface.** No algorithm parameter, HMAC or hash objects.
- **Precedence.** New natives normally win over same-named user functions at
  top level (legacy, per-name precedence). For `sha256`/`sha256_file`, a user,
  module, host or callable-binding definition with the same name wins
  everywhere, exactly like the existing `error` rule
  (`canonical_native_call`), so no existing program changes meaning, and the
  behaviour already matches Runtime 2.0's D-009#3 target. A scan of
  `tests/`, the regression suite clone and the package clones found no program
  defining either name.

## Implementation and provenance

- `src/Sha256.{h,cpp}` (`nift::crypto`): `Sha256` incremental hasher
  (`update`/`finish`/`reset`), `sha256_bytes(ptr, size)`, `sha256_hex`,
  `sha256_file(path, observer, context)` returning
  `{status, error_code, size, digest}` with `status ∈ {Ok, NotFound,
  IsDirectory, OpenFailed, ReadFailed}`. It knows nothing about the textual
  evaluator, `RuntimeValue` or `Parser`.
- **Vendored primitives:** `third_party/bitcoin-sha256/sha256_bitcoin.h`, a
  subset of Bitcoin Core v29.0 (`f490f556…`, MIT; SHA-NI code originally
  public domain by Jeffrey Walton/Intel). Five blocks are byte-for-byte copies
  of upstream line ranges (portable transform, streaming `Write`/`Finalize`,
  one-way SHA-NI transform); everything else is marked glue (C++17 endian
  helpers replacing upstream's C++20 `common.h`, namespaces, per-function
  target pragmas). `NIFT-PROVENANCE.md` records commit, file and block hashes;
  `scripts/check_vendored_sha256.py` verifies them (and, with `--upstream`,
  against a checkout — done for this checkpoint). License text added to
  `THIRD_PARTY_NOTICES.md` (and its Node package copy); release bundles copy
  `COPYING` as `LICENSE-bitcoin-sha256`; the Python wheel ships it under
  `nift/licenses`.
- **Why not other options.** No SHA-256 existed in the repository. OpenSSL is
  present on this Linux host but is not a build dependency on macOS/Windows
  and would add a hard system dependency; platform APIs (CommonCrypto,
  BCrypt) would mean three implementations. The vendored code builds
  everywhere Nift builds with no new flags or libraries, like `minifypp`,
  `markuppp`/cmark and libffi.
- **Hardware acceleration.** x86/x86-64 with GCC/Clang: SHA-NI transform,
  compiled with `#pragma GCC target("sse4.1,sha")` / `#pragma clang
  attribute` so no per-file flags are needed. Selected lazily and atomically
  at first use when CPUID reports leaf 7 (checked against the maximum leaf),
  SHA, SSE4.1 and SSSE3, and a two-block known-answer self-test passes;
  otherwise the portable transform. `-DNIFT_SHA256_DISABLE_X86_SHANI` builds
  the portable-only configuration (tested on x86 to cover what non-x86
  targets compile). ARMv8 SHA2 instructions are **future work**: upstream has
  the transform, but it could not be compiled or run on this host (no aarch64
  toolchain or emulator), so it is not vendored. macOS arm64 and Linux arm64
  currently use the portable transform (~250 MB/s class).
- **Streaming.** `sha256_file` opens once (`open(O_RDONLY|O_CLOEXEC)` with
  EINTR retry; `CreateFileW` with the native wide path and
  `FILE_FLAG_SEQUENTIAL_SCAN` on Windows), reads 64 KiB chunks into one heap
  buffer per call (not the stack: interpreter threads may run deep on 512 KiB
  macOS stacks), hashes each chunk, never holds the file. No `stat` before
  open: a directory is classified from `EISDIR` on read (POSIX) or
  `ERROR_ACCESS_DENIED` + attributes (Windows). `ENOENT`/`ENOTDIR` map to
  `NotFound`, matching `std::filesystem`'s not-found classification used by
  `exists()`/`stat()`. Symlinks are followed by `open`. The Windows branch is
  written against documented Win32 behaviour but was **not compiled here**
  (no MinGW toolchain on the host); it is covered by the cross-platform CI
  matrix at integration.
- **Legacy wiring** (`src/ParserExpression.cpp`): two entries in the native
  dispatch next to `open_bytes`, guarded by `canonical_native_call` before
  arguments are parsed (so a shadowing user function never sees its spread
  arguments evaluated twice). `src/CLI.cpp` lists both names for shell
  completion.

## Dependency tracking

- A successful `sha256_file` in a page render calls
  `observe_streamed_read(path, size, snapshot)`: the path becomes an ordinary
  dependency (modified mode: mtime), and in hash/hybrid builds the FNV-1a
  content snapshot is computed **from the same streamed chunks** through the
  observer (`filesystem::hash_bytes_update`, the streaming form of
  `hash_bytes`, identical results), so the file is read once and the snapshot
  describes exactly the hashed bytes. Standalone scripts record nothing; the
  observer is not installed unless a snapshot is wanted.
- `NotFound` records the path as **absent** (CP02 `observe_presence`), so a
  page that catches the error rebuilds when the file appears.
- Host-op trace (CP08): `{"op":"read","arg":<abs path>,"res":<bytes>}` per
  successful read (`trace-format.md` updated).
- **Pre-existing defect found and fixed** (`src/ParserTemplate.cpp`, `@try`):
  when a try block failed recoverably, `result_` was restored to its pre-try
  snapshot, discarding every dependency and observation the block had already
  recorded, although bindings assigned inside the block survive. Repro on the
  baseline: `r := "t0"; try { r = open("data/t.txt"); x := open("missing") }
  catch(e) {...}; return r` renders `t.txt`'s content, but editing `t.txt`
  did not rebuild the page (stale output in all modes). Now the failed block's
  dependencies, requirements and observations are merged back (output is
  still discarded). Recording more can only add rebuilds, never stale output.
  Without this the caught-missing-file absence above was lost too.
- Evidence (`tests/sha256_builtins.sh`, each in modified/hash/hybrid): initial
  build, trace line present, unchanged inputs stay up to date, editing the
  hashed file rebuilds with the new digest, editing an external file (outside
  the project) rebuilds, a caught missing file appearing rebuilds, a read
  inside a failed try block stays a dependency, and in hash mode `touch`
  without content change stays up to date (hybrid rebuilds on a newer mtime,
  as it does for `@input`).

## Tests

`make test-sha256` (added to `make test`, so `test-all`/`certify-targets`, and
to `tests/runtime2/certification_guard.sh` REQUIRED):

| Part | Result |
| --- | --- |
| `check_vendored_sha256.py` (5 verbatim blocks; also run with `--upstream`) | pass; a one-character mutation is detected |
| `sha256-unit` (both transforms) | 1310 checks, 0 failures as root; 1312 as an unprivileged user (unreadable-file checks run) |
| `sha256-unit-san` (ASan+UBSan, `-fno-sanitize-recover=undefined`) | 1310 checks, 0 failures; 1312 unprivileged |
| `sha256-unit-portable` (`NIFT_SHA256_DISABLE_X86_SHANI`) | 655 checks, 0 failures |
| `tests/sha256_builtins.sh` | 79 checks, 0 failures |

Unit coverage: FIPS 180-4 / NIST examples (empty, "abc", 448-bit, 896-bit,
one million `a` in 997-byte pieces) and SHA2_Additional cases (one byte,
four bytes, 55/56/57/64 zero bytes, 1000×`A`, 1005×`U`); NFC ≠ NFD;
reuse/reset; every size 0–260 plus 511/512/513 … 65537/100003 against an
independent textbook implementation written in the test, one-shot and with
random splits; files of 0, 1, 55, 56, 63, 64, 65, buffer−1/buffer/buffer+1,
2×buffer, 3×buffer+17 and 5 MiB+3 bytes (digest, size, observer sees exactly
the bytes, chunk bound, repeat); all byte values and NULs; missing,
file-as-directory, directory, symlink, dangling symlink, unreadable (skipped
as root); 8 threads concurrently.

Language/build coverage: NIST vectors through `sha256`; bytes vs string;
UTF-8 and NFC/NFD; NULs via `bytes`, `open()` strings and files; CRLF not
normalised; 18 boundary-sized files plus 20 random binary files (0–300 KB)
against python3 hashlib **and** `sha256sum`; 50× repeated hashing; loops and
functions; 4 threads + 4 async tasks; errors and codes (missing, through a
file, directory, wrong types, arity, handles); unreadable file (as root the
check runs as `nobody` via `setpriv`); absolute/`..`/symlinked paths outside
the project, dangling symlink; `--fs-root` restricts; user definitions shadow
both names at top level and in loops; the build cases above.

Regression suites run on this branch (all pass): see "Regression runs" below.

## Performance

Host: 2 vCPU Intel Xeon @ 2.10 GHz with SHA-NI, shared with a concurrent CP11
build (load 1.5–2.9), ext4, warm page cache. `make benchmark-sha256`
(`tests/sha256_bench.cpp`):

| Measurement | portable | x86 SHA-NI |
| --- | ---: | ---: |
| in memory, 1 MiB messages | 266 MB/s | 1378 MB/s |
| in memory, 4 KiB messages | 269 MB/s | 1347 MB/s |
| in memory, 700 B messages | 246 MB/s (2.85 µs) | 1189 MB/s (0.59 µs) |
| in memory, 64 B messages | 130 MB/s | 596 MB/s |
| `sha256_file`, 256 MiB warm file | 248 MB/s | 1173 MB/s |
| 10,000 warm 96–2048 B files, `sha256_file` | 5.48 µs/file | 3.13 µs/file |
| same files, open/read/close only | 2.40 µs/file | 2.38 µs/file |

Reference tools on the same 256 MiB file, whole process: `sha256sum` 0.248–
0.255 s (≈1.06 GB/s), `openssl dgst -sha256` 0.257–0.295 s, `nift` script
`print(sha256_file(...))` 0.251–0.257 s (≈1.05 GB/s), all printing the same
digest; `openssl speed sha256` 1 MiB blocks 1371 MB/s; Python `hashlib` in
memory ≈1.28 GB/s. The native module is at parity with OpenSSL's SHA-NI path.

End to end, `hash-sha256-10000` with the benchmark's own fixture generator
and adapters (local, unofficial, see the fs-bench branch below), all rows
identical to the hashlib oracle for every engine:

| | fresh | booted |
| --- | ---: | ---: |
| Nift | 364–392 ms | 387–395 ms |
| Python 3.13 | 345 ms | 259 ms |
| Node 22 | 291 ms | 226 ms |
| Bun 1.4.2 | 157 ms | 140 ms |
| Go 1.24 | 140 ms | 168 ms |

Decomposition for Nift (same host): walk alone ≈127 ms, walk + `stat` per
file ≈193 ms, walk + `stat` + `sha256_file` ≈ 365–390 ms. The native hashing
of all 10,000 files including open/read/close is ≈31 ms; the rest of the
increment is evaluator cost. Microbenchmarks (20,000 iterations): textual
native dispatch costs ≈5–6 µs per call for any native (`sha256("abc")`
6.3 µs, `html_escape("abc")` 6.4 µs; prepared value-only natives such as
`epoch()` 1.9 µs); `sha256_file(p)` ≈9.7 µs (≈3 µs native, ≈1.5 µs path
argument handling: `fs::absolute` + `lexically_normal` in the shared
`resolve_path`), cheaper than `open_bytes(p)` (12.1 µs). Investigated and not
changed: (a) adding the names to the prepared value-only native fast path
(`ParserTemplate.cpp`) would save ≈4 µs/call (≈40 ms per 10k) but adds a
second dispatch site with its own precedence rules to the legacy evaluator
that CP25 deletes; the VM native table is the place for that saving. (b) Path
normalisation is shared by every filesystem built-in and belongs to the CP16
Host-op path work. (c) The per-call 64 KiB heap buffer costs ≈0.1 µs/file.
No benchmark special-casing exists anywhere in the implementation.

## VM integration plan (no VM code changed)

CP11's native table (`src/rt/native.h` on the CP11 branches) binds natives to
`NativeId` at compile time and dispatches in `Vm::native` on register values.

1. **`sha256` (pure, CP11-compatible).** Add `NativeId::Sha256` (arity 1).
   In `Vm::native`: `const Value& v = R[in.a]`; for `Kind::String` take
   `string_view_of(v)`, for `Kind::Bytes` `bytes_of(v)`, call
   `nift::crypto::sha256_bytes(data, size)`, format with the
   `sha256_hex(digest, char[64])` overload into a stack buffer and
   `R[in.a] = make_string(std::string_view(buf, 64))`. Any other kind (including
   handles, which are typed in the VM) faults with
   `sha256: expected a string or bytes value`. No argument re-parsing, no
   `RuntimeValue`, no intermediate `std::string`.
2. **`sha256_file` (Host op, CP16).** Path resolution, `--fs-root` policy,
   dependency/absence recording and tracing belong to the Host-op layer. Add a
   Host op `hash_file(path) -> {status, size, digest}` that resolves the path
   like `checked_path`, calls `nift::crypto::sha256_file` with the snapshot
   observer when the host wants one, and records `observe_streamed_read` /
   `observe_presence(path, false)` from that single choke point. The VM maps
   `Sha256FileStatus` to the same diagnostics (`io.open_failed`,
   `io.read_failed` recoverable; directory fatal). Until CP16 the compiler
   should refuse units that call `sha256_file` (fall back before execution,
   D-013/D-015), as for other filesystem natives.
3. **Precedence.** Both names already follow the Runtime 2.0 rule (lexical and
   user definitions win), so they need no entry in `reserved_name()`; if
   `legacy_names.cpp` is regenerated from the legacy source it may list them,
   which is harmless (conservative fallback). rt-diff corpus: add
   `sha256`/`sha256_file` probes (stdout + `read` trace lines) when the VM
   supports them.
4. **Threads/async.** The module is thread-safe (stateless functions,
   per-object hashers, atomic transform selection), so VM workers can call it
   directly.

## Benchmark integration (fs-bench, local branch `nift-sha256`, commit `e817419`)

- `adapters/nift.f`: `hash-sha256` appends `[rel, size, sha256_file(p)]`
  after all other per-file conditionals (same control flow as the peers).
- `scripts/capabilities.py`: probe `nift eval 'sha256_file("<tmp>/abc.txt")'`
  (outside the clock); `run.py`/`local.py` enable the Nift cell only when it
  passes; `run.py` records it in `environment.json`; `audit.py` expects Nift
  hash cells only when recorded, so historical evidence re-audits unchanged.
- `docs/NEXT-CAMPAIGN-NIFT-SHA256.md`: change, verification, methodology notes.
- Historical results, reports, `CAPABILITIES.md`, report renderers and
  publication data untouched; nothing pushed.
- Verified: all five engines return identical digests for all 10,000 files.
- Fairness notes: peers read whole files then hash, Nift streams (same bytes,
  one read per small file); all engines use SHA instructions on SHA-NI CPUs
  (record `sha_ni` on the cloud host — without it Nift's portable fallback is
  ~5× slower per byte while peers may use AVX2 kernels); at 10k small files
  the cell measures traversal + metadata + evaluator dispatch far more than
  hashing, and must not be read as hash throughput.

## Recommendation: modification-time support (separate checkpoint)

The Labs page excludes Nift from three timestamp change-detection workloads
because `stat()` returns only `exists`, `type`, `size`.

- **API:** extend `stat()` (no new built-in): `mtime_ns` — integer
  nanoseconds since the Unix epoch, exact as the filesystem reports it (int64
  covers 1677–2262; Nift integers are exact int64) — and `mtime` — integer
  milliseconds, directly comparable with `epoch()` (which returns integer
  milliseconds). Present for every existing path (files and directories), as
  `size` is for files. Pre-1970 times are negative, not errors.
- **Implementation:** reuse `filesystem::dependency_status_native` (already
  returns seconds + nanoseconds on POSIX and Windows for the build engine), or
  add `STATX_MTIME` to the existing Linux `statx` mask in
  `inspect_path_unobserved` (no extra syscall); macOS `st_mtimespec`; Windows
  `FILETIME` (100 ns ticks since 1601) converted with the same code the build
  engine uses.
- **Precision:** document that resolution is the filesystem's (ext4/APFS/NTFS
  ns/100 ns, HFS+ 1 s, FAT 2 s, exFAT 10 ms, some network filesystems
  coarser); never round silently; `mtime` truncates toward negative infinity.
- **Symlinks:** `stat()` follows symlinks (target's mtime), consistent with
  today. Link mtimes belong with the deferred `lstat`-style API.
- **Errors:** unchanged (`io.metadata_failed`; missing → `{exists:false}`).
- **Clocks:** file timestamps come from the kernel's coarse filesystem clock;
  `epoch()` from the realtime clock, which can be up to a tick ahead (CP01).
  "Modified since I started" must compare against a timestamp taken from the
  filesystem (touch a marker and stat it), and docs must say so; equality of
  integer seconds is what the benchmark's peers compare.
- **Dependency tracking:** reading `mtime` makes output depend on metadata the
  current dependency kinds do not capture (hash mode ignores mtime-only
  changes; modified mode would treat the read as content). Add a CP02
  observation kind `mtimes` (spelling → recorded `mtime_ns`) re-checked in
  `build_reasons`, dropped for paths the page writes (`note_written`) to avoid
  perpetual rebuilds, and merged across pagination like the others. Expect
  rebuilds after `touch` even in hash mode for pages that display times —
  that is correct.
- **Tests:** per-platform precision probes, symlink, directory, negative
  times (`touch -d 1960-01-01`), the three Labs detect-mtime variants, and
  rebuild-on-touch / no-rebuild-without-touch in all three modes.

## Risks and open items

- Windows `sha256_file` and the clang/MinGW pragma path are untested here;
  first exercised by the cross-platform CI at integration.
- ARMv8 SHA acceleration not vendored (future work, needs an arm64 runner).
- A FIFO or device path blocks or streams until EOF, as with `sha256sum`.
- Reads inside `async` functions are not recorded as dependencies (pre-existing
  CP02 item 8, CP16), and permission-denied failures are not recorded (a page
  that catches one does not rebuild when permissions change; same as `open`).
- `open()` on a missing file does not record absence (pre-existing; only
  `sha256_file` does). Left unchanged to keep this checkpoint's scope.
- The `@try` observation fix changes rebuild behaviour (more rebuilds, never
  fewer) for pages whose try blocks read files before failing.
- `progress.md` is not edited here to avoid conflicts with the CP11 line; add
  a row when this branch is integrated. Release notes likewise.

## Regression runs (this branch, Linux, as root unless noted)

Pass: `test-runtime2-certification-guard`, `test-runtime2-dynamic-reads`,
`test-runtime2-fs-freedom`, `test-contracts`, `test-runtime2-vm-corpus`,
`test-runtime2-rt-diff`, `test-filesystem-type`, `test-cp19-bytes`,
`test-control-flow`, `test-requirements`, `test-path-safety`,
`test-metadata-safety`, `test-v42-language`, `test-parser-hardening`,
`test-commands`, `test-diagnostics`, `test-v410-native-dispatch-parity`,
`test-prepared-method-parity`, `test-incremental-state-transitions`,
`test-v48-callable-parity`, `test-conformance` (9/9).
`test-v43-language`: every script passes except
`v43_cp112_filevalue_wall_smoke.sh`, which fails as root on the baseline
binary too (read-only directory is writable by root) and passes as the
unprivileged `tester` user.
`nift-regression-suite` (`runtime-2` @`523d1df`): 93/94 modules pass; the
94th, `unreadable_source_smoke.sh`, is the documented root-only failure and
passes as `tester`.
Not run here: `make certify`, `test-all` (owner runs certification).
