# Pre-integration plan: canonical Nift absorbs Embedded Nift (fast-forward)

Status: **prepared, NOT executed**. Requires explicit authorization. Canonical
Nift (`nift-dev/nift`) has not been modified. Nothing here has been pushed to
canonical, merged, moved, archived or published.

---

## 1. Ancestry verification (verified, claims narrowed)

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
| File presence | Embed `src/` is a strict superset of canonical `src/` (CLI.cpp, CLI.h, Console.h, FileSystem.cpp, FileSystem.h all present in embed); embed adds Context/Engine/ProjectOwnership/ProjectRead/ProjectState/Value/c_abi + `bindings/` + `include/` |

Behavioural-preservation claim (narrowed): shared ancestry plus file presence
prove that no canonical file disappears from the resulting tree, and zero
canonical-only commits prove canonical holds no changes of its own. They do NOT
by themselves prove that no canonical-only CLI behaviour is discarded, because
the 111 descendant commits may have changed existing CLI files and behaviour.
The required evidence is the **complete reduced-CLI regression/equivalence
matrix** (section 8), which must pass before integration.

## 2. Immutable integration target + preflight

The integration target is an **exact reviewed commit SHA**, never a moving
branch. A document cannot reliably contain its own SHA, so the plan uses an
explicit placeholder that the reviewer's authorization message fills in:

```
proposed integration SHA: <APPROVED_INTEGRATION_SHA>
```

The reviewer reports the exact final `nift-embed` HEAD SHA externally; the
authorization names that exact SHA; the integration command fetches it directly
and verifies it. Any subsequent commit invalidates the authorization and
requires a newly reported SHA.

Immediately before integration, confirm ALL of:
- canonical `main` still equals `8a818f2b1b55c5e88da0479b71302e8f4cd83d90`;
- the chosen embed commit is the reviewed descendant;
- the merge base is still `8a818f2`;
- canonical-only commit count is still zero;
- every repository working tree is clean.

## 3. Phased integration sequence (revised)

P1. **Freeze and verify exact SHAs** (section 2 preflight; all repos clean).
P2. **Create a recoverability reference**: on the canonical remote, create a
     clearly named backup ref for the old head before any change:
     ```bash
     git push origin 8a818f2b1b55c5e88da0479b71302e8f4cd83d90:refs/tags/pre-embed-merge-v4   # OR refs/heads/backup/pre-embed-merge
     ```
     (tag/branch creation, no force-push; authorized as part of the plan).
P3. **Fast-forward in a disposable canonical integration clone** (never in the
     long-lived working copy):
     ```bash
     git clone git@github.com:nift-dev/nift.git /tmp/nift-integration
     cd /tmp/nift-integration
     git fetch https://github.com/nift-dev/nift-embed.git <SHA>:embed-review
     git checkout main
     git rev-parse HEAD          # MUST be 8a818f2 (abort otherwise)
     git merge --ff-only embed-review
     git rev-parse HEAD          # MUST be <SHA>
     ```
P4. **Run the complete validation matrix** in the integration clone (section 8).
     Do NOT push if any gate fails; fix forward on `nift-embed` and re-review.
P5. **Push canonical only if validation passes**:
     ```bash
     git push origin main        # 8a818f2 -> <SHA>
     ```
P6. **Rerun required GitHub Actions on canonical** (full matrix green on
     canonical main at <SHA>).
P7. **Structural isolation commit** (`src/embed/` move) only after the
     fast-forward state has independently passed; it is a separate reviewed
     commit, pushed and CI-verified on canonical.
P8. **Build package artifacts and run installation smoke tests without
     publishing** (wheel/nupkg/npm tarball/go module/native bundles + install
     smoke incl. `install-embed.sh` checksum verification).
P9. **Return for publication authorization** (do NOT publish in this plan).
P10. **Archive `nift-embed` only after a separately authorized successful
     completion** (redirect README + `gh repo archive`).

## 4. Proposed canonical directory layout (post-integration restructure)

The fast-forward brings embed's current layout. A single **post-integration
restructure commit** (P7) enforces structural isolation (no semantics change;
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

The CLI object boundary becomes `src/` minus `src/embed/` (a directory glob).

## 5. Makefile target / dependency layout (post-integration)

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

`make embed` is the single native target. The C++ API vs C ABI distinction is
an artifact distinction (headers + libnift_c.a; libnift_c.so; Windows .lib),
not separate targets.

## 6. Reduced CLI as the canonical shipped/tested target

- `make nift` (and plain `make`) build the **reduced** object set.
- **CLI CI tests the reduced binary** (a full-object build is a development
  convenience, never the shipped artifact).
- Evidence: reduced binary passes the complete CLI regression surface;
  multipage/paginated output byte-identical; 200x startup median unchanged
  (~1.3 ms); binary 1,256,712 B vs 1,380,240 B; ~4 s embedding-only compile
  avoided. Cross-platform reduced-CLI linking (macOS/Windows) added to CI.

## 7. C ABI compatibility / version policy (reconciled)

The existing header comment ("Additive ABI extensions keep the version;
incompatible changes bump the minor/major") was inconsistent with the earlier
plan. **One coherent rule is now adopted** (recorded; the header comment will
be updated at integration time):

- **Additive, backward-compatible ABI changes**: bump the ABI **minor**
  (e.g. 1.0 -> 1.1). New exported function, new capability, new option.
- **Breaking ABI changes** (removed/changed symbol signature, struct layout
  change, changed call contract): bump the ABI **major** and receive Nift
  **major-version** treatment.
- **Patch-level implementation changes that do not alter the ABI**: no ABI
  version change.
- **Bindings** require a matching **ABI major** plus feature/capability
  availability, NOT exact Nift patch equality. A compatible patch difference is
  accepted.
- **Symbol compatibility rule (corrected)**: within an ABI major, retain the
  existing exported symbol **names and signatures**; never remove or change a
  published signature. (Ordinary dynamic-library symbols have no
  API-significant source ordering; "reorder" was the wrong word.)
- **Struct evolution**: nothing is publicly released yet, so the initial ABI is
  finalized NOW. Audit every public struct (nift_string, nift_source,
  nift_render_result accessors, nift_context/engine handles) and fix the layout
  before first publication. Do NOT propose later `size`-field insertion as
  automatically backward compatible: adding a `size` member to an already
  frozen struct is itself a layout change. If growth is anticipated, define an
  explicit versioned/negotiated layout at initial release; otherwise freeze.
- **Feature detection**: `nift_abi_version()`, `nift_abi_version_major()`,
  `nift_abi_version_minor()` exist; add a capability/feature query for additive
  features if needed.
- **Binary-compatibility tests**: CI compiles a consumer against the previous
  ABI major and runs it against the new library; plus a symbol-surface contract
  test asserting the exported ABI.

## 8. Conformance assets into canonical (ownership split)

- **Into canonical `tests/conformance/`**: the neutral corpus (36 cases), the
  direct adapters/runners (cpp-embed, rust-embed, c-abi-embed), expectation
  files. Canonical correctness depends only on these (in-tree).
- **Remain in the regression-suite repo**: large historical campaigns, heavy
  performance harnesses, anomaly/warm-baseline harnesses, external
  orchestration.
- **Release validation pins the exact regression-suite commit.** Canonical
  correctness never depends on an unpinned mutable checkout.

## 9. Post-integration validation matrix (gate, phases P4/P6)

1. CLI (reduced): complete `make test`; byte-identical output on the reference
   project; startup median.
2. Embed / C ABI: `make test-embed` (adversarial, C-consumer smoke, engine +
   bindings + render-api, conformance 9/9).
3. Shared conformance corpus: all adapters (36/36 x 7).
4. Bindings: Go `test -race`; C#; Node; Python pytest + package smoke; all
   binding conformance.
5. Sanitizer/fuzz/lifetime: ASan/UBSan/TSan; ownership/lifetime suite.
6. CI full matrix on canonical at <SHA> (P6) and after the restructure commit
   (P7); macOS/Windows reduced-CLI + binding jobs.
7. Release packaging smoke: CLI packaging, embed bundle layout, pkg-config,
   per-binding package builds (no publication).

## 10. Rollback plan (revised - not force-push-first)

- **Recoverability reference (P2)**: before any change, a clearly named
  backup ref (`pre-embed-merge-v4`) is created on canonical pointing at
  `8a818f2`. This is a normal ref creation, never a force-push.
- **Primary strategy is validate-before-push and fix-forward-after-push.** The
  fast-forward is validated in the disposable clone (P4) before any push. If a
  post-push problem is found, the fix is made forward on canonical (a new
  commit), not by rewriting.
- **Force-push rollback is an explicit emergency action only**: if a
  post-push defect is severe enough to require restoring `8a818f2` from the
  backup ref, it requires **separate authorization at that time**. It is not a
  routine mechanism and is not pre-authorized. (Zero external post-fork commits
  makes it safe if ever needed, but that is not the default path.)
- `nift-embed` stays **unarchived** until canonical integration and post-push
  validation are complete.

## 11. Package identities (proposed, no publication)

| Package | Identity | Notes / manual prerequisites |
|---|---|---|
| PyPI | project `nift` | Trusted publisher (OIDC) from workflow `release.yml`, environment `release`; first successful publish secures the name. |
| NuGet | package ID `Nift`, owner `antimatroid`, repo `nift-dev/nift`, env `release`, scope push new packages+versions, package restriction exact ID `Nift`, unlist/relist disabled | **OIDC Trusted Publishing** (temporary-key exchange, see section 12); NO stored `NUGET_API_KEY`. Org policy restricts to the exact ID `Nift`. |
| npm | package `nift` (unscoped) | **Part of the initial production-binding release.** npm Trusted Publishing/OIDC if supported by the final workflow configuration, otherwise the minimum secure config (2FA + scoped publish token owned by the package, least privilege, environment-gated). |
| crates.io | NOT in the production package set | Rust (`nift-rs`) is the independent experimental/conformance implementation; its crates stay separate. No production crate proposed. |
| Go module | `module nift.dev/embed/v4` (v4 major suffix is required for major >= 2) | Submodule at `bindings/go/` in canonical; release tag `bindings/go/v4.0.7` for Nift v4.0.7; consumers `go get nift.dev/embed/v4@v4.0.7`. The `nift.dev/embed/v4?go-get=1` discovery response must point to `https://github.com/nift-dev/nift` and the relevant repository root. Do NOT retain `module nift.dev/embed` while assigning v4 versions. |
| Native C/C++ | GitHub Release assets `nift-embed-{target}.tar.gz/.zip` + `SHA256SUMS` + `install-embed.sh` | Checksum-verified curl/PowerShell installer is the initial C/C++ path; pkg-config `nift.pc`. No Conan/vcpkg in the initial release (no concrete reason). |

**Synchronized package-version derivation.** Binding package versions are
derived from the canonical release tag, never hard-coded (no `0.1.0`): each
publishing job reads the release tag (e.g. `v4.0.7`), verifies it against the
CLI version (`nift --version`), and stamps the package with that exact version
(`4.0.7`). Dry-run artifacts are prepared for the exact candidate version
before publication; nothing is published in this plan. The initial complete
release accounts for: npm `nift`, PyPI `nift`, NuGet `Nift`, Go
`nift.dev/embed/v4`, and native C/C++ release bundles.

## 12. `release.yml` job graph (all production binding registries)

Filename exactly `.github/workflows/release.yml`. The `release` GitHub Actions
environment is applied **only** to external-publishing jobs. `id-token: write`
is granted **only** on those jobs (never globally on build/test jobs).

```yaml
jobs:
  build-cli:            # no environment
    permissions: { contents: read }
    - build reduced CLI for the 5 targets; CLI regression
  build-embed:          # no environment
    permissions: { contents: read }
    - build native embed bundles for the 5 targets; C ABI/conformance tests
  validate:             # no environment
    needs: [build-cli, build-embed]
    permissions: { contents: read }
    - full matrix on the exact release commit (bindings, conformance,
      sanitizers, package smoke) - no publication
  release-github-draft: # no environment (GitHub Release is not external to GH)
    needs: [validate]
    permissions: { contents: write }
    - create DRAFT Release; attach CLI binaries + SHA256SUMS + embed bundles
  publish-pypi:         # environment: release
    needs: [validate]
    permissions: { contents: read, id-token: write }
    - pypa/gh-action-pypi-publish (trusted publishing, project `nift`)
  publish-nuget:        # environment: release
    needs: [validate]
    permissions: { contents: read, id-token: write }
    - name: NuGet login
      id: nuget-login
      uses: NuGet/login@v1        # pin to a reviewed full commit SHA during
                                  # workflow implementation; NOT actions/oidc-token@v1
      with:
        user: antimatroid         # NuGet profile name, not an email address
    - name: Publish NuGet package
      run: >
        dotnet nuget push artifacts/Nift.${VERSION}.nupkg
        --api-key "${{ steps.nuget-login.outputs.NUGET_API_KEY }}"
        --source https://api.nuget.org/v3/index.json
      (NuGet/login@v1 exchanges the GitHub OIDC identity for a temporary API
      key; NO stored NUGET_API_KEY; org policy restricts to the exact ID
      `Nift`)
  publish-npm:          # environment: release
    needs: [validate]
    permissions: { contents: read, id-token: write }
    - npm package smoke (contents/digest verified, dry-run)
    - npm publish (OIDC/Trusted Publishing if the final workflow supports it,
      otherwise the minimum secure config: environment-gated token owned by the
      package with 2FA, least privilege)
  verify-go-module:     # no environment
    needs: [validate]
    permissions: { contents: read }
    - verify nift.dev/embed/v4?go-get=1 discovery response points at
      https://github.com/nift-dev/nift and the repo root
    - clean external `go get nift.dev/embed/v4@<tag>` smoke test from a fresh
      module cache (after the Go module tag exists)
  release-status:       # no environment
    needs: [release-github-draft, publish-pypi, publish-nuget, publish-npm,
            verify-go-module]
    if: always()
    permissions: { contents: read }
    - report each required component's outcome distinctly:
      validation failure | GitHub draft created | per-registry publication
      result | version collision | complete publication
    - fail until every required component is accounted for
```

Rules:
- Language publishing jobs run independently after the common `validate` gate.
- `release-status` runs even when a publishing dependency fails (`if:
  always()`) and distinguishes each registry's result from validation failure,
  from the GitHub draft state, and from a version collision.
- A pre-existing version is NOT treated as matching merely because its version
  string exists. Where a registry provides a reliable content hash/digest
  comparison, idempotent success requires the existing package to match the
  expected artifact (version + contents + digest + provenance). Where a
  registry does NOT provide a reliable content hash comparison, a pre-existing
  version is a **collision requiring manual review** - never claimed as
  idempotent success.
- Retry only the failed job. Release candidates always run the complete matrix.

## 13. Stop boundary

This plan has not modified canonical, merged histories, restructured source,
created tags/releases, published packages, altered the homepage/install pages,
or archived repositories. It is awaiting final integration authorization.
## 14. Packaging status (honest record, 2026-08-27)

| Item | Status |
|---|---|
| Product/integration validation (CLI, engine, C ABI, corpus, bindings, sanitizers) | **PASS** |
| Release packaging implementation | **INCOMPLETE** - commands execute but native-asset inclusion, clean-consumer install, loading, version reporting and no-local-build-dir dependency are unproven |
| Python native wheel | **FAIL / not implemented correctly** - observed `nift-0.1.0-py3-none-any.whl` is a pure-Python/platform-independent tag; the binding has a native extension, so a valid wheel must be an `abi3` platform wheel (stable ABI) or a CPython/platform-specific wheel |
| Repository-generated pkg-config artifact | **NOT TESTED / not implemented** - the earlier check used a hand-written temporary `.pc`; the repository does not yet generate/stage/install `nift.pc` |
| Publication readiness | **HOLD** - nothing published; synchronized version derivation (binding packages derive from the canonical release tag, verified against the CLI version; no hard-coded `0.1.0`) is a packaging-checkpoint task |

These items are outside P7 and are resolved in the packaging checkpoint.
