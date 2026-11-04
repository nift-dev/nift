# Tooling checkpoint (post-B2, before B3)

Branch `cand/tooling`, based on `c1f43300` (`weekend/r2`, B2 integrated). Scope approved by the owner
(PHASE-B-OWNER-DECISIONS 2026-11-04, tooling checkpoint; coordinator rulings on the implementation
plan). The checkpoint is development and enforcement infrastructure only: no runtime semantics,
admitted program behaviour, test expectation or approved fixture changes. Each component is a
separate commit (or commits) with its own evidence and section here, reviewed one at a time;
combined-tree certification follows. Commit order (approved): 1, 6, 3, 4, 2, 5.

Evidence is local (2 cores, 7 GB) and focused; heavy lanes run on Linode via the coordinator. Gate
result files (tree identity, command, exit status, log sha256) are under the session scratchpad
`tooling/` folder, one file per gate.

## 1. Build-object reuse

### 1.1 Problem

39 recipe lines compiled the whole runtime (`$(RT_VALUE_SOURCES)`, 246 files, plus
`src/RuntimeValue.cpp`) into each runtime unit-test binary in one compiler command (single
threaded, nothing reused): `test-rt-value` alone compiled the runtime 11 times, `test-rt-value-
sanitize` 7 times under ASan/UBSan.

### 1.2 Change

- A test binary compiles only its own source and links the runtime as objects, in the order of
  `RT_VALUE_SOURCES` (the one-command build linked the same inputs in the same order: same link and
  static-initialisation order).
- Default flags (`$(CPPFLAGS) $(CXXFLAGS)`): the in-tree objects `make nift` builds
  (`RT_UNIT_OBJECTS`). The former `-Itests/rt` on those lines never reached a runtime source: no
  quoted include under `src`, `include`, `markuppp` or `minifypp` resolves into `tests/rt` (checked
  for every include spelling in the tree).
- Every other exact configuration has its own object directory, `.build/rt-obj/<config>/`, from
  one template (`RT_OBJ_CONFIG`): `destroy-local` (`-DNIFT_RT_DESTROY_LOCAL=1`, read by
  `value.cpp`), `saturated-slots` (`heap_account.cpp`), `no-cycle-check` (`value_cycle.h`),
  `sourcemap-counter` (`source_map.*`), `san` (`RT_SANITIZE_FLAGS`), `san-pthread` (the same plus
  `-pthread`, kept separate), `san-destroy-local`, `san-sourcemap-counter`,
  `tsan-sourcemap-counter`. Configurations never mix: a binary links only its configuration's
  objects, and the test source itself is still compiled with the binary's own flags.
- Each configuration (and the in-tree, `pic`, `san`, `san-lifetime` and `tsan` production object
  sets, which test binaries also link) has a signature file (`scripts/build/write-signature.sh`)
  holding each compile template's identity (`scripts/build/config-id.sh`): the compiler as named,
  its resolved path, the sha256 of its complete `--version` output, and the exact flags. It is
  rewritten only when that changes, and every object depends on it: a changed compiler, compiler
  version, flag or define rebuilds the objects; an unchanged one never does. Before this,
  `make nift CXXFLAGS=...` followed by a default build reused the other configuration's objects
  (shown with `c1f43300`'s Makefile below).
- Each object also gets a configuration sidecar (`OBJECT.cfg`, written by `compile-object.sh` from
  the compile command it actually ran: the same identity, as a sha256). Every runtime unit-test
  link and the `nift` link first check (`scripts/build/check-config.sh`) that every input object
  carries the linking configuration's identity, and stop otherwise. So the one place where two
  configurations share a directory, the in-tree objects (one configuration at a time, as
  `make nift` always was), can never link a mixture: a concurrent make with other flags on that
  directory makes the link fail instead. The supported way to build another configuration
  alongside is its own directory (`RT_OBJ_CONFIG`). One window remains, stated plainly: between
  the check and the linker reading the objects, another make with other flags could rename a new
  object into place; the certification runner never runs two configurations on one directory.
- **Parallel-safety scope.** Supported: concurrent builds of different configurations, each in its
  own `.build/rt-obj/<config>/` (and the separate `pic`, `san`, `san-lifetime`, `tsan` sets), and
  concurrent builds of one configuration sharing objects (atomic renames). Unsupported:
  concurrent builds of different configurations that share the in-tree object directory (for
  example `make CXXFLAGS=... X` alongside a default `make Y`); the link check turns that into a
  failed link rather than a mixed binary, except in the check-to-link window. `scripts/certify.py`
  never does the unsupported case: every make it starts, the phase-0 prebuild and each target, is
  `make [-k -jN] TARGET...` with no variable on the command line (`run_target`, `cmd = ["make",
  target]`; the prebuild call in `main`), in one inherited environment from which `clean_env()`
  removes `MAKEFLAGS`, `MFLAGS`, `MAKELEVEL` and `MAKEOVERRIDES`, so all its makes share one
  configuration. Targets that rebuild the shared objects under other configurations
  (`test-build-reuse`) or plant sources (`test-rt-isolation`) are in its `EXCLUSIVE` table and run
  alone. The Linode certification harness runs `certify.py` or plain `make TARGET` the same way
  (coordinator); it is not in this repository.
- **Permanent gate (owner):** `make test-build-reuse` runs `check-build-reuse.sh` (about 40 minutes
  on 2 cores) and is in `certify-targets`. Without `clang++`, the compiler row uses a wrapper of
  `g++` with another path and `--version`, which is a compiler-identity change as the signature
  sees it.
- Generated inputs: the tree has no generated sources or headers. The one generated include
  directory is vendored libffi's install tree, rebuilt when its signature changes; objects that
  include its headers depend on them through the compiler's dependency files.
- Header dependencies are the compiler's (`-MMD -MP`) for every object, and for the test source of
  each file-target binary (`$@.d`). There are no generated headers or sources in the tree.
- Objects are compiled atomically (`scripts/build/compile-object.sh`: private temporary object and
  dependency file, renamed into place on success, dependency file first), because
  `scripts/certify.py` runs many `make` processes in one tree at once and these objects are now
  shared between targets.
- Phony recipes that compile (`test-rt-value-sanitize`, `test-rt-vm-sanitize`,
  `test-rt-source-map-sanitize`/`-tsan`, `test-runtime2-cp22-sanitize`) now list their objects as
  prerequisites, so `certify.py`'s phase 0 builds them once.
- **One-time full rebuild (owner-approved):** the first `make nift` after this commit rebuilds every
  in-tree object once (the signature file is new); afterwards it behaves as before. The rebuilt
  objects are byte-identical to `c1f43300`'s (below).

### 1.3 Evidence

All on this host (2 cores, g++ 13.3, clang 18.1). Final component tree: commit `384c6a38`, tree
`fed0a8bd`; gate files under `gates/c1/` (`build-reuse.result`, `nift.result`,
`test-rt-lint.result`, `test-rt-protocol-compile-fail.result`).

- **Production objects unchanged.** After each signature-format change (two one-time rebuilds on
  this branch), all 291 in-tree objects are byte-identical to `c1f43300`'s and `nift` is sha256
  `3062f229...`, the same as `c1f43300` here and on Linode (`b2cert-3c962476`).
- **Old versus new binaries.** `value-unit` built with `c1f43300`'s Makefile (one command) and with
  this one (objects): byte-identical (`a863cf47...`); both print `83 checks, 0 failures`, identical
  output. The ASan `rt-san/value-unit` (old command versus the `rt-obj/san` objects): byte-identical
  too (`8f38491f...`). Identical binaries give identical results by construction; the full lanes
  run on Linode.
- **`scripts/build/check-build-reuse.sh`** on `384c6a38`: 20 checks, all PASS (2,476 s). Tracked set:
  the 247 default runtime objects plus `src/ParserExpression.o` (the one object that includes a
  generated header), each with its sidecar and dependency file, and three binaries (747 files);
  `no-cycle-check` and `saturated-slots` for the configuration checks.
  - deps: touching `src/rt/value_cycle.h` rebuilt exactly the 45 objects whose dependency file names
    it; touching `src/rt/aggregate.cpp` only its object; a further `make` rebuilt and relinked
    nothing; clean and incremental byte-identical (747 files).
  - A to B to A, one row per signature input; in every row B rebuilt exactly the affected objects
    (no A object reused), and the second A rebuilt every object B had built (none of B's reused) and
    reproduced the first A byte for byte (objects, sidecars, dependency files, binaries):

    | Input | B | Objects B rebuilt | Same bytes as A anyway |
    | --- | --- | --- | --- |
    | compiler identity | `CXX=clang++` | 248 (all) | 0 |
    | flags | `-O2` to `-O1` | 248 | 1 |
    | feature define | `-DNIFT_TOOLING_PROBE` (read by no file) | 248 | 248 (rebuilt: the sidecar identity differs) |
    | generated input | libffi's `LIBFFI_CFLAGS` changed: libffi rebuilt, its installed headers regenerated | 1 (`ParserExpression.o`) | 248 |
    | header edit | an inline function appended to `value_cycle.h`, then the original back | the 45 dependents | - |

    The generated-input row failed first (`e712768d`, kept: `build-reuse-e712768d-FAILED-generated`):
    under `-j`, `ParserExpression.o` rebuilt one run late after a libffi rebuild, because make read
    the installed headers' mtimes (named only by the dependency file) before the stamp rule
    reinstalled them. Fixed in `384c6a38` (the headers depend on their stamp); a scratch makefile
    reproduces both behaviours under `-j2`.
  - mixing: a `no-cycle-check` object (with its sidecar) placed in the default directory is refused
    by the link check (`check-config: 1 of 247 objects were not built by this configuration`), never
    linked.
  - parallel: 2 rounds of 5 concurrent `make` processes in one tree over three configurations
    (`no-cycle-check` and `saturated-slots` from clean, the default with 28 shared objects deleted,
    three of the makes sharing those): every exit 0; every object, sidecar and dependency file
    identical to a clean serial build of its own configuration; no temporary file left. (An earlier
    run of the first script version also did 3 rounds of 3 concurrent makes of one configuration:
    all identical.)
  - For the record: `c1f43300`'s Makefile considers `src/rt/value.o` up to date under different
    `CXXFLAGS`: the stale-object hazard closed here.
- **Build timings** (build only, `make -j2`; correctness and performance certification on Linode):

  | Lane | Before (`c1f43300`) | After |
  | --- | --- | --- |
  | One default binary (`value-unit`), objects present | 4 min 26 s (4 min 21 s CPU), every time | 2.5 s |
  | `test-rt-value` binaries (11), after `make nift` | about 48 min CPU (11 x 4 min 21 s; measured one, the others compile the same sources), about 25 min wall at `-j2` | 4 min 36 s wall, 9 min 1 s CPU (the two define configurations, 494 objects, plus 11 links) |
  | The same after touching one runtime source | the same as clean (no reuse) | 10.7 s |
  | The same, nothing changed | no-op | 1.3 s (1.24 s with the link check) |
  | `test-rt-value-sanitize` binaries (7) | about 55 min wall (the recipe compiles its 7 lines one after another; measured one: 7 min 53 s wall, 7 min 46 s CPU) | 8 min 19 s wall: `rt-obj/san` and `san-destroy-local` (494 ASan objects) 7 min 45 s, 7 links 34 s; afterwards links only |

  Each ASan configuration directory is 578 MB here.

### 1.4 Not changed

Test sources, expectations and fixtures; the binaries' link inputs other than the source/object
form; the remaining test rules that compile small, explicit source lists (handle, error-oom,
mutant and probe builds, which have their own defines and already used private objects or a few
files).

## 6. `libnift_c.so` symbol visibility

(Section numbers follow the component numbers; commit order is 1, 6, 3, 4, 2, 5.)

### 6.1 Problem

`libnift_c.so` exported every symbol with default visibility: 5,527 dynamic symbols, 2,355 of them
`nift::rt::` internals, plus the legacy engine, inline and template instantiations, and three
internal `extern "C"` signal handlers named `nift_host_*`. The documented interface
(`docs/handover/CP10-C-ABI-DESIGN.md`: "the shared library exports only the ABI symbols") is the C
ABI of `include/nift/c_abi.h`.

### 6.2 Change

- The allowed export list is built from the documented public embedding API, not from a name
  prefix: `scripts/check_abi_symbols.py --write-map` reads `include/nift/c_abi.h` through the C
  preprocessor, takes every declared function (72), adds the documented exceptions of
  `src/embed/abi-exports-extra.txt` (one: `nift_ffi_callback_i64_trampoline`, the FFI callback
  trampoline, exported before and part of the frozen ABI 1.1 surface of
  `tests/v46_b4_cp8_embedding_abi.sh`), and writes `.build/abi/libnift_c.map`: a GNU ld version
  script naming each of the 73 symbols exactly, everything else `local`. It is an anonymous version
  node: visibility only, no symbol versions, so a consumer resolves exactly as before. The map is
  regenerated whenever the header, the exceptions file or the script changes.
- Only the link of `libnift_c.so` changes (Linux: `-Wl,--version-script=`); no source, header or
  object changes (`include/` stays byte-identical, as `tests/v46_b4_cp8_embedding_abi.sh` requires),
  so the code inside the library and the `nift` executable are untouched.
- `make test-c-abi-symbols` (added to `test-embed`):
  - inventory: read from the built library's dynamic symbol table (`nm -D --defined-only` on
    `libnift_c.so`, the same set as `readelf --dyn-syms` defined entries), not from the linker
    map: the exported dynamic symbols must equal
    `tests/abi/libnift_c.symbols` (checked in for review) exactly, every function declared in
    `c_abi.h` must be exported, and the list must equal the declared functions plus the exceptions
    file; so any extra export and any missing documented symbol fails;
  - negative controls (`tests/abi/inventory_selftest.sh`, throw-away links of the same objects): a
    library without the version script fails (5,451 symbols that are not part of the ABI), and one
    whose map lacks `nift_engine_render_text` fails naming it;
  - external consumer (`tests/abi/external_consumer.sh`, `tests/abi/external_consumer.c`): copies
    `c_abi.h`, the program and the built `libnift_c.so` into a fresh directory outside the
    repository, compiles there with `cc -std=c99 -Werror` against the copied header only, links
    with `-lnift_c` (dynamic: `NEEDED libnift_c.so`, no `nift_*` symbol defined in the
    executable), and runs it with a minimal environment: version, engine bindings, context, render
    with a template, partial, text render, evaluate, execute with captured stdout, the program API
    (compile, run, output, return value, syntax error) and every free function.

### 6.3 Evidence

On this host, commit `eea0e8ba`; gate files under `gates/c6/` (`test-go-binding` and
`test-python-binding` ran on the same content before it was committed).

- Exported dynamic symbols: 5,527 before (the same objects linked without the script), 73 after,
  equal to the inventory. Every `nift_*` symbol exported before is still exported except the three
  internal signal handlers `nift_host_exit_handler`, `nift_host_signal_handler`,
  `nift_host_sigchld` (`src/rt/host/process_signals.cpp`: installed by address with `sigaction` and
  `atexit` inside the library, declared in no public header, referenced by nothing outside that
  file); hiding them changes nothing for the library (installed handlers are function pointers).
- `make test-c-abi-symbols`: inventory PASS (73 = 72 declared + 1 exception), both negative
  controls PASS, external consumer: 37 checks, 0 failures; 34 `nift_*` symbols resolved from the
  library at run time.
- Consumers that load the shared library dynamically: the CP18 C program (`tests/runtime2/cp18/
  program.c`, `-lnift_c`): 1,044 checks, 0 failures; the CP18 Python ctypes test: 70 checks. The Go
  binding (`make test-go-binding`, `go test -race`): ok; it links `libnift_c.a` statically
  (`-Wl,-Bstatic -lnift_c` in the generated `nift.pc`), as do the Python and Node bindings (they
  compile the sources themselves), so the version script does not reach them; the Python binding
  build fails, for a pre-existing reason unrelated to this component: its source list
  `src/rt/host-sources.txt` (unchanged since `c1f43300`) does not name
  `src/rt/handles/table_teardown.cpp`, so the extension does not load (`undefined symbol
  ...HandleTable::shutdown_noexcept`); it never loads `libnift_c.so`. Out of this checkpoint
  (owner): a **release-blocker audit item**, every supported binding (Go, C#, Node, Python) must
  build and pass before release; this one needs `src/rt/handles/table_teardown.cpp` (and a check
  of the whole list against what the C ABI links). The C# binding loads the shared library through P/Invoke of C ABI names
  only; `dotnet` is not installed here: Linode.
- `nift` executable: unchanged (sha256 `3062f229...`); its link is not touched.

### 6.4 Scope: Linux only; remaining before a cross-platform release

- Linux (GNU ld): done here.
- macOS (`libnift_c.dylib`): unchanged; still exports every external symbol. Needs
  `-Wl,-exported_symbols_list` generated from the same list (with the leading underscore), and the
  same inventory test with `nm -gU`.
- Windows (mingw `libnift_c.so` DLL): unchanged; with no `dllexport` anywhere GNU ld exports every
  symbol. Needs a `.def` file generated from the same list (or `--exclude-all-symbols` plus the
  list), and the inventory test with `objdump -p`.
- Both need their own CI runner; neither was built or tested here.

## 3. Test-helper isolation (structural)

### 3.1 Problem

`rt::Storage` befriends the storage-level unit tests' accessor `StorageTestAccess`, defined only by
`tests/rt/storage_access.h`. B2 guarded this with a repository-wide lint, which RECHECK2 bypassed
(N1: `#include "../../tests/rt/storage_access.h"` from a production file and a call of its free
functions, into `nift`; N2: the annotation accepted anywhere, and `markuppp/`, `minifypp/`, nested
`tests` directories and some suffixes unscanned).

### 3.2 Change

- **The production marker.** Every production C++ translation unit is compiled with
  `PRODUCTION_GUARD` (`-include src/build/production_tu.h`): every object of `nift`, `libnift_c`,
  the sanitizer and TSan `nift` builds, and the runtime object sets that test binaries link (the
  in-tree, `pic`, `san`, `san-lifetime`, `tsan` and `rt-obj/*` rules). A test translation unit (a
  source under `tests/`) is never compiled with it. The header defines `NIFT_PRODUCTION_TU` and
  nothing else.
- **In a marked unit, `src/rt/aggregate.h` ends** (after the unchanged class `Storage`) by
  asserting that `StorageTestAccess` is not yet a complete type, then poisoning the name
  (`#pragma GCC poison`, GCC and Clang): any later definition, declaration or use of the name,
  however spelt (token pasting, the `%:%:` digraph, a line splice) and wherever it comes from (the
  unit's own text or any header, of any suffix, under any directory), is a compile error. The
  assertion closes the one ordering route the poison alone leaves: a definition before
  `aggregate.h` whose member template names `Storage` only when instantiated after it (control 2
  below compiles without the assertion).
- **No macro is involved.** An earlier design (the friend named through a macro defined before the
  poison) was rejected after a probe: Clang accepts `#pragma push_macro`/`pop_macro` restoring the
  macro after its `#undef`, which reopened the name (GCC refuses). Force-including `aggregate.h`
  itself was rejected too: it changed the code generated for units that do not include it
  (template instantiation order in `Hooks.o`, `Parser.o`, `Minify.o`). The marker changes no
  object (below).
- **`Storage` is the same in test and production units**: the class is unchanged; the
  production-only tail is a `static_assert` and a pragma after it (B2's ODR objection to a
  test-only friend does not apply).
- **`tests/rt/storage_access.h`** refuses to compile (`#error`) where `NIFT_PRODUCTION_TU` is
  defined; that is the N1 diagnostic. Undefining the marker first gains nothing, since the header
  itself names the poisoned identifier.
- **Dependency check per object** (`compile-object.sh --production`, every production rule, C
  objects too): after the compile, every path in the compiler's dependency list (the whole include
  closure, resolved by the compiler, so no spelling, macro or relative path hides one) is resolved
  against the repository root, and a path under the top-level `tests/` fails the object. This
  catches any test header, not only `storage_access.h`.
- **Gate `test-production-deps`** (`scripts/build/check-production-deps.sh`): takes every production
  compile command from `make -n -B` of `nift`, `libnift_c.so`, `.build/nift-sanitize`,
  `.build/nift-sanitize-lifetime` and `.build/nift-tsan`, runs each with `-M` (system headers
  included) and fails on any dependency in `tests/`.
- **Gate `test-rt-isolation`** (`tests/rt/isolation/run.sh`): plants each reproducer into a real
  production source, builds that object with its real production rule (`make OBJECT`, the command
  `make nift` uses), requires failure with the expected diagnostic, restores the tree byte for
  byte, and requires a harmless control plant to build. Both gates are in `certify-targets`.
- The B2 lint (`rt-lint.py --test-access`) stays as defence in depth; its hardening is component
  5. The poison and assertion lines in `aggregate.h` carry the existing annotation until then.

### 3.3 Evidence

Commit `7d3bf612` (code) and `68ce8e1a` (the ordering case moved into a unit that includes
`aggregate.h`; the first run, kept as `test-rt-isolation-7d3bf612-FAILED-order-case.log`, planted it
in `src/Hooks.cpp`, which never includes `aggregate.h`, so it reached nothing and built). Gate files
under `gates/c3/`.

- Production unchanged: with the guard, all 291 in-tree objects are byte-identical to
  `c1f43300`'s, `nift` is `3062f229...`, and `libnift_c.so` is byte-identical to component 6's.
  Test binaries that include `storage_access.h` still build and pass (`value-unit` 83 checks,
  `value-regress` 280).
- `test-rt-isolation`, every case does not build:

  | Case | Plant (RECHECK2 reproducer) | Object | Diagnostic |
  | --- | --- | --- | --- |
  | N1 | `#include "../../tests/rt/storage_access.h"` and a call of `array_append` in `push_value` (`rc2/m-include`) | `src/rt/vm_ops.o` | `#error ... test-only header` |
  | N1, another test header | `#include "../tests/rt/rt_test.h"` | `src/Hooks.o` | `depends on the test tree: tests/rt/rt_test.h` |
  | N2a | a definition with `rt-lint: test-access-decl` in `src/Hooks.cpp` (`rc2/m-outside` probe C) | `src/Hooks.o` | poisoned |
  | N2b | a definition in `markuppp/src/Markup.cpp` (probe B) | `markuppp/src/Markup.o` | poisoned |
  | N2b, header | a definition in a new header under `minifypp/include`, included by `Minify.cpp` | `minifypp/src/Minify.o` | poisoned |
  | N2c | a definition in `src/rt/tests/helper.h` (a nested `tests` directory) | `src/rt/vm_ops.o` | poisoned |
  | N2d | the definition in `.hxx` and `.def` files, included | `src/rt/vm_ops.o` | poisoned |
  | R3 | the name built by `##`, by `%:%:`, and split by a line splice | `src/rt/vm_ops.o` | poisoned |
  | ordering | a definition before the includes, used after them through a template | `src/rt/vm_ops.o` | static assertion |
  | control | a harmless function in the same file | `src/rt/vm_ops.o` | builds |

- Load-bearing controls (`logs/c3/load-bearing-controls.txt`): the N2b plant compiles without the
  guard and fails with it; the ordering plant compiles with an `aggregate.h` lacking the assertion
  and fails with the real one.
- `test-production-deps`: PASS, 1,468 production compile commands (in-tree, `pic`, `san`,
  `san-lifetime`, `tsan`; C and C++), no dependency in `tests/` (300 s). Negative control
  (`logs/c3/production-deps-negative.log`): with `#include "../tests/rt/rt_test.h"` planted in
  `src/Hooks.cpp` it fails naming `src/Hooks.cpp -> tests/rt/rt_test.h`; the tree was restored.

### 3.4 Limits, stated plainly

- A unit that never includes `aggregate.h` but writes its own definition of class `Storage` (a
  forged header: the g20 class of RECHECK2, an ODR violation that needs no diagnostic) is outside
  what any header can stop; component 4's link-time symbol allowlist covers it for the static
  `nift` link, and component 6 for the shared library.
- Binding builds outside the Makefile (`bindings/python` `setup.py`/`build.sh`,
  `bindings/node/build.sh`) compile the production sources with their own command lines, without
  the guard. Their artefacts are not `nift` or `libnift_c`; giving them the guard is a binding-build
  item (with the release audit's binding source-list item).

## Carried items (release audit)

- **Release blocker (owner, 2026-11-04):** every supported binding must build before release. Found
  here: the Python binding's source list `src/rt/host-sources.txt` lacks
  `src/rt/handles/table_teardown.cpp` (section 6.3); pre-existing, unchanged since `c1f43300`.
- macOS and Windows export lists for the embedding library (section 6.4).
- Binding builds outside the Makefile compile production sources without `PRODUCTION_GUARD`
  (section 3.4).
