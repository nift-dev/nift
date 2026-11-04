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
