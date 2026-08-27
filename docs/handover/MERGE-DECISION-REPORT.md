# Merge decision: Embedded Nift and canonical Nift

Status: **analysis, measurement and recommendation only** (CP20). Canonical Nift
was not modified. The outcome is open; this document does not prejudge it.

Evidence sources: the three repositories at their current heads
(`nift-embed fb8597f`, regression suite `fdf35e7`, `nift-rs 37db797`), the
completed CP10-CP19 engineering record, the 500-pass warm-baseline campaign, the
event-faithful anomaly investigation, and fresh before/after measurements taken
for this report (section 4).

---

## 1. Product identity and user understanding

**Does Embedded Nift belong to Nift's identity, or is it a related product
built from Nift technology?**

The evidence points to a *related product built from Nift technology* rather
than a natural extension of the CLI's identity:

- Nift's canonical identity is "a fast static-site templating and build tool":
  content directories, templates, `@`-directives, pagination, incremental
  builds, deterministic output, a single fast binary.
- Embedded Nift is *the same parser/evaluator exposed as a request-time
  library*. It is genuinely "Nift semantics at request time", but the consumer,
  the failure model, the lifetime model (Context, engine/context lifecycle,
  concurrency, deferred destruction) and the packaging surface (a C ABI plus
  five language bindings) are library concerns the CLI never had.
- They share the language (template/directive semantics) but differ in the
  *product* (build tool vs rendering library).

**Would merging strengthen "Nift is a fast templating and build tool"?**

It strengthens *"Nift is one templating semantics everywhere"*, which is a
coherent message, but it risks blurring the primary message: the CLI's own
marketing ("fast build tool") is diluted if the first thing a visitor sees is
also a server-rendering library. The message must remain "one semantics; two
delivery modes; the library is optional."

**Could it confuse users into believing applications with backends need
Embedded Nift?**

Yes, if the merged repository presents Embed as a headline feature. The
established positioning boundary is:

> Embedded Nift is optional request-time rendering infrastructure. Applications
> that need a backend can continue using ordinary Nift-generated assets with
> JSON APIs, WebSockets or any backend framework. A backend does not require
> Embedded Nift.

This is only sufficient if the merged *structure* matches the message: Embed
must be visibly an optional component (separate install/package/headers), not
something pulled in by the CLI. A merged repo that builds the library and
bindings from a separate optional tree satisfies this; a merged repo that
splices binding build steps into the CLI `all` target contradicts it.

**Can the documentation clearly establish three equally legitimate
architectures?**

Yes - and this is the decisive documentation requirement regardless of
repository outcome:

1. Nift builds static assets; a backend supplies JSON/WebSocket APIs consumed
   by frontend JavaScript.
2. A backend uses Embedded Nift to render HTML at request time.
3. A mixed application uses static Nift pages, JSON-driven interactivity, and
   Embedded Nift only for selected routes.

---

## 2. Real-world architectural value (SSR vs JSON + frontend rendering)

### Where request-time server rendering (Embedded Nift) wins

| Application | Why SSR wins |
|---|---|
| Marketing/content sites, landing pages, docs | Initial-page completeness; link previews/OG tags require server-rendered HTML; SEO; no-JS operation. |
| Public listing/catalogue pages with light interaction | Fast first useful content; HTML cached at CDN/edge (no client JS needed to see content). |
| Articles, blogs, long-form content | Accessibility and readability without a client bundle; predictable time-to-content. |
| Personalized landing pages (a/b, geo, cohort) | Server can personalize first paint; client gets the personalized HTML immediately rather than after data round-trips. |
| Email-unsubscribe / confirmation / tokenized pages | One server render, no SPA plumbing, no duplicated client logic. |
| Low-bandwidth / low-end devices | Small or no client bundle; rendered HTML is the payload. |
| Offline-tolerant server-generated content | Caching the rendered HTML (full-page/ESI-style) reduces origin load. |

Benefits concentrate on: initial-page completeness, SEO/link previews,
accessibility, client bundle size (near zero), caching of final HTML, time to
first useful content, personalization at first paint, and reduced duplicated
rendering logic (one templating language instead of template + client-side
re-implementation).

### Where JSON + frontend rendering wins

| Application | Why client-side wins |
|---|---|
| Highly interactive authenticated dashboards | Dense interaction (filters, drag, live tables); re-rendering a page per click is worse than diffing state. |
| Realtime/chat/kanban/editor tools | Server-push updates; client owns the DOM/state model. |
| Client-heavy apps (SPAs, mobile-web hybrid) | Shared model with mobile clients; persistent session without page reloads. |
| High server-cost-sensitive, low-CPU apps | Offloading rendering to the client saves server CPU at scale (the server's JSON is cheaper than per-request HTML). |

Benefits concentrate on: interaction density, realtime responsiveness, server
CPU cost at very high interaction rates, and offline-first client behavior.

### Where a hybrid wins

Static Nift pages for public/marketing routes + JSON API for authenticated
interactive areas + Embedded Nift only for personalized/SEO-sensitive routes
(unsubscribe, previews, per-user landing). This is the strongest message: each
architecture is legitimate and the boundary is chosen per route, not forced by
the tooling.

**Conclusion: SSR is not universally better.** The correct framing is "one
templating semantics; choose the delivery mode per route." Embedded Nift is
valuable precisely for the routes where server-rendered HTML is the right
choice; it is not a replacement for client-rendered interactive apps.

---

## 3. Merge value versus separate-project value

**Benefits of merging:**
- One repository for all Nift semantics: CLI, embedding core, bindings,
  tests, docs. Contributors find everything in one place.
- Prevents *silent* semantic drift between the CLI and the embed library: a
  single source tree, single parser, single conformance corpus makes drift a
  build-time/CI failure rather than a subtle behavioral gap.
- One version number can describe the shared templating semantics.
- The regression suite becomes the canonical, shared gate for both.

**Costs/risks of merging:**
- Repository surface expands: 78 Makefile test targets, five bindings, a C ABI.
- Release coordination gets heavier: a CLI patch release is coupled to binding
  changes unless the packaging model isolates them.
- CI duration and complexity grow; a binding failure can look like a Nift
  failure unless gates are separated.
- Contributor onboarding: a "fix the CLI" contributor is confronted with the
  whole embedding stack.
- Ownership boundary blurs: who owns bindings vs the core?

**Can the same semantic consistency be achieved without merging?**

Yes - if a *separate* embedding repository consumes a **versioned canonical
core** and the shared conformance corpus is run against both. The conformance
suite already runs a 36-case corpus across seven adapters; that is the
semantic-enforcement mechanism. Versioning a canonical core (the parser
semantics contract) and pinning the embed repo to it provides drift protection
without a single repository. This is weaker than single-tree sharing (a change
to the core can temporarily break the embed repo until its pin bumps) but it is
achievable and is exactly the model the independent Rust conformance
implementation already uses.

**Is Embedded Nift valuable as a feature of Nift, or primarily as a separately
packaged library using Nift semantics?**

Primarily as a *separately packaged library using Nift semantics*. Its users
are backend developers choosing request-time rendering; they install a
language package (C++/Go/C#/Node/Python), not the CLI. The value is the
semantics, delivered through a library. The CLI and the library share
semantics; they do not share consumers.

**Would separate versioning let each product evolve more safely?**

Yes: the CLI can move on its own cadence (CLI-only fixes, packaging) without
binding releases, and the embed library can evolve its ABI/API with its own
major version. Shared-semantics compatibility is enforced by the conformance
gate, not by a shared release train.

**Which arrangement creates the clearest ownership and contribution
boundary?**

Separate repositories (or a merged repo with strictly separated ownership
areas) create the clearest boundary. A merged repo with clear per-area
ownership (core/CLI owners vs bindings owners) is a close second and has the
drift-prevention advantage.

---

## 4. Cost to ordinary Nift users (measured)

Fresh measurements on this machine (linux/amd64, g++ 14, `-O2`):

| Metric | CLI-only link | Full CLI (includes embedded engine) | Delta |
|---|---|---|---|
| Binary size | 1,256,712 B | 1,380,240 B | **+123,528 B (~9%)** |
| Startup (`--version`, 200x median) | ~1.3 ms/invocation | ~1.3 ms/invocation | identical |
| Clean build `make -j2 nift` (3x) | n/a (reduced set) | 16.1 / 16.6 / 16.9 s | embedding-only objects = ~4 s of that |
| `make libnift_c.so` | n/a | ~15.5 s | **compiles 15 PIC objects + shared link** |

CORRECTION: the earlier report described the additional native-library build as
"link-only". That is wrong. `make libnift_c.so` (measured ~15.5 s) compiles the
full source set with `-fPIC` (15 PIC objects) and then links the shared library;
it is not merely an archive/link step. The embedding-only objects
(`Engine.cpp`+`Context.cpp`+`c_abi.cpp`) take ~4 s of a full CLI build, so a
CLI-only build saves ~4 s of compile time.

Rigorous reduced-CLI verification (CLI linked without `Engine.o`, `Context.o`,
`c_abi.o`):
- **Regression:** the reduced binary passes the complete CLI regression surface
  (parser/content, commands/grammar, comments, contracts, JSON + JSON-Schema,
  pagination + ordering, template-optional, requirements, metadata-safety,
  path-safety, init-targets, control-flow, cross-feature, config-validation,
  diagnostics, console, minify, incremental modes, zero-mutation, repair
  campaign, ownership/concurrency, incremental state-transition adversarial,
  parser/value composition adversarial, complexity invariants).
- **Behaviour:** building the same multi-page paginated project with the full
  and reduced binaries produces **byte-identical output** (same file list, same
  file contents; only the directory mtime tar entry differs).
- **Performance:** 200x `--version` median identical (~1.3 ms/invocation).
- **No CLI command relies on Engine/Context/C ABI:** the reduced binary links
  and all CLI commands/tests pass; `nm` on `nift.o`/`CLI.o` shows no references
  to those classes at the command layer.
- **Cross-platform linking** (Windows/macOS) cannot be validated on this Linux
  host; the reduced object set is portable C++17 and the CI matrix must verify
  it per-platform. Note: `c_abi.cpp` contains the exported C symbols; a CLI-only
  target simply omits that object - no symbol collision.

Other observations (unchanged):
- The embedded engine is already linked into the CLI because the CLI and the
  engine share the parser and project semantics. Marginal cost to an ordinary
  CLI user: ~120 KB binary, identical startup, no additional runtime deps
  (`ldd nift` = libstdc++/libm/libgcc/libc - the same set as a CLI-only build).
- Embedding adds no runtime/build deps to the CLI beyond g++. Binding toolchains
  (Go/.NET/Node/Python) are needed only when building that binding.
- **Isolation requirement:** in any merged layout the CLI target must link only
  the core objects it needs and must NOT require the bindings or the C ABI to
  build. The ~120 KB is an already-incurred semantic-sharing cost; a merge does
  not increase it if targets stay separated.

**Conclusion:** ordinary CLI users pay a small, already-incurred, measurable
cost (~120 KB, no deps). A merge does not increase it provided the CLI remains
an independently buildable target.

---

## 5. Build and packaging boundaries

The merged repository must be able to guarantee:
- the CLI is independently buildable (`make nift`, g++ only);
- the native embedding library (`libnift_c.a`/`.so`) is optional;
- language bindings are optional (Go/.NET/Node/Python toolchains only needed
  for the binding being built);
- distributors can package the CLI without any binding toolchain;
- failure in one binding does not block an unrelated CLI package (separate CI
  gates per binding);
- users can install the CLI without embedding headers/libraries (separate
  install rules for `include/nift/`, `libnift_c.*`);
- users can install one binding without receiving irrelevant bindings
  (per-binding packages: `nift-cpp`, `nift-go`, `nift-csharp`, `nift-node`,
  `nift-python`);
- release artifacts and versions remain comprehensible (one core version; each
  package is a thin wrapper with its own semver).

### Four options compared

| Option | CLI build | Embed lib | Bindings | Install isolation | Drift risk | CI |
|---|---|---|---|---|---|---|
| 1. One repo, one always-built product | coupled | always | coupled | poor | low | one gate |
| 2. One repo, optional independently packaged targets | isolated (`make nift`) | optional target | per-binding optional targets | strong | low (shared tree) | per-target gates |
| 3. Separate repos sharing a versioned canonical core | isolated | separate repo | separate | strongest | medium (pin bumps) | per-repo gates |
| 4. Canonical Nift contains the native core; bindings separately packaged/maintained | isolated | in canonical (optional) | separate repos/packages | strong | low for core, medium for bindings | per-area gates |

Option 2 (one repo, optional independent targets) delivers the isolation
benefits of separation while retaining single-tree semantic sharing. Options 3
and 4 trade some drift protection for cleaner ownership. Option 1 is not
acceptable for the CLI user base.

---

## 6. Maintenance and reliability

| Concern | Merge (single tree) | Separate (versioned core) |
|---|---|---|
| Release cadence | one core version; per-package wrappers | independent per product |
| Version compatibility | semantic core pinned together | core version pinned by embed repo |
| API/ABI evolution | single C ABI versioning in-tree | embed repo owns its ABI major |
| Regression-suite ownership | canonical shared suite in one repo | suite runs against both checkouts |
| CI duration/complexity | grows (bindings + C++ in one workflow) unless gated | per-repo CI, smaller jobs |
| Contributor onboarding | heavier surface | focused surfaces |
| Platform support | one matrix | each repo its own matrix |
| Vulnerability response | one tree to patch | two trees (semantics core + wrapper) |
| Semantic conformance | enforced by shared suite in-tree | enforced by suite run against both |
| Risk of weakening CLI while changing embed | present; mitigated by separate CLI targets/gates | low (embed changes cannot touch CLI tree) |
| Risk of standalone vs embedded drift | low (shared parser) | medium unless conformance gate is strict |

Merging *concentrates* reliability evidence (one tree, one suite, one shared
parser) but it creates a larger coupled repository whose CI must be carefully
gated so a binding failure cannot masquerade as a CLI failure. The reliability
benefit is real but conditional on target separation.

---

## 7. Lessons from the Rust experiment

The independent Rust implementation (jsonic-rs/minify-rs/nift-rs) was built
from the behavioural contract, not the C++ source, and exposed the following:

| Lesson | Learned | Classification |
|---|---|---|
| Accidental C++ assumptions (e.g., `std::filesystem` behaviors, exception use) | Semantics must be specified behaviourally, not by implementation | Already incorporated into canonical C++ (conformance corpus); **evidence for conformance gate, not necessarily merge** |
| Portable semantics (separator handling, JSON edge cases) | Two implementations forced the corpus to be byte-exact | Already incorporated (corpus); **evidence for keeping implementation boundaries separate** |
| Ownership/lifetime (render result vs engine lifetime) | A library result must own its data independently of the engine | Already incorporated into C++/bindings (CP13/CP17 lifetime work) |
| API shape (explicit page/path/text) | Ambiguity must be designed out, not resolved by dispatch | Already incorporated into canonical API (CP19 `render`/`render_path`/`render_text`) |
| Error families (unknown page vs missing path vs render error) | Errors must identify the logical source | Already incorporated into canonical (CP10 error model); reinforced by CP19 |
| Concurrency (concurrent renders, deferred destruction) | Lifecycle must be explicit and testable | Already incorporated into canonical (CP7a/CP17) |
| Project-aware rendering (tracked snapshot) | Project state must be immutable/snapshot-scoped | Already incorporated (PA3/PA4) |
| Source/path/text distinctions | Never infer source kind from the filesystem | Incorporated (CP19); **should be enforced by the conformance gate for any future surface** |
| Conformance testing itself | Two implementations + one neutral corpus catches real drift | Incorporated as the suite; **evidence for the shared regression suite as the semantic anchor regardless of repo outcome** |

Classification summary:
- Incorporated into canonical C++: most lessons (portable semantics, lifetime,
  API shape, error families, concurrency, project-aware rendering).
- Should be incorporated before any merge: none outstanding (CP19 API shape is
  the last required one).
- Relevant only to bindings/conformance: source/path/text conformance
  enforcement, per-binding gates.
- Evidence in favour of merging: single shared parser prevents drift; the
  conformance suite already proves single-tree sharing works.
- Evidence in favour of keeping implementation boundaries separate: two
  independent implementations (C++ canonical + Rust conformance) produced a
  stronger corpus and caught C++-specific assumptions; a second production
  engine is explicitly out of scope, and the Rust experiment's value came from
  *independence*, not co-location.

Established direction retained: Rust is an experimental independent conformance
implementation, not a second production engine.

---

## 8. Required comparisons (evidence-backed)

| Comparison | Required conclusion |
|---|---|
| Canonical Nift before vs proposed merged state | +~120 KB binary for the CLI, ~3 ms startup unchanged, no new deps, link-only build-time delta. A merge with separated targets does not change these numbers. |
| Merged vs separate repository | Merged: lowest drift risk, heavier onboarding/CI. Separate: cleaner ownership, per-product versioning, drift guarded by the shared conformance corpus. |
| Static Nift + JSON frontend vs Embedded SSR | Each wins for distinct route classes (sections 2); neither is universally better; hybrid is legitimate. |
| Optional vs always-linked component | CLI-only binary is 1,256,712 B; with engine 1,380,240 B. The engine is already in the binary; keep it an optional *target*, not an optional *link decision* that forks semantics. |
| Bindings together vs independently packaged | Per-binding packages (option 2/4) are required so a user installing `nift-python` does not receive Go/.NET tooling and a CLI package never requires bindings. |
| Public positioning before vs after merge | The "optional request-time infrastructure" boundary must be matched by structural isolation (optional targets/packages), or users will read the merged repo as "Nift needs a backend." |
| Canonical C++ vs experimental Rust | Rust lessons are largely incorporated; the strongest remaining signal is that independent implementation + shared corpus is the drift-proofing mechanism - an argument for the conformance model, not for merging Rust into canonical. |

---

## 9. Recommendation

**Recommended outcome: Option 2 - one repository with optional, independently
packaged targets** (equivalently described as "merge as a shared repository
with strict target/package isolation"), OR the conservative variant: **defer the
final packaging decision until the packaging/website checkpoint**, keeping the
codebases as they are (already single-tree) while codifying the isolation
guarantees.

The concrete, defensible position: the code already lives in one repository
and shares one parser; the semantic-drift benefit is already realized. What
must be decided is *packaging/ownership*, and the evidence says that is best
served by per-product packages (CLI; C++/C-ABI library; each binding) with the
shared conformance corpus as the semantic anchor, whether in one repository or
across separate ones.

The single most important structural requirement in every outcome: **the CLI is
an independently buildable, independently packaged target; embedding and every
binding are optional; and the shared regression suite is the drift gate.**

If the decision must be binary between "merge the complete implementation now"
and "keep separate": the evidence favors **keeping the embedding product
separately packaged while sharing the canonical semantics core** (option 2 or
4), because the primary consumers differ and separate versioning serves both
better, with drift controlled by the conformance gate rather than by a shared
release train.

1. **Recommended outcome:** one repository, optional independently packaged
   targets (CLI, native library, each binding); conformance suite is the
   shared drift gate. ("Merge" means *source integration into one tree with
   isolated build targets and per-package release ownership* - it does NOT mean
   one always-built product or every binding building from the CLI target.)
2. **Strongest argument for:** single shared parser prevents semantic drift
   between the CLI and request-time rendering, and the conformance corpus
   already proves the shared-semantics model works at 36/36 x 7 adapters.
3. **Strongest argument against:** the consumer surfaces genuinely differ
   (build-tool users vs backend-library users); a merged tree couples releases,
   CI, and onboarding unless isolation is rigorously enforced.
4. **Evidence that would change the recommendation:** a demonstrated failure to
   keep CLI packages independent in a merged tree (binding toolchain leaking
   into CLI builds), or evidence that separate repos cause drift the conformance
   gate cannot catch (a case where the neutral corpus passes but behaviors
   diverge).
5. **Remaining uncertainties:** exact packaging/website final shape (deferred);
   whether per-binding CI gate isolation can be maintained long-term in one
   repo; distributor behavior for optional native libraries.
6. **Effect on ordinary CLI users:** ~120 KB binary (already incurred), no
   new dependencies, no startup cost; unchanged or improved clarity if the CLI
   package never requires embedding headers.
7. **Effect on embedding users:** per-binding packages with their own semver;
   the native library optional and versioned; a stable C ABI as the
   ownership-explicit boundary.
8. **Proposed repository/package layout (option 2):**
   - `src/`, `include/` - shared core (parser, project semantics).
   - `cli/` (or existing `nift` target) - CLI, g++ only, independent target.
   - `embed/` (native library + C ABI) - optional target `libnift_c.a/.so`.
   - `bindings/go|csharp|node|python/` - per-binding optional targets/packages.
   - `tests/` - CLI tests; `tests/conformance` + suite - shared corpus.
   - Packages: `nift-cli` (g++ only), `nift-cpp`/`nift-cabi` (headers+lib),
     `nift-go`, `nift-csharp`, `nift-node`, `nift-python` (per-binding).
9. **Versioning and release model:** one core semantics version (the shared
   corpus + C ABI major); each binding a thin wrapper with its own semver; CLI
   independent; per-package CI gates so one binding cannot block the CLI.
10. **Documentation/positioning model:** "one templating semantics, two
    delivery modes, library optional"; the three legitimate architectures
    (static + JSON frontend; backend SSR; hybrid) documented up front.
11. **Migration and rollback plan:** this checkpoint changes no code; a future
    packaging step is additive (new package targets), the CLI behavior and
    corpus are unchanged, and the current single-tree layout is the rollback
    state.
12. **Confidence level:** Medium-high for the packaging/isolation model (it is
    directly supported by the measured 120 KB/deps-free result and the existing
    conformance gate). Medium for the "merge vs separate" binary because the
    strongest differentiators are organizational (release/CI/ownership
    discipline) rather than technical, and the current single-tree reality
    already captures the main technical benefit.

---

## 10. Stop boundary

This checkpoint produced analysis, measurements and a recommendation only.
No canonical Nift modification, history merge, repository move, package
publication, or website/release work was performed. The next decision point is
the packaging/website checkpoint, gated on this report.
## CP20 design review (2026-08-27) - assessment of the merge + distribution proposal

The proposal (canonical source integration + strict build/package/documentation
isolation) is endorsed with the following structural refinements:

- **Adopt directory-based isolation** (`src/embed/` for Engine/Context/C ABI) so
  the CLI target's object filter is a directory glob, not a fragile filename
  list, and the boundary is visible in the layout.
- **Name the CLI target explicitly**: `make nift` builds the REDUCED CLI object
  set; CI must test that reduced binary (a full-object `nift` is a development
  convenience, not the shipped artifact).
- **Makefile**: `make` == `make nift` (CLI only). `make embed` = native library
  (libnift_c.a/.so + headers + pkg-config) in one target - do NOT split C++ API
  vs C ABI into separate targets. `make go-binding|csharp-binding|node-binding|
  python-binding`, `make bindings`, `make test*` mirrors, `make install` =
  CLI-only, `make install-embed` = native headers/libs.
- **Synchronized versions**: all packages carry the canonical Nift version;
  simplest workable policy = republish every binding on every release (patch
  churn acceptable); fall back to minor-sync + patch-on-change only if patch
  cadence becomes high.
- **CI model**: a single workflow always runs `cli` (reduced target) + 
  `conformance` (shared corpus) to avoid path-filter false-green gaps; binding
  jobs run on shared-core or binding-local paths; packaging jobs separate.
- **ABI contract is the biggest technical caveat**: co-locating the C ABI in
  the canonical repo makes the canonical parser/engine's ABI a public contract;
  the report must add an explicit C-ABI-major policy and treat binding
  conformance as blocking for shared-core changes.
- **Underspecified before merge**: which CLI becomes canonical (nift-embed's
  v4.0.7 shares the parser; the pre-Embed reference must be reconciled or
  explicitly superseded); the regression suite's home (recommend the shared
  corpus lives in canonical `tests/conformance/`); history-integration
  mechanics (recommend `--allow-unrelated-histories` subtree-style move + a
  read-only archived nift-embed with a redirect README; never rewrite/delete);
  Windows .lib/.dll + Node/Python native matrix verification on CI.

Strongest objection: the public-ABI + binding-conformance coupling inside the
canonical repo is the only mechanism that can slow ordinary CLI development; it
is acceptable if the ABI-major policy and per-target CI gates are explicit.

## 11. Settled architecture (accepted 2026-08-27)

One canonical Nift repository containing the CLI, shared templating
implementation, native Embedded Nift API/C ABI and production bindings, with
strict build, installation, packaging, CI and documentation isolation.
`nift-embed` does not remain an active duplicate C++ tree. `nift-rs` stays the
independent experimental/conformance implementation. Source integration does
not make Embedded Nift mandatory and does not turn Nift into a backend
framework.

Fixed boundaries:
- `make` / `make nift` build only the ordinary reduced CLI; `make embed`,
  `make go-binding|csharp-binding|node-binding|python-binding`, `make bindings`,
  `make nift embed bindings` are explicit. `make test*` mirrors. `make install`
  installs only the CLI; `make install-embed` installs the native
  headers/libraries/pkg-config.
- CLI CI tests the reduced CLI binary (embedding-only objects excluded).
- Native install via a dedicated `install-embed.sh` (checksum-verified, per-user
  default + `--system`, never silently building/downloading unverified
  artifacts) and a PowerShell/ZIP path for Windows.
- Go uses the separately installed native library (pkg-config + `NIFT_NATIVE_LIB`
  override), never bundled; ABI-major compatibility check at engine init.
- Initial native matrix: Linux glibc x86-64/arm64, macOS x86-64/arm64, Windows
  x86-64. No initial musl prebuilts; source build is the fallback.
- C# NuGet RID assets (linux-x64/arm64, osx-x64/arm64, win-x64); Node N-API
  prebuilts + source fallback; Python abi3 if straightforward else per-CPython
  wheels + sdist.
- Synchronized versions: every published component carries the canonical Nift
  version; simplest policy publishes all required components each release
  (incl. patches); registry prerelease syntax may differ mechanically.
- Documentation: homepage and primary install pages untouched; a dedicated
  Embedded Nift section (own install pages per language, API refs,
  lifecycle/concurrency, examples/dogfoods, deployment, SSR-vs-other
  architecture guidance) reached by a restrained nav link.
- GitHub Releases: CLI binaries + SHA256SUMS primary; optional native embed
  bundles per target; bindings live in their ecosystems (Go module, NuGet, npm,
  PyPI); Releases are not a second registry.
- Release publication: build once, test fully, record hashes/provenance, draft
  release, attach verified CLI+native artifacts, publish each language package
  via independent idempotent jobs, retry only failed jobs, finalize only when
  all required components are accounted for. "version already exists" is
  success only if the existing package matches expected version/content/digest/
  provenance; otherwise a fatal version-collision error.
- CI categories: CLI; native embed/C ABI; shared conformance; per-binding
  correctness; packaging smoke; release packaging; sanitizer/fuzz/lifetime.
  Shared parser/project/API/C ABI changes run all binding conformance gates;
  binding-local changes run CLI/core sanity + conformance + that binding.
  Unknown/new shared paths default to the complete matrix (no silent skip);
  the path-classification mechanism has an integrity/liveness guard. Release
  candidates always run the complete matrix.
- Regression-suite ownership: neutral conformance corpus + direct runners move
  into canonical `tests/conformance/`; heavy campaigns/perf harnesses/
  orchestration stay in the suite repo; release validation pins the suite
  commit.

## 12. Pre-integration plan

See docs/handover/INTEGRATION-PLAN.md. Verified: merge base 8a818f2 == canonical
main; 0 canonical-only commits; embed is a strict descendant; integration is a
fast-forward; embed `src/` is a strict superset of canonical `src/` (no
canonical-only CLI behaviour discarded). C ABI policy, Makefile/directory
layout, reduced-CLI target, conformance ownership, validation matrix and
rollback plan are specified there. NOT EXECUTED - awaiting authorization.

## 13. Pre-integration plan revision (2026-08-27)

Revision to sections 1 and 12 per review:
- **Narrowed ancestry claim**: shared ancestry + file presence prove no
  canonical file disappears and zero canonical-only commits prove canonical
  holds no own changes; behavioural preservation is established by the complete
  reduced-CLI regression/equivalence matrix (not asserted from ancestry alone).
- **Reconciled C ABI policy**: additive backward-compatible -> ABI minor bump;
  breaking -> ABI major + Nift major; patch-level no-ABI changes -> no ABI
  version change; bindings require matching ABI major + feature availability,
  not exact patch equality. Symbol rule corrected to "retain existing exported
  symbol names and signatures within an ABI major" (no source-ordering
  significance). Public structs are finalized before first publication; later
  `size`-field insertion is NOT described as automatically backward compatible.
- **Rollback revised**: pre-integration backup ref (`pre-embed-merge-v4`) at
  the old head; validate-before-push and fix-forward-after-push are the primary
  strategy; force-push is an explicit emergency action requiring separate
  authorization, never routine/pre-authorized. `nift-embed` unarchived until
  post-push validation completes.
- **Immutable integration target**: exact reviewed commit SHA
  `7d5482ef960076adeb34e3fabd77219009c95de7` (re-verified at integration time);
  preflight re-checks canonical == 8a818f2, reviewed descendant, merge base,
  zero canonical-only commits, clean trees.
- **Publishing design**: `release.yml`, environment `release` only on
  external-publishing jobs, job-level `permissions: {contents: read,
  id-token: write}` only there; separate dependency-gated idempotent jobs for
  PyPI `nift` and NuGet `Nift` (NuGet incapable of other package IDs) after
  full validation + package smoke; "version already exists" success only on
  exact content/digest/provenance match, else fatal collision.
- **Phased sequence**: freeze/verify SHAs; backup ref; fast-forward in a
  disposable canonical clone; full validation matrix; push only if green;
  rerun canonical CI; structural isolation commit only after the fast-forward
  state independently passes; build + install smoke without publishing; return
  for publication authorization; archive nift-embed only after separately
  authorized completion.
- **Package identities** proposed: PyPI `nift`; NuGet `Nift` (owner
  antimatroid, restriction to exact ID); npm `nift` (first publish claims);
  crates.io NOT in the production set (Rust = conformance); Go `nift.dev/embed`
  via `bindings/go/v4.x.y` submodule tags; native C/C++ via GitHub Release
  bundles + checksum-verified installer.

Full detail in docs/handover/INTEGRATION-PLAN.md (revised). Canonical `nift`
remains untouched at 8a818f2; nothing published, merged, moved or archived.
