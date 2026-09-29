# Gate 6A-R: exact-token libffi dispatch

Date: 2026-09-29

Status: **pending**. The Linux implementation and release-package evidence below
is local. Hosted macOS and Windows workflow evidence and maintainer review have
not completed, so this report does not close Review Gate 6A.

## Architecture

- `Parser::ffi_call` retains the current signature grammar, supported token set,
  generic arity limit (0-6), homogeneous `f32`/`f64` limit (0-4), and mixed
  float-class/integer rejection. No new callback form, by-value struct call,
  bytes API, CP16 work, or consumer API was added.
- Each token maps to an exact libffi ABI type: bool/`u8` to `ffi_type_uint8`,
  signed and unsigned integer widths to matching types, `f32`/`f64` to float and
  double, and `ptr`/`cstr`/`buffer`/`callback_i64` to pointer. Void maps to
  `ffi_type_void`.
- Arguments and results use typed storage selected for the prepared CIF.
  Narrow integer results use libffi's required `ffi_arg`/`ffi_sarg` widened
  storage, while register-sized and larger results use exact-width members.
  Runtime range checks occur before narrowing and conversion reads typed values,
  without byte-order assumptions. `buffer` and `callback_i64` returns are
  rejected explicitly as unsupported implementation accidents.
- Native symbols and callbacks use exact function-pointer types. The POSIX
  `dlsym` representation transfer is isolated and documented at that API
  boundary; callback pointers do not round-trip through `void*` or integers.
- The only callback remains synchronous `i64(i64)`. A scope guard owns the
  thread-local parser/tag/error activation and restores it on argument failure,
  nested-activation rejection, CIF failure, native return, and callback error.
- Struct calls remain pointer-only. `ffi_struct` and `ffi_sizeof` use
  `ffi_get_struct_offsets`. The unit oracle compares every supported scalar
  field offset and total size with C++ `offsetof`/`sizeof`; the C fixture
  independently checks total size and field-sensitive pointer interpretation.

## Vendored build

- Official builds configure `third_party/libffi` out of tree as static PIC only.
  Build paths are keyed by target triple and C compiler; generated `ffi.h`,
  `ffitarget.h`, configuration files, objects, and archives remain in `.build`.
- Vendored libffi is compiled with hidden visibility. ELF consumers additionally
  link with `--exclude-libs,ALL` and `-Bsymbolic`, and PE consumers use
  `--exclude-libs,ALL`, so archive members cannot become public/preemptible
  symbols in Nift shared artifacts. Mach-O consumes the hidden definitions.
- The vendored build fingerprint includes source and build-script identities,
  compiler paths and versions, flags, and configure arguments. Every consuming
  build validates that fingerprint, and sanitizer/TSan compilation waits for
  its matching generated headers.
- CLI and shared artifacts link the vendored archive directly. `libnift_c.a` is
  formed by copying the libffi archive and adding Nift objects, preserving the
  single consumer-linkable archive contract without GNU MRI assumptions.
- ASan/UBSan and TSan use independently instrumented libffi build directories.
  Direct Python and Node builds create invocation-local static libffi builds.
  Python sdist staging carries the source/build script and builds it during
  `build_ext` rather than consulting a system libffi.
- Native bundles include notices/licenses/provenance. Python wheel metadata
  stages the libffi license files. The ordinary checked-in Node package includes
  canonical notice and libffi-license copies; a drift and `npm pack --dry-run`
  check prevents release-only staging from concealing a deficient npm payload.
  `nift.pc` retains no `-lffi` consumer dependency because the objects are in
  `libnift_c.a` and the shared library is self-contained.
- `scripts/audit_no_dynamic_libffi.sh` checks ELF, Mach-O, or PE dependencies for
  the CLI, shared library, Python extension, and Node addon as applicable. It
  rejects archives, unknown formats, missing inspection tools, and failed
  inspections rather than treating them as successful audits.
- `libnift_c.a` has a separate clean-consumer link/run proof because static
  archives do not have a dynamic dependency table.
- `scripts/audit_private_libffi.sh` fail-closes for ELF, Mach-O, and PE. It
  rejects exported/imported/bound libffi symbols, rejects ELF libffi dynamic
  relocations, and requires local definitions of representative code/data
  symbols. A deliberately exported fixture proves the audit rejects bad output.
- PIC compilation emits and includes depfiles. The regression touches and
  restores `src/FfiAbi.h` plus generated `ffi.h` and `ffitarget.h`, and verifies
  each header invalidates `Parser.cpp` without leaving source timestamps changed.
- The focused workflow builds Node/Python on macOS and the documented supported
  Go/C# bindings on Windows. MSYS2 explicitly inherits setup-go/setup-dotnet's
  PATH. Direct Node/Python scripts are documented as Linux/macOS-only.

## Provenance

The vendored source is upstream libffi 3.8.0 at commit
`12ffd1f9dc56fcea79d2f742f424301ae668d663`. Upstream release-asset SHA-256 is
`7da3e2d9a171eb0a038f592ecad3ff2bb2550f3496d87b3b29ad0cf4430c0db4`.
The annotated upstream tag, downloaded release checksum, and extracted release
tree were independently checked; the vendored tree matches except for Nift's
provenance file. `scripts/check_vendored_libffi.py` enforces the recorded tree
digest before builds, including builds from the self-contained Python sdist.
The durable record is `third_party/libffi/NIFT-PROVENANCE.md`.

## Local evidence

Passed locally on Linux x86-64 with clean GCC and Clang static-libffi builds:

```text
make clean
make -j2 CC=gcc CXX=g++ test-libffi-source
make -j2 CC=gcc CXX=g++ test-gate6ar-ffi
make -j2 CC=gcc CXX=g++ test-v45-ffi test-libffi-static-archive embed
make -j2 CC=gcc CXX=g++ node-binding python-binding
make -j2 CC=gcc CXX=g++ test-sanitize
NIFT=.build/nift-sanitize bash tests/gate6ar_ffi.sh
make -j2 CC=gcc CXX=g++ .build/nift-tsan
NIFT=.build/nift-tsan bash tests/gate6ar_ffi.sh

make clean
make -j2 CC=clang CXX=clang++ test-libffi-source
make -j2 CC=clang CXX=clang++ test-gate6ar-ffi
make -j2 CC=clang CXX=clang++ test-v45-ffi test-libffi-static-archive embed
make -j2 CC=clang CXX=clang++ node-binding python-binding
make -j2 CC=clang CXX=clang++ test
make -j2 CC=clang CXX=clang++ test-embed test-bindings
make -j2 CC=clang CXX=clang++ test-build-boundary test-build-boundary-nondestructive

bash packaging/stage-release.sh 4.6.0
```

After the private-symbol, depfile, workflow, and legal-payload repairs, focused
clean GCC and Clang final-tree runs also passed:

```text
make clean
make -j2 CC=<compiler> CXX=<compiler++> embed test-libffi-static-archive
make -j2 CC=<compiler> CXX=<compiler++> test-gate6ar-ffi
make -j2 CC=<compiler> CXX=<compiler++> test-v45-ffi
make CC=<compiler> CXX=<compiler++> node-binding python-binding
make CC=<compiler> CXX=<compiler++> test-node-binding test-python-binding
make CC=<compiler> CXX=<compiler++> test-libffi-private-symbols test-pic-depfiles test-node-package-licenses
```

For both GCC and Clang, the static clean consumer, Gate 6A-R wall, retained v4.5
contract/scalar/memory/package walls, private-symbol negative fixture, PIC
header-invalidation regression, and ordinary npm legal-payload check passed.
Node reported 26 passed and 0 failed; Python reported 23 passed.
The final Linux Go race suite passed, and C# reported 28 passed and 0 failed.

The fail-closed dependency audit passed for both compiler builds' CLI, shared
library, Node addon, and Python extension. The private-symbol audit passed for
the shared library and both direct extensions. Negative checks rejected a
static archive, an unrecognized text file, and an intentionally exported libffi
fixture as intended. GCC ASan/UBSan and TSan-instrumented Gate 6A-R runs passed.

The ABI wall covers all signed/unsigned widths at boundaries, negative signed
values, bool, f32/f64, pointer/null/cstr/buffer, mixed integer widths,
integer/pointer placement, void and every supported return category, generic
arities 0-6, float arities 0-4, callback success and conversion-failure scope,
struct size/layout interpretation, and retained rejection/error surfaces.

The final post-repair release rehearsal produced exactly the CLI, native bundle,
Python sdist and wheel, npm package, NuGet package, and `SHA256SUMS`; all six
artifact checksums verify. The staged CLI, native shared library, wheel
extension, npm addon, and NuGet native library have no dynamic libffi
dependency. The four staged shared artifacts pass the private-symbol audit. The
sdist's vendored-source integrity check passes, clean wheel and npm consumers
load and render successfully, and the staged notices, licenses, provenance,
build script, integrity checker, and `markuppp` sources are present.

## Pending evidence and blockers

- Clang ASan/UBSan cannot configure locally because this host lacks Clang 21's
  `libclang_rt.asan_static.a` and `libclang_rt.asan.a`. GCC ASan/UBSan and the
  GCC TSan-instrumented Gate 6A-R wall pass.
- Hosted `.github/workflows/gate6ar-ffi.yml` Linux GCC/Clang, macOS Clang, and
  Windows MinGW results require a push and therefore remain pending.
- Gate 6A-R and Review Gate 6A remain pending maintainer review even after local
  walls pass.
