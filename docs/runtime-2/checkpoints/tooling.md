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
  sets, which test binaries also link) has a signature file (`scripts/build/write-signature.sh`):
  compiler, its version line and the exact flags, rewritten only when they change; every object
  depends on it. A changed compiler, flag or define rebuilds the objects; an unchanged one never
  does. Before this, `make nift CXXFLAGS=...` followed by a default build reused the other
  configuration's objects (shown by the negative control with `c1f43300`'s Makefile).
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

All on this host (2 cores, g++ 13.3), commit `861ff7c6`; gate file `gates/c1/build-reuse.result`.

- **Production objects unchanged.** `make nift` with this Makefile (after the one-time rebuild): all
  291 in-tree objects byte-identical to `c1f43300`'s, and `nift` sha256 `3062f229...`, the same as
  `c1f43300` here and on Linode (`b2cert-3c962476`).
- **Old versus new default binary.** `value-unit` built with `c1f43300`'s Makefile (one command) and
  with this one (objects): byte-identical (`a863cf47...`); both runs print `83 checks, 0 failures`
  with identical output. The ASan build `rt-san/value-unit` (old command versus `rt-obj/san`
  objects): byte-identical too (`8f38491f...`). Identical binaries give identical results by
  construction; the full lanes run on Linode.
- **`scripts/build/check-build-reuse.sh`** (default configuration plus `no-cycle-check`; 498 objects
  and binaries), all PASS:
  - deps: touching `src/rt/value_cycle.h` rebuilt exactly the 90 objects whose compiler dependency
    file names it (both configurations); touching `src/rt/aggregate.cpp` rebuilt only its object in
    each configuration; a further `make` rebuilt and relinked nothing;
  - equivalence: clean versus incremental, every object and binary byte-identical;
  - negative controls: `CXXFLAGS` `-O2` to `-O1` rebuilds the default object (signature updated,
    object differs); an added define rebuilds it (mtime: `value.cpp` does not read the define, so
    the object is the same bytes); restoring the flags rebuilds the original object
    byte-identical; dropping `-DNIFT_RT_NO_CYCLE_CHECK` from that configuration rebuilds its object
    (`vm_closure.o`, the one object the define changes), restoring it restores the bytes. For the
    record: `c1f43300`'s Makefile considers `src/rt/value.o` up to date under different
    `CXXFLAGS` (the stale-object hazard the signature closes);
  - separation: the `no-cycle-check` set is its own (`vm_closure.o` differs from the default one);
  - parallel stress: 3 rounds of 3 concurrent `make` processes in one tree, each building a
    different binary from the same default objects with 28 shared objects deleted first: every
    round exit 0, every object and binary identical to the serial clean build, no temporary file
    left.
- **Build timings** (build only, `make -j2`; correctness and performance certification on Linode):

  | Lane | Before (`c1f43300`) | After |
  | --- | --- | --- |
  | One default binary (`value-unit`), objects present | 4 min 26 s (4 min 21 s CPU), every time | 2.5 s |
  | `test-rt-value` binaries (11), after `make nift` | about 48 min CPU (11 x 4 min 21 s; measured one, the others compile the same sources), about 25 min wall at `-j2` | 4 min 36 s wall, 9 min 1 s CPU (the two define configurations, 494 objects, plus 11 links) |
  | The same after touching one runtime source | the same as clean (no reuse) | 10.7 s |
  | The same, nothing changed | no-op | 1.3 s |
  | `test-rt-value-sanitize` binaries (7) | about 55 min wall (the recipe compiles its 7 lines one after another; measured one: 7 min 53 s wall, 7 min 46 s CPU) | 8 min 19 s wall: `rt-obj/san` and `san-destroy-local` (494 ASan objects) 7 min 45 s, 7 links 34 s; afterwards links only |

  Each ASan configuration directory is 578 MB here.

### 1.4 Not changed

Test sources, expectations and fixtures; the binaries' link inputs other than the source/object
form; the remaining test rules that compile small, explicit source lists (handle, error-oom,
mutant and probe builds, which have their own defines and already used private objects or a few
files).
