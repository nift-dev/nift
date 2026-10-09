# NIFT v4.10 — BUILD PIPELINE / DOCS CAMPAIGN FINAL

Decision: retain the certified build-pipeline/docs/transformation tranche, publish its ordinary development commits, require the ten normal non-release hosted walls on the published Nift head, then STOP. The next proposed campaign is performance architecture; it is not started here.

## Scope and contracts

The accepted Silverback baseline is `d924075ce62c833bdc7bde07893e58956f4cb18f`. This tranche is separate from that closed campaign. No release/package publication, RuntimeValue layout change, C ABI change, Jsonic++ edit/development pin, public benchmark rerun, Labs or Linode work is included.

- **Docs:** 92/92 content pages audited against navigation, source references, rendered contextual inbound links, sitemap and search surfaces. Build Scripts is a primary sidebar/sitemap page. Scripting was already linked. Historical performance is now cross-linked from how-nift-works. Three noindex legacy redirects remain intentionally hidden. Genuine remaining orphans: 0. No site-wide search index exists in this checkout. See [the complete page table](docs-audit.md).
- **Sidecars:** `<content-stem>.pre-build.f`, `.build.f`, `.post-build.f`. Explicit generic fields take precedence. Deprecated hyphenated pre/post discovery remains fallback. A dotted/legacy collision without explicit selection is an error; neither can accidentally run twice. Legacy space-separated hooks retain additive mode matching and serial execution.
- **Fields:** `pre-build`, custom `build`, `post-build` are non-empty `.f` path strings, validated for type, extension, NUL and project-local execution. `depends` is an array of valid tracked names. Missing/self/duplicate dependencies and cycles fail before build mutation; cycle paths are useful even at 10k nodes.
- **Lifecycle:** pre → custom script or normal render/minify → post. New scripts use native Parser scopes and invocation-local project/item environment. A custom build owns tracked output bytes and bypasses normal rendering/minification; regular project-local output is mandatory before and after post. Any phase failure fails the item and invocation; later phases stop. Metadata commits only after post succeeds. Partial script writes use existing recovery/repair semantics.
- **Incremental:** scripts, content, native import/file dependencies, sidecar selection and relevant tracked metadata participate. `depends` is completion order only. Clean dependents are rechecked after prerequisites execute so ordinary generated-file dependencies can invalidate them in the same invocation. Multiple consumers share a completed-output hash decision, preventing a hash refresh from concealing changed bytes. Generated output source/JSON caches use symlink-aware identities in DAG builds; retired source owners keep concurrent readers alive.
- **Scheduler:** name/index resolution, numeric adjacency and indegrees, iterative validation, ready queue and blocking condition variable; no busy polling or recursive graph traversal. Failed prerequisites block transitive descendants without success metadata; independent branches continue. Target builds include transitive closure, including clean intermediate prerequisites without rebuilding them. Full, incremental and build-auto share the same path; watch reloads scripts/edges. Renames preserve graph references; removal rejects retained dependent references.
- **Zero dependencies:** optional pipeline payloads remain absent, graph vectors stay empty and allocate no graph storage, no job-vector copy is made, and the existing atomic-index worker path runs. `BUILD_DAG nodes=0 edges=0` confirms selection of that path. Existing load-time content-path uniqueness sorting is unchanged; O(V+E) describes graph validation/planning rather than every operation in project loading.
- **Existing-project init:** rewrite/redesign only add missing workspace files, including nested invocation. Config, tracked state, README, AGENTS, HANDOVER, workbooks, investigation files and other existing bytes remain identical. Existing files win even over append/replace flags. Both modes coexist; reruns are idempotent. Fresh non-project initialization retains its existing collision/policy rules.

The reference is [Build Scripts](../../BUILD-SCRIPTS.md); the [handover](../../handover/V4.10-BUILD-PIPELINE.md) preserves the subsequent performance roadmap. README, CLI help and development release notes match the implementation. Website additions are clearly labelled unreleased v4.10.

## Ordinary 10k paired measurements

Six balanced before/after pairs were run after compiler and consumer-suite activity stopped. These private fixtures use 10,000 plain tracked items, four workers, modified-mode invalidation and no templates/minification. Both binaries render the same content. The frozen official series was not rerun. Median child CPU and wall time:

| Operation | Before CPU ms | After CPU ms | CPU change | Before wall ms | After wall ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| `['build', '--all']` | 878.315 | 883.045 | +0.54% | 277.562 | 281.148 |
| `['build']` | 495.833 | 492.745 | -0.62% | 169.898 | 172.850 |
| `['status']` | 505.044 | 489.769 | -3.02% | 173.769 | 171.871 |

Deterministic status instruction counts: 1,699,997,648 → 1,708,492,037 (+0.500%). The retained implementation is neutral within these paired host measurements; no competitive benchmark claim follows. Earlier inline metadata/per-file directory-entry prototypes caused an unacceptable regression and were dropped. Optional metadata, name-only native enumeration, and the ordinary-record parse guard removed that tax.

## 10k graph scale and concurrency

| Shape | Edges | Wall ms | CPU ms | Scheduler plan ms | Peak ready | Peak active | Peak process RSS MiB | Wakeups | Queue lock wait ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| independent | 0 | 236.54 | 712.63 | 0.000 | 0 | bypass | 14.72 | 0 | 0.000 |
| chain | 9,999 | 804.18 | 892.47 | 1.781 | 1 | 1 | 18.09 | 29904 | 0.344 |
| star | 9,999 | 306.53 | 950.51 | 1.301 | 9,999 | 4 | 17.88 | 9 | 0.294 |
| tree | 9,999 | 311.78 | 947.08 | 1.618 | 3,253 | 4 | 17.84 | 12 | 0.295 |
| fan-in | 9,999 | 308.66 | 938.31 | 1.783 | 9,999 | 4 | 16.75 | 9 | 0.298 |
| layered | 990,000 | 752.63 | 1388.29 | 31.328 | 100 | 4 | 66.03 | 589 | 0.367 |
| sparse | 100 | 300.41 | 937.41 | 0.994 | 9,900 | 4 | 16.04 | 6 | 0.294 |

All seven shapes succeeded. Independent means no graph at all: its graph counters are zero, while ordinary execution remains configured for four workers. Chain serialization is required by its dependencies. Star, tree, fan-in, layered and sparse graphs reach four active workers. Layered has 100-item layers and 990,000 edges; sparse has 100 edges. A bounded two-worker barrier independently proves custom scripts overlap.

The opt-in statistics are absent from normal execution. `plan_ns` covers scheduler adjacency/indegree/ready initialization; it excludes loading, validation, closure and stale-selection work. RSS is the peak whole process from GNU time, not graph storage alone. Lock wait measures ready-queue mutex acquisition. Wakeups count blocking condition-variable resumptions. Each graph records a bounded chronological trajectory of `[completed, ready, active, elapsed_ns]`, available in [dag-performance.json](dag-performance.json); elapsed time begins at scheduler planning. These are local scale checks, not official performance scores.

## Certification

- Full `make test`, transformation/functional-truth/target checks and all language bindings: PASS. C# reports 29/29; other binding groups also pass.
- Final new black-box contract: 30 groups, both ordinary and ASan/UBSan/leak builds, PASS. Covers all failed phases, recovery, missing output, sidecar precedence/conflicts, imports/dependencies, target closure, order-only invalidation, same-pass hash/modified invalidation, alias freshness, graph mutation integrity, parallel barriers, 10k cycle diagnostics, watch reload and byte-preserving init.
- Lifetime sanitizer shallow corpus and file-buffer ownership guards: PASS. GCC and Clang first-party warning gates: zero warnings. Test-integrity scan: 309 files, zero findings; header collisions: none.
- Independent NRS: **94 modules PASS**, pinned revision `80ac41bfbeee7ca1bee4cd8cec2376cdf617b2e0`. Its former init-rerun rejection assertion was updated to successful byte-identical reruns; no assertion was relaxed to mask a failure.
- Independent PRS: **12 modules PASS**. Staged consumer prefix retains C ABI **1.3**.
- Website: full build, 19,028 rendered local references, shared desktop/mobile transformation navigation, canonical guide byte identity, agent-readiness sitemap, syntax highlighter and script-extension gate: PASS. Canonical full Build Scripts example executed successfully.
- Pagination trace ordering: PASS (`output 1 < cleanup 3 < info 4`). The initial sandbox failure was reproduced as a ptrace permission denial; the unchanged test passed with tracing permitted. It never represented flaky ordering behavior. Both the denied trace and successful receipt are retained.
- Boundary verification: all **81** frozen benchmark evidence files match their prior hashes; canonical Jsonic++ and Minify++ checkouts are clean, vendored copies unchanged, RuntimeValue layout/public C ABI headers unchanged. See [boundaries.json](boundaries.json).

## Hosted Windows fixture correction

Checkpoint 10 run `37954160154` failed the output-alias assertion on Windows.
The fixture treated MSYS Python's POSIX `os.name` as proof that POSIX symlink
creation was appropriate, despite running native Nift. MSYS links can be copies
or runtime-emulated links. The test now uses PowerShell to create a real Windows
symbolic link and verifies its ReparsePoint attribute and SymbolicLink type.
The original freshness assertions are unchanged, and the alias test now runs
on native Windows as well. The failed log is retained; an unchanged workflow
is not rerun to conceal the failure. Native and sanitized local contracts pass,
with the corrected Windows fixture requiring a new exact-head hosted cohort.

## Native Windows alias cache correction

The verified native-symbolic-link fixture subsequently exposed a production
failure in Checkpoint 10 run `37956192665`: the dependent read cached `seed`
instead of the producer's replacement `seedX`. The failed log is retained.
MinGW `std::filesystem::weakly_canonical` does not reliably follow native
Windows reparse points, as already accounted for by the filesystem containment
implementation. Build-cache identity now reuses that native handle-based
resolver on Windows. Both the alias read and completed-output invalidation
resolve to the same target. POSIX keeps weak canonical resolution; ordinary
no-dependency projects still bypass canonical cache keys.

The assertion is retained without skipping Windows. Local native pipeline,
filesystem-boundary, integrity and changed-source GCC/Clang warning checks
pass; lifetime certification is recorded alongside the correction. Final
acceptance still requires the new exact-head Windows and full hosted cohort.

## Current macOS C++17 warning correction

Gate 6A-R run `37958179092` passed Linux GCC/Clang sanitizer walls and Windows,
but current macOS libc++ rejected `shared_ptr::unique()` as deprecated in
C++17 under the existing `-Werror` policy. The copy-on-write check now uses
`use_count() != 1`, preserving the same ownership decision without suppressing
warnings. Native pipeline and integrity checks plus GCC/Clang warning checks
pass locally. The failed hosted log is retained; the new exact-head cohort must
pass the current macOS warning wall before acceptance.

## Hosted publication wall and stopping rule

Publish Nift main, the pinned NRS revision, and website generated main/source stage commits. Dispatch only the ordinary non-release walls: Hosted certification diagnostic; v4.4/v4.5 cross-platform; Test integrity; Init targets; Gate 6B bytes; Gate 6A-R libffi; build-only packaging; Performance regression; Checkpoint 10 cross-platform; Deep guards. Deep includes the exact 94-module NRS pin. No release or package submission is dispatched.

Final acceptance requires all ten workflows green on the exact published Nift head, plus the NRS repository consumer wall. Exact run IDs/URLs and final commits are written after publication to `.build/cp410-pipeline/final-hosted.json` and the user-facing final report. These receipts are generated separately so recording hosted results does not change the certified source head. If any wall fails, diagnose/fix and certify the resulting head; do not rerun an unchanged failed test to conceal instability.

Recommendation after those walls: **return to the held performance architecture roadmap, then STOP this tranche**. Sort/selector/capture/frame/retention remains the primary performance target. Traversal/glob, tiny-save lifecycle, filesystem wrappers, strings and other runtime costs follow. This report does not start them.

## Reproduction and artifact integrity

Build the accepted baseline in a separate checkout and place its binary at `.build/cp410-pipeline/nift-before`; build the retained candidate as `./nift`. From the Nift repository root, copy the evidence Python harnesses into `.build/cp410-pipeline/`, then run `baseline.py`, `performance.py`, `dag-performance.py`, and `profile-load.py` in that order. They create private temporary fixtures; they do not rerun the frozen official benchmarks. Audit scripts expect the sibling website source/generated checkouts. GNU time and Valgrind are required for RSS/instruction records.

Raw certificates, measurement rows, harnesses, page audit and binary identities are retained here. [SHA256.json](SHA256.json) covers all raw artifacts; report text is excluded from that manifest to avoid a self-hash.
