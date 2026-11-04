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
  alongside is its own directory (`RT_OBJ_CONFIG`). (Review F7, corrected: the check now runs
  on every link and archive of a signed set, and the sidecar holds the object's sha256, so a
  sidecar and an object from two different compiles are refused as a pair; see "Review fixes".)
  One window remains, stated plainly: between the check and the linker reading the objects,
  another make with other flags could rename a new object and its sidecar into place; the
  certification runner never runs two configurations on one directory.
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
- **In a marked unit, `src/rt/aggregate.h` ends** (since component 2, this tail is at the end of
  `src/rt/build.h`, which `aggregate.h` includes last, so it still follows every use of the name
  whichever header comes first) (after the unchanged class `Storage`) by
  asserting that `StorageTestAccess` is not yet a complete type, then poisoning the name
  (`#pragma GCC poison`, GCC and Clang): any later definition, declaration or use of the name,
  spelt directly or built after the poison (token pasting, the `%:%:` digraph, a line splice) and
  wherever it comes from (the unit's own text or any header, of any suffix, under any directory),
  is a compile error. (Re-review N5, corrected from "however spelt": a macro defined *before* the
  poison, `#define NIFT_TA StorageTestAccess` or `#define StorageTestAccess Other`, expands past
  it, as GCC documents; neither the poison nor the assertion stops it. The lint catches both, at
  the `StorageTestAccess` token on the `#define` line; recorded as a limit of the compile-time
  layer, "Re-review fixes".) The
  assertion closes the one ordering route the poison alone leaves: a definition before
  `aggregate.h` whose member template names `Storage` only when instantiated after it (control 2
  below compiles without the assertion).
- **The friend is not named through a macro.** (Review F4, corrected: the guard itself is the
  macro `NIFT_PRODUCTION_TU`; undefining it before `aggregate.h` removed the poison and the
  assertion, and is now refused by `compile-object.sh --production`, see "Review fixes".) An
  earlier design (the friend named through a macro defined before the poison) was rejected after a
  probe: Clang accepts `#pragma push_macro`/`pop_macro` restoring the
  macro after its `#undef`, which reopened the name (GCC refuses). Force-including `aggregate.h`
  itself was rejected too: it changed the code generated for units that do not include it
  (template instantiation order in `Hooks.o`, `Parser.o`, `Minify.o`). The marker changes no
  object (below).
- **`Storage` is the same in test and production units**: the class is unchanged; the
  production-only tail is a `static_assert` and a pragma after it (B2's ODR objection to a
  test-only friend does not apply).
- **`tests/rt/storage_access.h`** refuses to compile (`#error`) where `NIFT_PRODUCTION_TU` is
  defined; that is the N1 diagnostic. Undefining the marker after `aggregate.h` gains nothing,
  since the header itself names the poisoned identifier; undefining it before (review F4) is
  refused at the object (see "Review fixes").
- **Dependency check per object** (`compile-object.sh --production`, every production rule, C
  objects too): after the compile, every path in the compiler's dependency list (`-MD` since review
  F6: system headers and headers behind `#pragma GCC system_header` included) (the whole include
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
  5 (done: section 5, `--boundary`). The poison and assertion lines in `aggregate.h` carry the existing annotation until then.

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
- rt-lint limits: computed includes, non-paste macro-built names, the alias form, unscanned
  non-production directories (section 5.3).

## 4. SortKey and sort declarations

### 4.1 N3: the byte-copy route (closed)

RECHECK2 N3: `SortKey` was trivially copyable, so `__builtin_bit_cast(SortKey, ...)` (C++20
`std::bit_cast`) made a key in well-defined code, and, as an implicit-lifetime type, a key "existed"
in `malloc`'d or `char` storage under C++20 rules. `SortKey` (`src/rt/mutation.h`) now has a
user-provided private destructor and deleted copy (so no implicit move):

- not trivially copyable: the bit cast does not compile (`bypass_sort_key_bitcast.cpp`:
  "`__builtin_bit_cast` destination type `SortKey` is not trivially copyable");
- no copy or move: a key cannot be copied out of a forged lvalue into a real object
  (`bypass_sort_key_copy.cpp`: "use of deleted function `SortKey(const SortKey&)`");
- not implicit-lifetime: no `SortKey` object exists in `malloc`'d or `char` storage, so using one
  from there is undefined behaviour again;
- `absent.cpp` asserts: not trivially copyable, not copy or move constructible, not trivially
  destructible, not destructible outside its friends.
- The two new cases discriminate: both compile against `c1f43300`'s `mutation.h`, and `absent.cpp`'s
  new assertion fails there (`logs/c4-discrimination.txt`). Unlike RECHECK2 I1, they match the
  specific diagnostic ("not trivially copyable", "deleted function ... SortKey"), not any error
  that names `SortKey`.
- Cost: the key is only ever bound to `const SortKey&`; `nift` objects are byte-identical to
  `c1f43300`'s except `collection_plain_sort.o`, where one equality compare before `je`, at the
  function's entry (the incomparable-check setup, not the sort loop), has its register operands
  swapped (`cmp %rcx,%rax` versus `cmp %rax,%rcx`): same instruction count, the same meaning. `nift`
  sha256 is therefore `5f511e2c...` from this component on.

### 4.2 By-value (prvalue) keys: not adopted

The coordinator's ruling allowed passing the key by value as a prvalue (which would also refuse
the `malloc`'d-lvalue and inactive-union-member forms, since a forged lvalue cannot initialise a
by-value parameter without a copy) if the hot-loop instructions and callgrind stayed the same. It
fails earlier, for a structural reason: only a friend of `SortKey` can create a prvalue key, and
B2's key passes through `mutation::SortController` (a struct in `rt/sort_controller.h`) and through
anonymous-namespace helpers of `collection_sort.cpp` (`receiver_items`, `save`, `restore`,
`next`). Making `SortController` a friend reopens the f22 route (a unit that does not include
`sort_controller.h` defines its own `SortController` and makes keys with no undefined behaviour at
all); anonymous-namespace functions cannot be befriended from a header; and removing
`SortController` changes B2's approved compile-fail fixtures `bypass_sort_paste.cpp` and
`bypass_sort_plain.cpp` (their expected diagnostics name `SortController::items`). So only N3 is
applied, as the ruling's fallback says. Remaining deliberate-source routes, recorded:

- `*static_cast<const SortKey*>(std::malloc(1))` bound to the `const SortKey&` parameter: compiles;
  undefined behaviour (no `SortKey` object there).
- a reference to an inactive union member of type `SortKey` (the union needs a user-provided
  destructor now): compiles; it binds a reference to storage where no `SortKey` lives
  ([basic.life] permits forming such a reference; the commits never access the key).
- Coverage: the out-of-line commits (`sort_storage`, `commit_permutation`) are also covered by the
  link-time allowlist below; `sort_items` is inline, so a forged call to it leaves no symbol, and
  only the lint (`SortKey`, `sort_items` tokens; extended to every production root by component 5)
  sees it.

### 4.3 N4: the duplicate-declaration route (not closable by the language; refused at link time)

RECHECK2 N4 (rc2/forge/g20): a unit that does not include `rt/mutation.h` declares its own public
`SortKey` and `Protocol::sort_storage` with the real mangled name and calls the real out-of-line
commit. That is an ODR violation of `SortKey` and `Protocol`: ill-formed, no diagnostic required
([basic.def.odr]); no compiler refuses it and no header can, because the unit never sees the real
declarations (`test-rt-sort-forgery` case 0 compiles it). So it is closed where the unit meets the
real code, at link time:

- **Static links** (`nift`, `libnift_c.a`, `libnift_c.so`): `scripts/build/check-symbol-refs.sh`
  lists every object's undefined references to the restricted symbols (`rt::Storage`'s members,
  `Protocol::sort_storage`, `sort_items`, `commit_permutation`, `commit_array`) and fails the link
  on any (object, symbol) pair outside the reviewed allowlist `tests/abi/restricted-symbol-refs.txt`
  (15 pairs today: the protocol's commit layer in `mutation.o` and `mutation_collection.o`, the two
  sort files' commits, and the protocol's inline array commits expanded in `cell.o` and `vm_ops.o`).
  A new reference, forged or legitimate, needs review. Its granularity is the (object, symbol)
  pair (review F5): within an object already allowed a symbol it cannot tell one use from
  another (a builder's from an in-place or forged one), so it adds nothing inside the 20-odd
  builder users; there the compile-time key and the lint are the guards. Since review F5 it fails
  closed: no objects, an `nm` failure or diagnostic, or an empty listing of an LTO object fail the
  link.
- **Dynamic links**: `libnift_c.so` does not export these symbols (component 6), so a forged unit
  linked against it has an undefined reference.
- `make test-rt-sort-forgery` (`tests/rt/sort_forgery/run.sh`, in `certify-targets`, exclusive):
  case 0, the forged unit compiles (the language does not refuse it); case 1, planted into
  `src/Hooks.cpp`, `make nift` fails at the allowlist (`src/Hooks.o
  nift::rt::mutation::Protocol::sort_storage`); case 2, linked against `libnift_c.so` it does not
  link (undefined reference to `Protocol::sort_storage`); control, against the same objects linked
  without the version script it links (the route RECHECK2 demonstrated).
- Not covered: a forged unit that reimplements a commit's body instead of calling it (for example
  writing `heap_as<ArrayObject>()->items` directly); that is the raw-layout route, a known boundary
  (Carried items) with lint coverage only. The g05 route (defining one of the three friend
  functions with its exact signature) already fails the static link ("multiple definition").

### 4.4 Gates

Commit `f0bd7214`; gate files under `gates/c4/`: `test-rt-protocol-compile-fail` (absent.cpp, 13
bypass cases, control), `test-rt-lint`, `test-rt-sort-forgery`, `test-rt-sort-protection-mutants`
(S1, S2, S4, S5 caught; wording control), `test-rt-mutation-protocol` (191), `test-runtime2-collections`
(279), `test-rt-refrep` (116 checks; production checks PASS): all exit 0.

## 2. Compile-time builder type (construction, publication, mutation)

### 2.1 The contract

- **Construction** of a fresh aggregate goes through a builder (`src/rt/build.h`, included at the end
  of `rt/aggregate.h`, so every unit that sees the aggregates sees the complete builder
  definitions): `build::Array` (element by element, the heap tally per push, as the former
  `array_push`), `build::ArrayBulk` (the natives' bulk form: one raw-storage hand-out at
  construction, plain appends, the tally when asked; what `natives::ArrayBuilder` always did, now
  a thin wrapper), `build::Object`, `build::Struct`. A builder is move-only, cannot be assigned, has
  no constructor from an existing `Value` (`build::Array::shallow_clone(source)` makes a new array
  from a source's elements, as `array_shallow_clone` did), and offers reads of its own value only
  (`size()`, `get()`), never a writable reference into its storage. (Review F10, corrected:
  `build::Object::get(key)` returns a `const Value*` into the builder's own object, used by
  `obj_merge`'s `shallow_object` merge and two natives to test a key; it grants no write, and is
  valid only until the builder's next `set` or its `publish`, as for any pointer from
  `object_get`; no caller keeps it past the statement.)
- **Publication** is `std::move(builder).publish()` (rvalue-qualified, so it cannot be written by
  accident on an lvalue): the value moves out and the builder is left holding nothing (a null
  `Value`; `ArrayBulk` also a null storage pointer). From then on no use of the builder, through
  the builder itself, a moved-from copy, a reference or a pointer to it, before or after any number
  of moves, can reach the published value: **that is guaranteed by the builder being empty, not by
  a compile error** (C++ cannot reject a use after a move). Checked builds (sanitizers,
  `NIFT_RT_CHECKED_REFS`: the condition of `aggregate_store.h`, mirrored in `build.h` and asserted
  equal in `aggregate.cpp`) abort on such a use; release builds do not check (it cost 10 to 20
  instructions per literal or native result, 2.6) and a use dereferences the builder's own null
  value. Publication never allocates (tested under injection).
- **Mutation** of a published value only through `mutation::Protocol` (unchanged).
- **The primitives** are the former free construction functions, out of line and unchanged except
  that the stored value is taken by rvalue reference: every entry point (a builder operation, a
  key forwarder, a test shim) still takes it by value first, so the caller's argument is built
  exactly where the former by-value parameter was and no `Value` is moved more than before (the
  first version forwarded by value twice: up to +88 instructions per JSON literal, 2.6). They are
  private members of `rt::Construction`, reachable only by the builders, the storage-level tests'
  accessor (2.5), and an `InPlaceKey` holder (2.4, inline forwarders: the key costs nothing).

### 2.2 Guarantee layers

| Layer | What it guarantees | Where |
| --- | --- | --- |
| compile time | no free construction function exists; the primitives are private; a builder cannot be copied, assigned or made from an existing `Value`; `publish()` needs an rvalue; the key cannot be constructed, copied or byte-copied outside its owners (a reference to one can be forged, and explicit instantiation reaches the primitives: 2.4) | `absent.cpp` (structural, SFINAE), eleven compile-fail cases (2.5, review fixes) |
| structural | a published or moved-from builder holds no reference to anything | `build.h`; `test-rt-build` (refcount 1 after publication, observed contents) |
| link time | only the reviewed objects reference the primitives, so a unit outside them forging `Construction`, a builder, an owner or the key does not link `nift`/`libnift_c`; inside an object allowed a primitive (every builder user is allowed `Construction::array_push`, for example) the (object, symbol) check cannot tell a builder use from an in-place or forged one (review F5) | `tests/abi/restricted-symbol-refs.txt` (34 construction pairs added, 49 in all), component 4's check |
| run time | `check_fresh`: checked builds abort if a construction receiver is shared, immutable or sort-protected (both tiers); checked builds abort on a spent builder | `aggregate_store.h`, `build.h`; the sanitizer lanes (Linode) |
| lint | an in-place site keeps its `rt-lint: storage-ok <label>` annotation; builder sites need none (no construction token remains) | `tools/rt-lint.py`: any `Construction` token is a construction reference (review F1, corrected: the B2 name list had missed the six renamed primitives); each annotation pinned by the line it covers (section 5) |

### 2.3 Migration, site by site

Every production construction site of `1e23b13e` (79 references in 25 files; the 95 of the B2
estimate counted declarations and the implementing files). Categories (coordinator): **(a)** private
construction, unreachable by anyone else until published: a builder; **(b)** construction with an
intermediate reference (another reference, register, cell, callback argument or snapshot could see
the value before it is complete): reasoned individually; **(c)** incremental construction across VM
instructions, callbacks or resumable native state: the bounded in-place exceptions (2.4). For every
(a) site the four properties are the same and are stated once: **ownership** (the builder is a
local of the same function as the former local `Value`, destroyed at the same scope exit, so a
partial value is released at the same point on every early return and on unwinding), **reference
identity** (the same elements are stored, by copy or move exactly as before, and the published
value is the same new object), **allocation sequence** (the builder's constructor is the same
`make_*` call at the same statement, each operation is the same primitive, `publish()` allocates
nothing), and **observable behaviour** (unchanged: the value first becomes observable at the same
statement, the assignment of the published value). The first-observable column names that
statement; it is the same statement as before in every row.

| # | Site (new) | Old code | New form | Cat. | First observable (old = new) | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `vm_report.cpp:172` `Vm::cold` NewArray | `out = make_array(c); array_push(out, R[b+k])` | `build::Array array(c); array.push(...); out = publish` | a | `R[a] = out` after the switch | `out` is `cold()`'s local |
| 2 | `vm_report.cpp:195` `Vm::cold` ObjectSet | `object_set(R[a], key, v)` | `Construction::object_set(key, R[a], ...)` | c | the instruction that consumes `R[a]` after the last ObjectSet | 2.4 E1 |
| 3 | `vm_struct.cpp:33` `Vm::struct_op` StructAdd | `struct_add_field(R[a], f, v)` | `Construction::struct_add_field(key, R[a], ...)` | c | after the last StructAdd | 2.4 E2 |
| 4 | `vm_iter.cpp:79` collection snapshot for `@for` | `Value array = make_array(n); array_push(array, items[k])` | `build::Array array(n); push; source = publish` | a | `source = ...` (the loop's own register) | stack order kept (reverse) |
| 5 | `vm_iter.cpp:151` `op_object_keys` (map) | `Value keys = make_array(n/2); array_push` | `build::Array keys(n/2); push; R[a] = publish` | a | `R[a] = ...` | |
| 6 | `vm_iter.cpp:158` `op_object_keys` (object; FOR mutant M6 form too) | same | same | a | `R[a] = ...` | |
| 7 | `vm_coro_parallel.cpp:56` task seeds | `Value seeds = make_array(g+argc); array_push x3 loops` | `build::Array seed_builder(g+argc); push; seeds = publish` | a | `graph.encode(seeds, ...)` | |
| 8 | `parallel_task_start.cpp:53` task seeds | `Value seeds = make_array(); array_push` | builder; `seeds = publish` | a | `encode(seeds, ...)` | |
| 9 | `isolate_task.cpp:129` transfer root | `Value root = make_array(n); array_push` | builder; `root = publish` | a | `graph.encode(root, ...)` | |
| 10 | `isolate_snapshot.cpp:40` constants array | `make_array(total)`, `array_build_storage`, `items.push_back`, `clear_may_hold_identity` | `build::ArrayBulk(total)`, `append`, `holds_no_reference()`, publish | a | `encode(constants, ...)` | the same raw storage, no tally, the same summary bit |
| 11 | `mutation.cpp:256` `splice_remove` removed items | `Value cut = make_array(count); array_push` | builder; `*removed = publish` | a | `*removed = ...` | |
| 12 | `mutation.cpp:301` `splice` removed items | `Value cut = make_array(count)` ... `array_push(cut, ...)` ... `*removed = cut` | builder ... `if (removed) *removed = publish` | a | `*removed = ...` | without `removed`, freed at return as before |
| 13 | `aggregate.cpp:303` `Storage::struct_set` (REFSEM mutant R2 only) | `make_struct; struct_add_field; instance = copy` | `build::Struct`; `instance = publish` | a | `instance = ...` | mutant-only code |
| 14 | `compiler_collection.cpp:65` sort_by directions | `Value directions = make_array(n); array_push` | builder; `directions = publish` | a | `make_immutable`, then the constant pool | |
| 15 | `session_invocation.cpp:15` invocation `args` | `make_array(n); array_push`; ARGSCMD mutant 2: `array_build_storage(args).pop_back()` | builder; mutant: `drop_last()`; `args = publish` | a | the binding `install` | |
| 16 | `cell.cpp:90,92` `cell_read` cache fields | `array_build_set(owner->value, 5/6, ...)` | `Construction::array_set(key, owner->value, ...)` | b | never user-visible (the cell's private descriptor) | 2.4 E8 |
| 17 | `cell.cpp:114` `cell_cached_read` descriptor copy | `Value d = array_shallow_clone(src); array_build_set(d, 0, x); make_cell(d)` | `build::Array::shallow_clone(src); set(0, x); make_cell(publish)` | a | `make_cell(...)` (and never user-visible) | |
| 18 | `copy_graph.cpp:147-170` `GraphCopy::fill` (array, object, struct, map, set, stack) | `array_push(copy, mapped(item))` etc. on the shells | `Construction::...(key_, copy, ...)` | b | the parent's fill (post-order) or the result | 2.4 E6 |
| 19 | `copy_graph.cpp:138` `fill_shallow` (REFSEM mutant R9 only) | `struct_add_field(copy, ...)` | `Construction::struct_add_field(key_, ...)` | b | mutant-only | 2.4 E6 |
| 20 | `copy_graph.cpp:195` `shallow_copy` struct | `make_struct; struct_add_field; out = copy` | `build::Struct`; `out = publish` | a | `out = ...` | |
| 21 | `copy_graph.cpp:210,214` `shallow_copy` array/object | `copy = array ? make_array(n) : make_object(); array_push / object_set` | `build::Array` or `build::Object`; `out = publish` | a | `out = ...` | the same allocation per kind |
| 22 | `copy_graph.cpp:251,256` `clone_tree` (`clone_constant`, LoadKFresh) | `Value copy = make_array(n); array_push(copy, clone_tree(item)); return copy` | builder; `push(clone_tree(item))`; `return publish` | a | the caller's push, finally `out` | child cloned before the push, as before |
| 23 | `legacy_bridge.cpp:43` `object_from_legacy` | `out = make_object(); ... object_set(out, ...)`; failures return with the partial object in `out` | builder; `out = Value()` right after it (the old release point of `out`'s previous value); `out = publish` on success and before each failure return | a | `from_legacy`'s caller, after return | the partial value reaches `out` on every return, as before (review F10, corrected: on an exception path, `bad_alloc` in a push, `out` is now null where it held the partial value; rows 24, 35, 36 and 37 likewise; `from_legacy` has no caller in `src/` outside the file, and no caller reads `out` after an exception) |
| 24 | `legacy_bridge.cpp:78` `convert_from` array | `out = make_array(n); array_push(out, v)`; failure returns with the partial array | the same pattern as 23 | a | after return | |
| 25 | `natives/support.cpp` `ArrayBuilder` (every array-returning native: slice, split, keys, values, entries, map of json arrays, ...) | `make_array(n)` + `array_build_storage` once; `items_->push_back`; stride `heap_retally`; `finish`: tally, move | `build::ArrayBulk`: the same calls in the same order | a | `finish()`'s return | |
| 26 | `natives/object_methods.cpp:36` `listing` entries pair | `Value pair = make_object(); object_set x2; result.push(pair)` | `build::Object pair; set x2; result.push(publish)` | a | `result.push(...)` | |
| 27 | `natives/object_methods.cpp:47` `shallow_object` | returned a `Value` that callers kept filling | returns the `build::Object` (moved); callers publish | a | the caller's publish | |
| 28 | `natives/object_methods.cpp:58` `merge_deep` | `Value merged = shallow_object(left); object_get(merged, k); object_set(merged, ...)` | `build::Object merged`; `merged.get(k)`; `merged.set(...)`; `result = publish` | a | `result = ...` after the loop | the read of `existing` and the copy-out `inner` unchanged; recursion takes `inner`, never the builder |
| 29 | `natives/object_methods.cpp:191` `obj_merge` | `Value result = shallow_object(args[0]); object_set(...)` | builder; `out = publish` | a | `out = ...` | `args[1]` may be the receiver: read only |
| 30 | `natives/object_methods.cpp:186` `obj_merge` (REFSEM mutant R-B5 only: merge mutates its receiver) | `Value result = args[0]; object_set(result, ...)` | `Construction::object_set(key, result, ...)` | b | mutant-only | 2.4 E9 |
| 31 | `natives/object_methods.cpp:212` `obj_pick` | `Value result = make_object(); object_get/object_set` | builder `get`/`set`; publish | a | `out = ...` | |
| 32 | `natives/object_methods.cpp:227` `obj_omit` | same | same | a | `out = ...` | |
| 33 | `natives/array_methods.cpp:291` `from_entries` | `Value result = make_object(); object_get; object_set(result, name, *value)` | builder; `out = publish` | a | `out = ...` | `entry`, `key`, `value` point into the argument, not the result |
| 34 | `natives/json_methods.cpp:38` `from_document` object | `Value object = make_object(); object_set` | builder; `out = publish` | a | `out = ...` | failure returns drop the local, as before |
| 35 | `natives/host_env.cpp:84` `host_env` | `out = make_object(); ... object_set(out, ...)`; the budget failure returns with the partial object in `out` | builder; `out = Value()`; `out = publish` before the failure return and at the end | a | the VM's call site, after return (`out` is `builtin()`'s local) | as 23 |
| 36 | `natives/host_fs_internal.h:139` stat-object result | `out = make_object(); object_set x3` | builder; `out = Value()`; `out = publish` | a | after return | no failure path between |
| 37 | `natives/host_fs_nav.cpp:54,85` `ls` results | `out = make_array(n); array_push` | builder; `out = Value()`; `out = publish` (before `finish_host`, as before) | a | after return | |
| 38 | `collection_helpers.cpp:41-45` `append_group` | `object_set(object, k, make_array())`, `object_build_slot`, `array_push(*group, item)` | `Construction::...(bk, ...)` | c | the native's last phase (the result slot) | 2.4 E3 |
| 39 | `collection_helpers.cpp:59-112` `consume` (map, filter, sort_by keys, unique_by, index_by, count_by, group_by_each) | `array_push(s[Result] / s[Keys], ...)`, `object_set(s[Result], ...)` | `Construction::...(bk, ...)` | c | the native's last phase | 2.4 E3 |
| 40 | `collection_helpers.cpp:117` `group_by_each` `seen` | `Value seen = make_object(); object_get/object_set` | `build::Object seen` (never published) | a | never (a local set) | |
| 41 | `collection_helpers.cpp:134,135,158` `sorted_keys` | `array_push(s[HashTable], i)`, `array_build_storage`, `std::sort` over it, `array_push(s[Result], ...)` | `Construction::...(bk, ...)` | b/c | never (scratch) / the last phase (result) | 2.4 E3: a raw storage reference is held across `std::sort` |
| 42 | `collection_helpers.cpp:195,197` partition result | `object_set(s[Result], "matched"/"unmatched", make_array())` | `Construction::object_set(key, ...)` | c | the last phase | 2.4 E3 |
| 43 | `collection_sort.cpp:116` keep-alive | `array_push(s[KeepAlive], value)` | `Construction::array_push(build_key, ...)` | c | never (scratch held by the native's state) | 2.4 E4 |
| 44 | `collection_plain_sort.cpp:25,27` order scratch | `array_push(s[HashTable], i)`, `array_build_storage`, `std::sort` | `Construction::...(build_key, ...)` | b | never (scratch) | 2.4 E5 |
| 45 | `vm_keyed.cpp:68` `op_keyed_mutate` (REFSEM mutant R14 only) | `map_build_set(c, k, v)` before the refusal | `Construction::map_set(key, c, ...)` | b | mutant-only (a deliberate bypass on a published receiver) | 2.4 E9 |
| 46 | `vm_ops.cpp:301` `set_index` (REFSEM mutant M-B2 only) | `array_build_set(object, i, value)` | `Construction::array_set(key, object, ...)` | b | mutant-only (as 45) | 2.4 E9 |

Rows 16, 18, 19, 30, 41, 44, 45, 46 are (b): each keeps the in-place form, reasoned in 2.4. No (a)
site needed a change of ownership, publication point or allocation behaviour; none of its output
can differ.

### 2.4 The bounded in-place exceptions

Each exception holds the key in its named owner (the only functions that can construct one: a
`friend` of `build::InPlaceKey`, `build.h`; for the VM, the members `Vm::cold` and
`Vm::struct_op` through `VmInPlace`, `rt/vm_in_place.h`, not the whole class, since review F3;
the mutant-only owners are friends only in their mutant builds), and each production use is
listed here. Guarantees, separately: **compile time**: only the owner can construct the key
(`absent.cpp`, `build_key_forge.cpp`, `build_key_bitcast.cpp`, `build_key_copy.cpp`,
`build_key_vm_member.cpp`, `build_key_mutant_owner.cpp`). What it does not refuse (review F3), the
same deliberate-source class as `mutation::SortKey`'s (4.1, 4.2): a reference to a key forged by
a cast (`*reinterpret_cast<const InPlaceKey*>(&c)`, review `x/k1_cast.cpp`) or as an inactive union
member (`x/k2_union.cpp`), and the explicit-instantiation access rule ([temp.explicit]), which
reaches the private primitives, and `Storage`'s commits, with no key at all (`x/k3_inst.cpp`,
`x/k4_storage.cpp`; B2 recorded it as INFO for `Storage` and `Protocol`); each is written
deliberately in source and compiles under g++ and clang++; which layer catches which route is
in the table of "Re-review fixes" (re-review N2, corrected: an asm label or mangled name names no
`Construction` or `Storage` token; the lint now flags any mangled `nift::rt` name instead), and
checked builds' `check_fresh` stops a write to a published receiver whatever the route; **link time**: only the listed objects reference the
primitives behind it; **run time**: `check_fresh` (checked builds) aborts if the receiver is
shared, immutable or sort-protected; **lint**: the site keeps its `rt-lint: storage-ok` annotation.
It is a structural narrowing of who may construct in place, not a builder proof of freshness:
freshness at these sites rests, as before, on the owner's invariant, `check_fresh` and the tests
named.

| Id | Owner (holds the key) | Site | Why no builder | Publication boundary | Regression coverage (cannot modify a published receiver) |
| --- | --- | --- | --- | --- | --- |
| E1 | `Vm::cold` | `vm_report.cpp:195` ObjectSet | the object is built in a VM register across instructions (`NewObject`, then one `ObjectSet` per key, values computed in between); a builder cannot live in a register | the instruction after the last `ObjectSet` that reads `R[a]` (the compiler emits the literal into a register no binding names until it is complete) | refsem `b2_literal_fresh_*`, session `session_literal_fresh_statement`, collections `same_object_literal`; refsem mutant R-B7 (fresh literals); `check_fresh` in the sanitizer lanes |
| E2 | `Vm::struct_op` | `vm_struct.cpp:33` StructAdd | as E1, for a struct (`NewStruct`, then `StructAdd` per field) | after the last `StructAdd` | the struct suites (refsem `semantics-structs`, `test-rt-vm`), `check_fresh` |
| E3 | `collection_step` | `collection_helpers.cpp` (rows 38, 39, 41, 42) | the result, keys and hash-table slots of a resumable native are built across callback suspensions (one element per resume) and, for `sort_by`, sorted through a raw storage reference; no builder survives a suspension | the native's last phase returns `s[Result]`; scratch slots are never returned | collections suite (279) and its 11 mutants, refsem; B2's D5 snapshot rule (the receiver is cloned when shared, so a callback mutating it does not reach the iteration) |
| E4 | `collection_sort_init` | `collection_sort.cpp:116` keep-alive | a slot of the comparator sort's resumable state | never returned (scratch) | sort-protection mutants (S1, S2, S4, S5), refrep sort cases |
| E5 | `collection_plain_sort` | `collection_plain_sort.cpp:25,27` order | a raw storage reference is held across `std::sort`, then the permutation commits through the protocol | never returned (scratch) | collections sort cases, `test-rt-sort-forgery` (the commit) |
| E6 | `deep_copy_tuple` | `copy_graph.cpp` `GraphCopy::fill` (rows 18, 19) | the shells are created first and held in the identity map (`copies_`) so a DAG keeps its sharing; each is filled in post-order before any parent holds it | the parent's fill, or the returned roots | `test-rt-build` (deep_copy under allocation injection: the source unchanged, the DAG kept, nothing leaks), refsem (copy, deepcopy), CP18 transfer campaigns (Linode) |
| E7 | (none: row 17 became a builder) | | | | |
| E8 | `cell_read` | `cell.cpp:90,92` | updates two cache fields of a selector cell's own descriptor after its creation (not a new value at all) | never user-visible | refsem cells (`cell_cycle_*`), refrep cell cases; B2 carried item L2 (`cell.cpp`) unchanged |
| E9 | `Vm::op_keyed_mutate`, `set_index`, `natives::obj_merge` | rows 30, 45, 46 | test mutants only (REFSEM R14, M-B2 30, R-B5 26): each is a deliberate bypass of the protocol on a published receiver, which the mutant gates must catch | not compiled in production, and (review F3) a friend of the key only in its own mutant build (`NIFT_REFSEM_MUTANT` 14, 30, 26); re-review N3 and check C2: the switch is a macro, so a production object that defines or undefines a test-only switch in source, or sets one on its command line other than its own rt-obj configuration's `-D` names, is refused (`compile-object.sh --production`; the rule is in "Final check fixes") | `test-runtime2-refsem-mutants` (each caught) |

### 2.5 Tests

- `make test-rt-build` (`tests/rt/build_unit.cpp`, in `certify-targets`), built three ways from one
  small, explicit source set so each variant is consistent throughout: release (204 checks),
  checked (`NIFT_RT_CHECKED_REFS`, 240 checks), and the two mutants. It covers:
  - publish, then reuse (push, set, append, add, publish twice) for every builder kind;
  - repeated moves (a builder moved twice; each moved-from one reused);
  - publication through a reference to the builder, then reuse through a pointer;
  - allocation failure at every allocation of a nested construction (an object holding an array, a
    struct and a bulk array, each from its own builder, published into the parent): no partial
    value escapes, nothing leaks (allocator and live-heap counts), 18 failing sites (24 checked);
  - publication and the bulk tally allocate nothing (the next allocation set to fail);
  - graph construction under injection: `clone_constant` (builders: 11 sites) and `deep_copy` (the
    in-place key: 28 sites) of a DAG: no leak, the source unchanged, the DAG kept by `deep_copy`;
  - destruction of unpublished builders on every failure path (the leak checks above; publication
    itself cannot fail).
  A reuse runs in a forked child that snapshots the published value (size, refcount, every element)
  and reports on every exit path (return, SIGSEGV, SIGABRT) whether it changed; release and checked
  must report "unchanged, the reuse faulted or aborted".
- Builder mutants (the gate requires each to fail and to show at least one observed change):
  **B-M1** (`NIFT_RT_BUILDER_MUTANT=1`: `publish()` copies the value out and the builder keeps it,
  so storage authority is retained after publication): 17 failures, 7 of them observed changes to
  the published value (push, set, publish twice, append, Object, Struct, alias); **B-M2** (`=2`: a
  move copies, so the moved-from builder keeps the value): 7 failures, 4 observed changes.
- Compile-fail (`test-rt-protocol-compile-fail`, each must not compile, matched on its specific
  diagnostic): `build_copy.cpp`, `build_assign.cpp`, `build_from_value.cpp` (reconstruct over a
  published value), `build_publish_lvalue.cpp`, `build_free_function.cpp` (the former
  `array_push` on a published value; compiles on `1e23b13e`), `build_primitive.cpp`,
  `build_key_forge.cpp`, `build_key_bitcast.cpp`, `build_key_copy.cpp`; `absent.cpp` asserts the same
  structurally (no free construction function, the primitives inaccessible, the builder and key
  traits, publish only on an rvalue). Fixture change, stated: `absent.cpp`'s control "the
  construction API is detected" named the free `map_build_set`, which this component removes; it
  now names `build::Array::push` (the control's purpose, that the detector can say yes, is
  unchanged).
- Tests construct through shims with the former names (`tests/rt/storage_access.h`:
  `array_push`, `object_set`, ... over `StorageTestAccess`, the poisoned test friend of component
  3); test bodies are unchanged, twelve test sources gained the include.

### 2.6 Performance (local callgrind, guidance only; Linode is authoritative)

`ops,mutation` (`benchmarks/runtime2/vm_compare.py`) and the construction paths
(`scratchpad/tooling/perf/build_paths.py`: marginal instructions per loop iteration, N = 2,000 vs
4,000, `NIFT_VM=require`, output checked), against `1e23b13e` (identical to `c1f43300` on every
path):

| Path (Ir per iteration) | `1e23b13e` | builders, first version | spent check on, forwarding fixed | final (`f93441af`) |
| --- | --- | --- | --- | --- |
| JSON object literal (`LoadKFresh`, `clone_constant`) | 4,381 | 4,469 (+88) | 4,397 (+16) | 4,381 (0) |
| array literal (`NewArray`) | 1,611 | 1,649 (+38) | 1,624 (+13) | 1,614 (+3) |
| object literal with computed values (`NewObject`, `ObjectSet`: in place) | 2,559 | 2,569 (+10) | 2,563 (+4) | 2,560 (+1) |
| `keys()` (`ArrayBulk`) | 2,179 | 2,240 (+61) | 2,192 (+13) | 2,180 (+1) |
| `entries()` (`ArrayBulk` + `Object`) | 4,406 | 4,491 (+85) | 4,429 (+23) | 4,407 (+1) |
| `map()` (resumable native: in place) | 7,453 | 7,482 (+29) | 7,462 (+9) | 7,453 (0) |
| `split()` (`ArrayBulk`) | 2,940 | 3,024 (+84) | 2,961 (+21) | 2,941 (+1) |
| `merge()` (`Object`) | 2,583 | 2,606 (+23) | 2,591 (+8) | 2,585 (+2) |

What moved the numbers: the first version passed each stored value by value twice (an extra
`Value` move per element) and the key forwarders added another; fixed by taking the stored value
by rvalue reference in the out-of-line primitives. The spent-builder check cost 10 to 20 per literal
or native result, so it runs in checked builds only (coordinator ruling). `map()`'s +9 was the key
object made at `collection_step`'s entry (a stack slot on every phase); it is now made only in the
branches that construct. An unchecked build was measured separately to attribute the check's cost.

`ops,mutation`, `c1f43300` versus `f93441af` (Ir/op): loop 117/117, add_local 188/188, call
566/566, array_index 436/436, array_push 368/367, object_member 762/762, string_concat 3,314/3,312,
if_branch 249/249; array_set 380/381, array_push_pop 1,431/1,430, array_insert_pop 2,446/2,446,
map_set 1,498/1,496, set_add 1,365/1,363, stack_push_pop 2,192/2,190. An intermediate tree had
map_set +20, set_add +11, stack_push_pop +22: the collection primitives' rvalue-reference form had
broken the code GCC shares between them and Storage's protocol commits; with the former signatures
`aggregate_collection.o` is the former object with the two names swapped (2.1). The website build (the
legacy template engine) does not run the scripting compiler or the VM and does not reach these
paths; the site group is measured on Linode.

### 2.7 Gates

Final component tree `f93441af` (code: `fa10fc9b`, then the collection-signature fix and the
run-time layer test); `nift` sha256 `de382d75...`. Gate files under `gates/c2/` (an earlier full
run on `fa10fc9b` is kept under `gates/c2-first/`), all exit 0: `nift`, `test-rt-build`
(release 204, checked 242, B-M1 and B-M2 caught by observed changes),
`test-rt-protocol-compile-fail` (absent.cpp, 22 bypass and build cases, control), `test-rt-lint`,
`test-c-abi-symbols` (exports unchanged: 73), `test-rt-mutation-protocol` (191) and `-mutants`,
`test-rt-refrep` (116) and `-refrep-mutants`, `test-rt-sort-protection-mutants`,
`test-runtime2-refrep-cli`, `test-runtime2-refsem` (474) and `-refsem-mutants` (including R-B7,
R14, M-B2 30, R-B5 26, R9, R2), `test-runtime2-collections` (279) and `-collections-mutants`,
`test-rt-sort-forgery`, `test-rt-isolation`, `test-production-deps`, the CP18 C program (1,044)
and Python ctypes (70). Heavy lanes (the full `test-rt-value`, `test-rt-vm`, the sanitizer lanes,
where the spent-builder check and `check_fresh` are live, CP18 allocation campaigns): Linode.

## 5. Lint hardening (the documented gaps only)

Scope (coordinator ruling): the gaps recorded against rt-lint, nothing more: the seven R3 tokenizer
gaps (b2-refrep.md 19.3, the re-checker's `rc/lintb/rt/vm_bypass2.cpp`, `bypass_inc.inc` and
`rc/m-digit`), and RECHECK2's `rc2/lint2` findings (annotation text accepted anywhere, basename
allowlists, roots and suffixes not scanned, nested `tests/` directories skipped). The style rules
(function length, line length, string compare, one concern) are unchanged. After component 2 the
construction names no longer exist as free functions (a call does not compile), and after
component 3 a production `StorageTestAccess` does not compile; the lint stays as defence in depth
and the reviewable record of every in-place site.

### 5.1 What changed (`tools/rt-lint.py`)

- **`lex()`, the source as the compiler reads it.** Translation phase 2 first: every line splice
  is removed, so `array_\` + `push(` is one identifier; a spliced logical line is reported on the
  physical line it starts on. Then the phase 3 forms: `//` and `/* */` comments; string and
  character literals (contents blanked); raw strings (`R`, `u8R`, `uR`, `UR`, `LR`, any valid
  delimiter, over any number of lines), so `R"(")"` or `R"(")" "/*"` no longer desynchronises the
  scan; pp-numbers, so the digit separator of `1'0` (and `0x1'Fp+2'0`) is not a character literal;
  the digraphs `%:` and `%:%:` (read as `#` and `##`, so `a %:%: b` is a token paste and
  `%:include` an include); the header name of each `#include`. Per physical line it returns the
  code, the comment text and the included header name.
- **Annotations are read only from comments**: on the line, or on a comment-only line directly
  above it (as before); text inside a string or raw string is not an annotation.
- **Allowlists by repository path**: `STORAGE_ALLOWED`, `SORT_ALLOWED` and the new
  `TEST_ACCESS_ALLOWED` name `src/rt/...` paths (`src/rt/natives/support.*` for the array
  builder); a file with an allowed basename elsewhere is checked.
- **`rt-lint: test-access-decl` only in `src/rt/aggregate.h` and `src/rt/build.h`**; anywhere else
  the annotation itself is a problem, and the name on that line is still flagged.
- **`--boundary`** (in `test-rt-lint`, replacing `--test-access src include bindings` and the B2
  `.inc` grep): the storage-write, construction, sort, paste and test-access rules over `src`,
  `include`, `bindings`, `markuppp` and `minifypp`; every directory below them, nested `tests/`
  included (`src/tests/`, `src/rt/tests/`); symlinked directories followed (review F11); only the
  top-level `tests/` is skipped, and `third_party/` is scanned only where a scanned file includes
  it (review F11: `src/Sha256.cpp`'s vendored header); every C/C++ suffix (`.h .hh .hpp .hxx .h++ .c .cc .cpp .cxx .c++ .inc .ipp .tpp .tcc
  .ixx .cppm .def .m .mm`); and every file a scanned file `#include`s, whatever its suffix
  (resolved against the including file's directory and the build's include roots). Today: 687
  files (`src` 615, `markuppp` 38, `minifypp` 12, `bindings` 11, `include` 10, plus
  `jsonic/include/json.h` through an include; 3 `.inc`, 15 `.c`, 2 `.cc`), 0 problems.
- **Annotation manifest** `tools/rt-lint-annotations.txt`: every boundary annotation
  (`storage-ok <label>`, `paste-ok`, `test-access-decl`) by path, label and count (42 at
  `df7eb54e`). An annotation not listed, a listed one gone, or a changed count fails `--boundary`.
  (Review F1, corrected: at `df7eb54e` the claim "a new in-place site is a reviewed edit of the
  manifest" did not hold for the six renamed primitives, which no rule matched; since the fix the
  manifest pins each annotation by path, label and the normalised code of the line it covers, 41
  lines, and an annotation that covers no checked reference fails; see "Review fixes".)
- **One source edit**: `src/embed/program_api.cpp`'s `PROGRAM_VIEW` macro (C API accessor names
  built with `##`, found by the wider scan) gains a `/* rt-lint: paste-ok ... */` comment on its
  `#define` line; `program_api.o` is byte-identical and `nift` is unchanged (`de382d75...`).

### 5.2 Gap by gap

| Gap (source) | Before | Now | Fixture |
| --- | --- | --- | --- |
| digit separator `1'0` (R3, `rc/m-digit`) | rest of line read as a char literal | pp-number | `bad_lexical.cpp` 7, 29 |
| raw string `R"(")"` (R3) | quote desync | raw string skipped | 8, 36; `tree/src/embed/t6_raw.cpp` |
| splice inside a name (R3, rc2 t4) | two halves, no match | phase 2 first | 10, 40; `t4_splice.cpp` |
| digraph paste `%:%:` (R3, rc2 t3) | not a paste | `##` | 2; `t3_digraph.cpp` |
| annotation text in a string (R3) | raw line searched | comments only | 14, 36 |
| raw-string comment desync (R3) | lines hidden until `"*/"` | raw string skipped | 17, 18, 19 |
| `.inc` never opened (R3) | B2 grep of `src/rt/*.inc` only | every suffix, every include | `bad_lexical_inc.inc` 2; `x.inc`, `x.ipp`, `x.hxx`, `x.def` |
| annotation accepted anywhere (rc2 t1) | any file | `aggregate.h`, `build.h` only | `t1_annot.cpp` 2, 6; `src/embed/aggregate.h` 4 |
| basename allowlists | `support.h` etc. anywhere | repository paths | `src/embed/build.h` 3 (flagged), `src/rt/build.h` (passes) |
| roots `markuppp`, `minifypp` not scanned (rc2) | `src include bindings` | all five | `markuppp/src/*.cpp`, `minifypp/include/minify_extra.h` |
| nested `tests/` skipped (rc2) | any `tests` path component | top-level `tests/` only | `src/tests/helper.h`, `src/rt/tests/helper.h` flagged; `tests/ok_helper.h` skipped |
| no-suffix file | never opened | opened when included | `src/embed/noext` (via `includes_noext.cpp`) |

The fixture `tests/rt/lint_fixtures/bad_lexical.cpp` holds `vm_bypass2.cpp`'s b1 to b8 (b4's pasted
name is caught at its `#define`, b7 through the `.inc` it includes, b8's second statement after one
annotation), the `rc/m-digit` `push_value` line, a raw string with a delimiter over three lines, an
annotation inside a raw string, a splice inside `Storage::`, and a line under a code line's
annotation; the self-test requires exactly lines 2, 3, 7, 8, 10, 14, 17, 18, 19, 26, 29, 35, 36,
39, 40, 43 and the `.inc`'s line 2. `tests/rt/lint_fixtures/tree/` is `rc2/lint2`'s `src`,
`markuppp` and `minifypp` files byte for byte plus the path, location, suffix and root controls
above; the self-test requires the exact test-access and storage-write lines of all 25 scanned
files, the three misplaced annotations, that `noext` is scanned and `tests/ok_helper.h` is not, the
tree's own manifest clean, and three manifest mutations (a count changed, an unlisted annotation,
a listed one gone) each failing. Every earlier fixture and its expected lines are unchanged. Run on
the re-checker's own trees: `rc/lintb/rt` fires on every form of `vm_bypass2.cpp` and on
`bypass_inc.inc` (12 problems; the B2 lint found 1, b8's second statement); `rc2/lint2` flags every file in the five roots (16
test-access lines; t2 and t3 at their pasting `#define`); `rc/m-digit/vm_ops.cpp` line 349 is
flagged.

**Lint mutants** (`scratchpad/tooling/lint-mutants.py`, a local evidence script: each removes one
lexer step or rule and requires the self-test to fail): no splice handling, no digit separator,
no raw string, no `%:%:`, annotations from the raw line, annotation above any line, basename
allowlist, `test-access-decl` anywhere, no location rule, nested `tests/` skipped, no include
following, `.h`/`.cpp` only, manifest ignoring unlisted annotations: 13 of 13 caught.

### 5.3 Limits (recorded, not closed)

- A lint, not a compiler: it does not run the preprocessor. A computed include (`#include MACRO`)
  is not followed; a construction name built by a macro other than token pasting (for example an
  object-like macro that expands to `array_push`) is caught only at the paste or by the compiler
  (after component 2 the free names do not exist). The alias form (`rc/lintb/rt/vm_alias.h`: one
  annotated `auto& build_push = array_push;`, then unannotated uses) passes the per-line rule; the
  manifest pins the annotation, and component 2 makes the aliased name unresolvable.
- Not scanned: `benchmarks/`, `tools/`, `jsonic/` and `third_party/` (except what production
  includes: review F11), top-level files, and an unincluded file with no C/C++ suffix: none of
  these is compiled into `nift` or `libnift_c` (except through the includes followed). `rc2/lint2`'s `benchmarks/x.h`, `tools/x.h` and `top.h` are outside the roots.
- Trigraphs (removed in C++17) are not translated.

### 5.4 Gates

Code commit `eb3ebb1b`; gate files under `gates/c5/`, all exit 0: `nift` (sha256 `de382d75...`,
unchanged), `test-rt-lint` (self-test 16 fixtures; `src/rt` 0 problems; boundary 0 problems in
687 files, 42 annotations against the manifest), `rt-lint-mutants` (13 of 13), `test-c-abi-symbols`
(73 exports), `test-rt-isolation`. No production object changes, so the runtime gates of section
2.7 stand; nothing for Linode beyond the combined-tree certification.

## Review fixes (independent review of `df7eb54e`)

The review (combined ACCEPT-WITH-CONDITIONS; component 5 REJECT as delivered) found no change of
semantics, behaviour or expectations. These commits on top of `df7eb54e` fix or record each
finding; history is not rewritten. Probes cited as `x/...` and `lintN/` are the reviewer's, under
the session scratchpad `review-tooling/`.

| Finding | Fix | Evidence |
| --- | --- | --- |
| F1 HIGH: the six renamed primitives (`Construction::array_storage`, `array_set`, `object_slot`, `map_set`, `set_add`, `stack_push`) matched no lint rule; 11 pinned annotations covered unchecked lines | any `Construction` token is a construction reference and any `Storage` token a storage reference, so the lists cannot fall behind a rename; the self-test reads every static member declared in `class Construction` (build.h, 9) and `class Storage` (aggregate.h, 15) and requires each to be flagged; an annotation must cover a checked reference (`annotation-unused`); the manifest pins each annotation by path, label and the normalised code of the line it covers (41 lines), so none can move to a new site or stay unneeded; the pre-existing `storage-ok scratch` in `collection_sort.cpp:29` covered nothing the lint checks (a raw-layout hand-out: the known raw-layout route, 4.3) and is now a plain comment (object unchanged); the other 10 now sit on checked lines | `bad_inplace.cpp` (`lint1/src/rt/probe.cpp`: every primitive, `using C = Construction`, an unneeded annotation); the self-test plants the reviewer's `Construction::array_storage(bk,s[Returned]).clear();` into the real `collection_helpers.cpp` after `case M::Map:` and requires exactly that line flagged (and the real file clean); `lint1` flags all 7 lines |
| F2: backslash, blanks, newline is a splice to GCC and Clang, not to `lex()` | `splice()` matches `\\[ \t\f\v]*\r?\n` | `bad_lexical.cpp` 46 (`Construct\␠␠⏎ion::`), 48 (`Storage\⇥⏎TestAccess`), 51 (`#\␠⏎#`); `lint4/` flags all three forms |
| F3: `friend class Vm`; mutant-only owners friends in production; forgery routes not recorded for the key | `InPlaceKey` befriends `VmInPlace` (`rt/vm_in_place.h`), which befriends exactly `Vm::cold` and `Vm::struct_op` (and `Vm::op_keyed_mutate` only under `NIFT_REFSEM_MUTANT == 14`); `set_index` and `obj_merge` are friends only under their mutant values (30, 26); the key's destructor is public (an owner's local is destroyed in its scope) and user-provided; 2.2 and 2.4 now list the cast, union and explicit-instantiation routes as the deliberate-source class of `SortKey` | compile-fail `build_key_vm_member.cpp` (`x/k5_vm.cpp`), `build_key_mutant_owner.cpp` (`x/k6_objmerge.cpp`); `absent.cpp` asserts not default-constructible, not aggregate, not trivially copyable or destructible, no copy or move; every production object and `nift` byte-identical before and after (`de382d75`, `vm_report.o`, `vm_struct.o`, `vm_keyed.o` compared); the R14, R-B5 and M-B2 mutant units compile |
| F4: `#undef NIFT_PRODUCTION_TU` before `aggregate.h` removes the poison and the assertion; 3.2 claims wrong | `compile-object.sh --production` refuses an object whose command line undefines the marker (`-U`) or any of whose in-repository dependencies holds an undef directive for it, read as the preprocessor reads it (splices including backslash-blank-newline removed, `%:` as `#`, comments allowed between the tokens; a conservative text check: such a directive in a string or a disabled `#if` block is refused too); 3.2 corrected | `test-rt-isolation` cases `f4-undef-in-unit` (the reviewer's `vm_ops.cpp` plant, `x/i1_undef.cpp`), `f4-undef-in-header` (`# /* x */ undef NIFT_PRODUCTION_\␠⏎TU` in an included header), `f4-undef-command-line` |
| F5: `check-symbol-refs.sh` fails open on `nm` errors; the (object, symbol) limit | fails closed: no objects, an `nm` failure or any diagnostic other than "no symbols", an empty listing of an object with LTO sections, an object `readelf` cannot read; the limit is stated in the script, 2.2 and 4.3 | a missing object, a non-ELF file and an empty object list each fail; the 290 in-tree objects pass |
| F6 LOW: `#pragma GCC system_header` hides a test include from `-MMD` | production objects record dependencies with `-MD` (every header; the compile command and the signature are unchanged) | `test-rt-isolation` case `f6-system-header` (`x/sysh`) |
| F7: the configuration check missed most links; sidecar and object renamed separately | `check-config.sh --sets` runs first on every link and archive rule of the Makefile that has object prerequisites (256 rules: `nift`, `libnift_c.a`, `libnift_c.so`, the sanitizer and TSan `nift`, every test and mutant link), and checks each object of a signed set against its set's signature (in-tree: `default.flags`; `pic`, `san`, `san-lifetime`, `tsan`; each `rt-obj/<config>`), the C or C++ template by the source's suffix; private mutant and probe objects carry no signature (F8, 1.4). The sidecar now holds the object's sha256 beside the identity (format v3: every object rebuilt once, the second one-time rebuild of this checkpoint), so a sidecar and an object from two compiles never pass as a pair. Remaining window: a consistent object-and-sidecar pair of another configuration renamed in between the check and the link (unsupported case only; `certify.py` never does it) | `check-build-reuse.sh` gains the reviewer's `-O1` archive reproducer (`libnift_c.a` refused) and a sidecar beside another compile's object (refused); run locally as a focused check (below); the full `test-build-reuse` runs on Linode |
| F8 LOW (record) | not changed: compiler environment variables (`CPATH`, `CPLUS_INCLUDE_PATH`, `GCC_EXEC_PREFIX`, `COMPILER_PATH`, `SOURCE_DATE_EPOCH`) are not in the identity; `LDFLAGS`/`LDLIBS` changes and a source removed from a list do not relink (pre-existing); an object rewritten by another tool keeps a stale sidecar only if the tool also writes a matching sidecar (the sha256 now refuses a bare object rewrite); the private mutant, probe, CP20-CP22 and session-mutant object rules have no signature and no dependency file (pre-existing, 1.4) | |
| F9 LOW: two changed objects unlisted | 27 of 291 in-tree objects differ from `c1f43300` (the reviewer's count): 25 of the 26 objects whose source component 2 changed (one compiles to the same object; `collection_plain_sort.o` differs by one swapped `cmp` operand, read only by `je`), and `src/rt/session.o` and `src/rt/isolate_transfer_decode.o`, which differ only in section and COMDAT order (template instantiation order from `build.h`'s inline code): same section set, identical per-section disassembly, identical `.rodata` and `.data` | the reviewer's `tools/secdiff.py` |
| F10 LOW: record and controls | 2.1 (`build::Object::get()` is a `const Value*` into the builder's object, valid until its next `set` or `publish`) and row 23 ("on every return"; the exception path) corrected; `absent.cpp`'s free-function detector gets a positive control through the same macro (`array_size`); `build_publish_lvalue.cpp` matches the lvalue diagnostic (g++ "discards qualifiers", clang "rvalue ref-qualifier") and `control.cpp` compiles `std::move(builder).publish()` | `test-rt-protocol-compile-fail` |
| F11 LOW: symlinked directories; `third_party/` includes | the scan follows symlinked directories (each real directory once) and scans a `third_party/` header that a scanned file includes (689 files today) | fixture tree: `src/rt/linked -> ../../elsewhere`, `third_party/lib/inc.h` (scanned), `third_party/lib/unincluded.h` (not); `lint5/` flags `src/rt/sub/x.cpp` |
| F13 (provenance) | the gate script refuses to start on a tree with modified tracked files and records `git status` before and after in each result file | gate files under `gates/review/` |

Not changed (no finding requires it): F11's `test-production-deps` command-count range (the gate
still requires a non-empty list; recorded), F12 (component 6 controls and the header parser's
one-declarator form; recorded), Clang wording in `tests/rt/compile_fail/run.sh` (the harness
matches g++ diagnostics, as before).

Lint mutants (local evidence, `scratchpad/tooling/lint-mutants.py`): 19 of 19 caught, the 13 of
section 5 plus F2 (splice without blanks), F1 (no `Construction` token rule, no
`annotation-unused` rule, manifest by count) and F11 (no symlinks, `third_party/` never).

## Re-review fixes (narrow re-review of `5e156911`)

One bounded round (coordinator ruling, owner guidance: tooling must not become another B2): the
findings below are fixed in commits on top of `5e156911`; any deliberate-source route still open
after them is recorded as a limit, not chased further. Probes cited as `rr/`, `rrlint/` and
`rrlint2/` are the re-reviewer's, under the session scratchpad `review-tooling/`.

| Finding | Fix | Evidence |
| --- | --- | --- |
| N1 (mandatory): `check-symbol-refs.sh` dropped weak undefined references (`nm` class `w`, `v`), so the link layer failed open from any object | every class `nm -u` lists is a reference | `test-rt-sort-forgery` case 3: a weak `Storage::array_mut` reference planted into `src/Hooks.cpp`, by `__attribute__((weak))` (the `rr/e3-weak.diff` form) and by `#pragma weak`, fails `make nift` at the allowlist; `rr/w_weak.o` is refused |
| N2: asm labels and mangled names carry no lint token | the boundary lint flags any identifier or string literal spelling a mangled `nift::rt` name (`_Z...4nift2rt`): an `asm`/`__asm__`/`__asm` label, `#pragma weak`, an `extern "C"` declaration so named; production code has none | fixture `bad_mangled.cpp` (`rr/e1-asm.diff`, the weak form, `rrlint/src/rt/v4_asm.cpp`): exactly its 5 lines; `rrlint/src/rt/v4_asm.cpp` flagged; lint mutants 21 of 21 (L20, L21 new) |
| N3: a `#define NIFT_REFSEM_MUTANT 26` in a production source turned a mutant, and its key friendship, on in release | `compile-object.sh --production` refuses, in any repository dependency, a `#define` or `#undef` of a test-only switch (`NIFT_*` names containing `MUTANT`, `TEST`, `PROBE`, `HOOK` or `BYPASS`, and the builder-mutant hooks `NIFT_RT_BUILDER_RELEASE`/`TRANSFER`), except the 8 reviewed definitions (the `#define NIFT_CORO*_MUTANT 0` off defaults and the hooks in `rt/build.h`), and refuses `-D`/`-U` of one on the command line; test builds keep setting them on their own command lines (check C2, corrected: the set missed configuration switches such as `NIFT_RT_NO_CYCLE_CHECK`, the allowlist ignored values, and the rt-obj configurations are `--production` compiles that pass their own defines; see "Final check fixes") | `test-rt-isolation`: `n3-mutant-define-in-unit` (`rr/e2-mutant-define.diff`), `n3-test-switch-in-header` (`NIFT_SORT_WORDING_TEST`), `n3-switch-command-line` |
| N4: the scanner missed a lone CR, a backslash-CR splice, a UTF-8 BOM, a path with a space, and headers outside the repository | the checks move to `scripts/build/check-production-tu.py`: the `-MD` list parsed with make's escapes; each dependency resolved through symlinks; one outside the repository and outside the compiler's own system include directories (`COMPILER -x LANG -E -v`, without the command's flags; cached per compiler under `.build/obj-config/`) fails; each repository file read with a leading BOM dropped, splices with LF, CR LF or a lone CR removed, CR LF and lone CR as line ends, `%:` as `#` | `test-rt-isolation`: `n4-lone-cr`, `n4-backslash-cr-splice`, `n4-utf8-bom`, `n4-space-in-path`, `n4-symlink-outside`, `n4-absolute-outside`; every one of the 295 existing in-tree production dependency lists passes |
| N6: a quote in a dependency's name stopped `xargs` | fixed with N4 (no `xargs`; the list is parsed) | `test-rt-isolation` `n6-quote-in-path` (a header named `probe'q.h` that includes a test header) |
| N5 (record) | a macro defined before the poison expands past it (`rr/probes/p1_macro_before.cpp`, `p2_rename.cpp`); 3.2 corrected ("however spelt"); the lint catches both at the `#define` line's `StorageTestAccess` token; the compile-time and link layers do not | |
| N7 (record) | the F7 row's "256 link and archive rules that have object prerequisites" overstates: 256 rules carry `check-config.sh --sets`, about 100 of them have no `.o` prerequisite (they compile sources directly, so `$(filter %.o,$^)` is empty and the check is a no-op), and the mutant loops (`test-rt-refrep-mutants`, `test-rt-sort-protection-mutants`) link `$(RT_REFREP_OBJECTS)` in a shell loop outside `$^`, covered only through their dependency on `refrep-vm`, whose rule checks the same objects. The re-review found no unchecked consumer of signed objects; a link outside make (a hand-run compiler, the binding builds) is not checked | |
| N8 (record) | the F1 row's "none can move to a new site" means: none can move to a line of different text; identical lines in one file (the two `Construction::array_push(bk,s[Keys],r);` in `collection_helpers.cpp`) are interchangeable, and a pinned line continued on the next line (`for(const Value& i:order)Construction::array_push(bk,s[Result],`) pins only its first line | |
| N9 (record) | a forged `class VmInPlace` in a unit that does not include `rt/vm_in_place.h` constructs a key (`rr/probes/k8_forge_vminplace.cpp`): an ODR violation, the forged-header class of 3.4 and 4.3; the lint catches it (`Construction` on the call) | |

The F4 row's "read as the preprocessor reads it" holds since N4 for the forms listed there; the
F5 row's "fails closed" and 2.2's "does not link" hold since N1. The scanner stays a text check:
a directive written so that only full preprocessing reveals it (built by macro expansion, which
`#define`/`#undef` cannot be) is not a form the language allows, and a header outside the
repository cannot be reviewed, so it is refused rather than scanned.

### What each layer catches (deliberate-source routes, as of this round)

| Route | Compile time | Object scan (`--production`) | Link (`check-symbol-refs.sh`) | Lint (`--boundary`) | Run time (checked builds) |
| --- | --- | --- | --- | --- | --- |
| a free or private primitive called by name | refused | | refused outside the allowlisted objects | flagged | `check_fresh` |
| an in-place key made outside its owners | refused | | (inside an allowlisted object: no) | flagged | `check_fresh` |
| a key forged by cast or union member (`x/k1`, `x/k2`) | compiles | | not inside allowlisted objects | flagged (`Construction` token) | `check_fresh` |
| explicit-instantiation access to a primitive or commit (`x/k3`, `x/k4`) | compiles | | refused outside the allowlisted objects | flagged (`Construction`/`Storage` token) | `check_fresh` |
| a forged `VmInPlace`, `Storage`, `SortKey` or `Protocol` (ODR) | compiles | | refused outside the allowlisted objects (static `nift`); not exported from `libnift_c.so` | flagged | `check_fresh` |
| an asm label or mangled name, strong or weak (N1, N2) | compiles | | refused outside the allowlisted objects (weak too, since N1) | partly flagged: a label in one literal or an identifier (since N2); a label split across adjacent literals or written with `\x` escapes passes (check C4), and inside an allowlisted object only review and `check_fresh` stop it | `check_fresh` |
| `StorageTestAccess` defined in production | poisoned or asserted | | | flagged | |
| ... through a macro defined before the poison (N5) | compiles | | not inside allowlisted objects | flagged | `check_fresh` |
| `#undef NIFT_PRODUCTION_TU`, a test-only switch defined (N3, N4) | compiles | refused | | | |
| a test header included from production | `#error` (storage_access.h) | refused | | | |
| a header outside the repository (N4) | compiles | refused (unless a compiler system directory) | | not scanned | |

No layer refuses a route that is compiled into an allowlisted object and spelt with none of the
lint's tokens; the remaining guard there is review and `check_fresh` in the sanitizer lanes
(Linode). These are accepted limits of this checkpoint.

## Final check fixes (short check of `19395b98`)

Last fix round (coordinator ruling); every remaining route is a deliberate-source route of a
recorded class and is recorded, not chased.

| Item | Fix or record | Evidence |
| --- | --- | --- |
| C1 (regression from `5e156911`): a dependency named `...:` hid itself and every later dependency from the parser | `deps_of` reads only the object's own rule (the first logical line of the `-MD` file, the `OUT:` target dropped), split with make's escapes | `test-rt-isolation` `c1-colon-in-name` (the `src/rt/natives/m:` plant, `ck/plants.diff`); the self-test parses `o.o: a.cpp m: b\ c.h d$$.h e\#f.h` |
| C2: the test-only set missed configuration and measurement switches (`NIFT_RT_NO_CYCLE_CHECK` turned the cycle check off in release) | the refused set is `TEST_ONLY` (the name pattern) plus `NIFT_RT_BUILDER_UNCHECKED` plus every `NIFT_*` name any Makefile rule passes with `-D`, read from the Makefile at each compile (47 names today), so a new configuration or test define is covered without an edit; refused in any production source (except the reviewed defaults) and on any production command line, except that an object under `.build/rt-obj/<config>/` may carry its configuration's own `-D` names on the command line (read from its `RT_OBJ_CONFIG` line): destroy-local and san-destroy-local `NIFT_RT_DESTROY_LOCAL`; saturated-slots `NIFT_RT_SATURATED_SLOTS`; no-cycle-check `NIFT_RT_NO_CYCLE_CHECK`; sourcemap-counter, san-sourcemap-counter and tsan-sourcemap-counter `NIFT_SOURCEMAP_COUNTER`; san and san-pthread none. Two production defaults of test-sized constants are reviewed defaults: `#define NIFT_RT_SATURATED_SLOTS 256` (`heap_account.cpp`) and `#define NIFT_RT_DESTROY_LOCAL 32` (`value.cpp`) | `test-rt-isolation` `c2-config-switch-in-source` (`ck/nocycle.diff`) and the `check-production-tu.py --self-test` step (the known switches read from the Makefile, at least 9 configurations, every configuration define in the set, every default present in its file, the parser and argument cases); all 1,009 existing production dependency lists (in-tree and every rt-obj configuration) pass |
| C3: the defaults allowlist matched (file, name), not the value | each default is pinned by its normalised directive text, value included (`#define NIFT_CORO_MUTANT 0`; 12 entries) | `test-rt-isolation` `c3-default-value-changed` (`compiler_coro.cpp`, `0` to `18`) |
| C6: sort-forgery case 3's `#pragma weak` form produced a strong reference under g++ 13 | the pragma form is an `extern "C"` declaration of the mangled name with `#pragma weak`; each form now compiles the planted object first and requires `nm -u` to show class `w` or `v` before the link is tried | `test-rt-sort-forgery` case 3 (attribute, pragma) |
| C5 (fixed): `-Wp,-D...` and `--define-macro` were not checked | the command-line check reads `-D`/`-U` (joined or separate), `-Wp,` lists, `-Xpreprocessor`, `--define-macro`/`--undefine-macro` (joined or separate) | the self-test's argument cases |
| C4 (record) | the mangled-name lint matches per literal: a label split across adjacent literals or written with `\x` escapes passes it; outside an allowlisted object the link layer refuses it, inside one only review and `check_fresh` (checked builds) stop it; the per-layer table row now says "partly flagged" | `ck/v9_split.cpp` |
| C7 (record, supported configuration) | the system-directory probe runs the compiler path alone; a wrapper (`ccache`) and flags that move the system directories (`--sysroot`, `-stdlib`, `-nostdinc`, an `-isystem` outside the repository) are not supported for production compiles (their headers are refused as outside the repository) | |
