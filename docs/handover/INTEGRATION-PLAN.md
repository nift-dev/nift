# Pre-integration plan: canonical Nift absorbs Embedded Nift (fast-forward)

Status: **prepared, NOT executed**. Requires explicit authorization. Canonical
Nift (`nift-dev/nift`) has not been modified. Nothing here has been pushed,
merged, moved, archived or published.

---

## 1. Ancestry verification (verified)

Fork topology: `nift-dev/nift-embed` is a GitHub fork of `nift-dev/nift`.

| Check | Result |
|---|---|
| Merge base | `8a818f2b1b55c5e88da0479b71302e8f4cd83d90` |
| Canonical `nift` main HEAD | `8a818f2` (== merge base) |
| Canonical commits on main | 195 |
| Embed commits total | 306 (195 ancestor + 111 embed-only) |
| Embed-only commits (`HEAD ^upstream/main`) | 111 |
| Canonical-only commits (`upstream/main ^HEAD`) | **0** |
| Shared ancestry | Confirmed - `--allow-unrelated-histories` NOT required |
| Integration type | **Fast-forward** (`8a818f2..dae8345`), no merge commit |
| Canonical-only CLI behaviour | None discarded - embed `src/` is a strict superset of canonical `src/` (CLI.cpp, CLI.h, Console.h, FileSystem.cpp, FileSystem.h all present in embed); embed adds Context/Engine/ProjectOwnership/ProjectRead/ProjectState/Value/c_abi + bindings/ + include/ |

Note: the merge base `8a818f2` is the same commit used as the pre-Embed baseline
in the CP18 performance comparison.

## 2. Integration command sequence (to authorize)

```bash
# On a fresh clone of the canonical repository
git clone git@github.com:nift-dev/nift.git nift-canonical
cd nift-canonical
git fetch https://github.com/nift-dev/nift-embed.git main:embed-main
git checkout main
git rev-parse HEAD                 # expect 8a818f2 (safety gate)
git merge --ff-only embed-main     # fast-forward to embed HEAD (dae8345)
# Validation gate (section 7) runs BEFORE push.
git push origin main               # canonical main -> embed HEAD
```

Post-push (only after the validation gate passes):
- Keep `nift-dev/nift-embed` intact as a read-only archived comparison record:
  add a redirect README and run `gh repo archive nift-dev/nift-embed --yes`
  (authorized separately).
- Keep `nift-dev/nift-regression-suite` / `nift-embed-regression-suite` per the
  ownership split in section 6.

## 3. Proposed canonical directory layout (post-integration restructure)

The fast-forward brings embed's current layout. A single **post-integration
restructure commit** then enforces structural isolation (no semantics change;
pure move):

```
nift/  (canonical)
  Makefile
  src/                shared core: nift.cpp, CLI.cpp, Parser.cpp, Value.cpp,
                      FileSystem.cpp, Json*, minifypp, ProjectInfo/Read/State,
                      WatchList, BuildProgress, ProjectOwnership, Console, Types
  src/embed/          Engine.cpp, Context.cpp, c_abi.cpp   <- moved (isolation)
  include/nift/       public embed headers (engine.h, c_abi.h, context.h, ...)
  bindings/go/        bindings/csharp/  bindings/node/  bindings/python/
  jsonic/  minifypp/  packaging/  scripts/  snap/  benchmarks/
  tests/              CLI tests (unchanged)
  tests/conformance/  shared neutral corpus + direct runners (moved from suite)
  docs/               main docs; docs/embed/ = dedicated Embedded Nift section
```

Rationale: the CLI object boundary becomes `src/` minus `src/embed/` (a
directory glob), never a fragile filename exclusion list.

## 4. Makefile target / dependency layout (post-integration)

```make
all := nift                 # plain `make` == CLI only

nift := $(CORE_OBJECTS)     # src/*.o minus src/embed/*.o  (reduced CLI, shipped)
embed := libnift_c.a libnift_c.so   # + headers + nift.pc pkg-config
go-binding      : embed  + go build
csharp-binding  : embed  + dotnet build
node-binding    : embed  + node build.sh (N-API addon)
python-binding  : embed  + python build.sh (abi3/ext)
bindings        : go-binding csharp-binding node-binding python-binding

test            : CLI regression (g++ only)
test-embed      : C ABI adversarial, C-consumer smoke, engine tests, conformance
test-go-binding : go test -race + go conformance
test-csharp-binding : csharp tests + conformance
test-node-binding   : node tests + conformance
test-python-binding : python pytest + conformance
test-bindings   : all binding test targets
test-all        : test + test-embed + test-bindings

install         : CLI only (ordinary Nift install)
install-embed   : headers + libnift_c.a/.so (+ Windows import lib) + nift.pc
```

`make embed` is the single native target (headers + static/shared/import
library + pkg-config). The C++ API vs C ABI distinction is an artifact
distinction (headers + libnift_c.a for C++/C consumers; libnift_c.so dynamic;
Windows .lib), not separate targets.

## 5. Reduced CLI as the canonical shipped/tested target

- `make nift` (and plain `make`) build the **reduced** object set (`src/`
  minus `src/embed/`).
- **CLI CI must test the reduced binary**, not a full-object development
  convenience build. Evidence accepted: reduced binary passes the complete CLI
  regression surface; multipage/paginated output byte-identical; 200x startup
  median unchanged (~1.3 ms); binary 1,256,712 B vs 1,380,240 B; ~4 s
  embedding-only compile avoided.
- Cross-platform reduced-CLI linking (macOS/Windows) added to the CI matrix
  (Linux verified here).

## 6. C ABI compatibility / version policy (initial)

Existing surface: `NIFT_ABI_VERSION "1.0"`, `nift_abi_version()`,
`nift_abi_version_major()`, `nift_abi_version_minor()`.

- **ABI major** is the compatibility contract. It is independent of the Nift
  release-version syntax (currently "1.0") but a **breaking ABI change** must
  receive Nift major-version treatment AND bump the ABI major.
- **Additive changes** (new exported function, new struct field appended with
  an explicit `size` field, new capability) keep the ABI major; bump ABI minor.
- **Breaking changes** (removed/changed symbols, struct layout change) bump the
  ABI major and the Nift major.
- **Struct evolution**: public structs that may grow carry an explicit
  `size` field so consumers can negotiate layout (nift_string / nift_source
  already carry lengths; add size negotiation where growth is anticipated).
- **Symbol compatibility**: exported symbols are never removed or reordered
  within an ABI major; `version-script`/visibility is applied to export only
  the public surface.
- **Feature detection**: `nift_abi_version()` + `nift_abi_version_major()`
  already exist; add a capability/feature query (e.g., a
  `nift_abi_has_feature(name)` entry) for additive features.
- **Binding/native compatibility check**: each binding calls
  `nift_abi_version_major()` at engine creation/initialization and returns a
  clear controlled error on mismatch. Policy: matching Nift versions
  recommended; runtime compatibility governed by ABI major + feature
  availability; a compatible patch difference is NOT rejected.
- **Binary-compatibility tests**: CI keeps a consumer compiled against the
  previous ABI major running against the new library; plus a symbol-surface
  contract test asserting the exported ABI.

## 7. Conformance assets into canonical (ownership split)

- **Move into canonical `tests/conformance/`**: the neutral corpus (36 cases),
  the direct adapters/runners (cpp-embed, rust-embed, c-abi-embed), and the
  expectation files. Canonical correctness depends only on these (in-tree).
- **Remain in the regression-suite repo**: large historical campaigns, heavy
  performance harnesses, the anomaly/warm-baseline harnesses, and external
  orchestration.
- **Release validation pins the exact regression-suite commit.** Canonical
  correctness never depends on an unpinned mutable checkout.
- The binding conformance runs live in canonical via `test-<lang>-binding`
  (each binding's public API is the adapter).

## 8. Post-integration validation matrix (gate before push)

Run against the merged tree at canonical main (fast-forward target), and again
after the restructure commit:

1. **CLI (reduced):** complete `make test` surface; byte-identical build output
   vs pre-merge CLI on the reference project; startup median.
2. **Embed / C ABI:** `make test-embed` (C ABI adversarial, C-consumer smoke,
   engine + bindings + render-api tests, conformance 9/9).
3. **Shared conformance corpus:** all adapters (36/36 x 7).
4. **Bindings:** Go `test -race`; C# tests; Node tests; Python pytest + package
   smoke; all binding conformance.
5. **Sanitizer/fuzz/lifetime:** ASan/UBSan/TSan; ownership/lifetime suite
   retained from the Embed programme.
6. **CI:** full matrix green on the merge commit and on the restructure commit;
   macOS/Windows reduced-CLI + binding jobs.
7. **Release packaging smoke:** CLI packaging (deb/tar/snap), embed bundle
   layout, pkg-config, per-binding package builds (no publication).

## 9. Rollback plan

- Record pre-merge canonical main: `8a818f2` (the fork point).
- The fast-forward is a single ref update. If post-push validation (a window
  kept open after push) finds an unacceptable state: on canonical,
  `git reset --hard 8a818f2 && git push -f origin main`. Safe because canonical
  has zero external post-fork commits; force-push pre-authorized in the rollback
  plan only.
- `nift-embed` is NOT archived until after validation completes, so the entire
  pre-merge state remains fully recoverable.
- No package, tag, or release is published until the matrix passes.

## 10. Authorization gate

Execute section 2 only after:
- authorization to fast-forward canonical `nift` main to embed HEAD; and
- authorization (separate) to archive `nift-dev/nift-embed` post-validation.

This plan and the settled architecture decisions are recorded in
docs/handover/MERGE-DECISION-REPORT.md (sections 11-12 below).