# Embedded Nift — architectural programme handover

This fork (`nift-embed`) is the experimental workspace for making the Nift C++
implementation usable as an embeddable rendering engine while keeping the
existing CLI on the same implementation. The production Nift repository is the
authoritative release line; this fork may restructure aggressively as long as
each checkpoint is behaviour-preserving and individually reviewable.

## Goal

One Nift template language, one semantic implementation, one parser/rendering
core, multiple ways to use it:

```text
                    shared Nift core (Parser + json::Document value model)
                         │
             ┌───────────┴───────────┐
             │                       │
         Nift CLI               Embedded Nift (nift::Engine)
      project/build use          runtime/library use
```

The long-term architecture is one canonical C++ Nift core consumed through
idiomatic ecosystem bindings, plus a single independent implementation
experiment:

```text
C++ Nift core (shared parser/evaluator)
    │
    ├── Nift CLI
    ├── C++ Embed (nift::Engine)
    ├── thin ecosystem bindings (Node/JS, Python, Go, C#, Ruby, PHP, ...)
    │        same parser/evaluator underneath; native-feeling APIs on top
    └── Rust independent implementation (portability/conformance experiment)
```

Bindings mean a bug fix in Nift fixes every language simultaneously; the
bindings do not maintain their own parser/evaluator. Rust is the deliberate
independent implementation used to validate portability and conformance (and a
thin Rust binding to the canonical core remains useful as a reference/oracle).
A stable, minimal C ABI is the likely common binding boundary. A consequence of
this work is a formal distinction between genuine Nift contracts and C++
implementation details.

## Project-attached rendering (recorded requirement)

"Embedded Nift must not fake tracked pages" means the Engine must not **invent**
tracked-page state when none is supplied. It does **not** mean Embedded Nift
should be incapable of using real Nift project state.

An Engine associated with an actual Nift project should eventually be able to
consume that project's `.nift/tracked.json` and obtain the same named-output
semantics for `@pathto` as the CLI:

```text
standalone Engine
    -> no tracking assumed; @pathto uses concrete-project semantics

Engine attached to Nift project/tracking
    -> real tracked-name resolution
    -> normal @pathto semantics
```

This is the preferred path for SSR/server integration and the eventual Node
binding (e.g. `new Engine({ root: "." })` discovering the project's tracking) —
and is better than an arbitrary `set_path_resolver` callback for the normal
case, because the project already knows what its names mean; the application
should not have to explain Nift back to Nift.

The host seam that makes this possible is `RenderHost::tracked_output_path()`:
a project-backed Engine host would implement it by loading `.nift/tracked.json`
(tracked name -> generated output + index flag), exactly as `ProjectInfoHost`
does today. Not implemented in CP6; recorded here so the abstraction is not
accidentally closed off.

### Render by tracked page name (explicit requirement)

A project-aware Engine should be able to render a tracked page by name:

```cpp
RenderResult render(std::string_view page_name);
RenderResult render(std::string_view page_name, const Context& context);
```

Semantics: look up the page in the attached project's `.nift/tracked.json`,
find its content/template/output metadata, render through the same shared core,
and return the result. `@pathto("about")` then works naturally because the
Engine instance holds the real tracked-name -> output mapping; `@input`,
contracts, dependencies and runtime values compose through the existing core.

Hierarchy (the common case is tiny; low-level control remains):

```text
High:   render("about")
Medium: render("about", context)
Low:    render(page_source, template_source, context)
```

Target DX (eventual Node binding):

```js
const nift = new Engine({ root: "." });
app.get("/about", (req, res) => res.send(nift.render("about")));
app.get("/user/:id", async (req, res) =>
    res.send(nift.render("user", { user: await db.user(req.params.id) })));
```

Not implemented yet; recorded as an explicit requirement. A project-backed
Engine host plugs into the existing `RenderHost` seam (loads `.nift/tracked.json`
and `.nift/config.json`, supplies `content_path`/`output_path`/
`tracked_output_path`/loaders/contracts), and `render("about")` is then
orchestration over the same `render_composed` used by `render(page, template)`.

## Project-aware Engine programme (approved with amendments, PA1 done)

The architecture was approved in principle with six amendments folded in below.
Do **not** let the project-aware Engine depend on mutable `ProjectInfo`; it
consumes a read-only snapshot and never becomes the build system.

```text
.nift/config.json ─┐
                   ├──► ProjectState (read-only: config + tracked pages + path geometry + read caches)
.nift/tracked.json ┘                          │
                                              ▼
                                         ProjectHost : RenderHost
                                              │
                     Engine(path) ────► render("page-name"[, context])  ──► existing shared core
```

### Programme (PA1–PA6)

- **PA1 read-only ProjectState** — DONE. `src/ProjectState.{h,cpp}` mirrors
  ProjectInfo's read semantics exactly (config + tracked validation, name
  registry, path geometry, shared read caches) but owns no build/write/watch/
  hash machinery and returns errors instead of printing. Never writes.
  `open()` is transactional: on failure the object is left empty/unopened (no
  partial config/tracked registry observable; direct sequence regressions for
  malformed config, valid-config+malformed/missing tracked, success-then-
  failure, failure-then-success). Parity evidence:
  `tests/project_state_parity.cpp` (valid parity vs ProjectInfo, ~28 invalid
  accept/reject parity cases, zero-write guarantee on success and failure,
  concurrent shared reads).
- **PA1b semantic convergence** — DONE. The duplicated project-read authority
  is removed: `src/ProjectRead.{h,cpp}` is now the **single implementation** of
  Nift project-read semantics — config parsing/validation, tracking
  parsing/validation, tracked-name rules, and path geometry — consumed by both
  `ProjectInfo` and `ProjectState`. `ProjectInfo::load_config`/`load_tracking`
  delegate to it (adding only the CLI console-error prefix), and
  `ProjectInfo`'s geometry methods delegate to `project_read::*`. Nothing moved
  downward: build/write/watch/hash ownership remains entirely in `ProjectInfo`;
  `ProjectState` still owns no write/build machinery and remains zero-write.
  The parity harness stays permanently as regression evidence.
  ```text
               shared project-read layer  (ProjectRead)
                /                  \
               /                    \
      ProjectInfo                 ProjectState
    CLI build/write                SSR read-only
  ```
  Evidence: build clean (no warnings); `test-project-state` parity corpus green;
  all six engine tests + public-header probe; CLI `test-config-validation`
  (`unknown config key '<name>'`), `test-contracts`, `test-minify-integration`
  (exact error-text greps), `test-content`, `test-pagination`,
  `test-requirements`, `test-path-safety`, `test-metadata-safety`,
  `test-template-optional`, `test-pathto-404`, `test-output-permissions`,
  `test-pagination-equivalence` (18 comparisons) and
  `test-incremental-state-transitions` (modified/hash/hybrid) all pass.
- **PA2 ProjectHost** — DONE. `src/ProjectHost.h` is a per-render `RenderHost`
  adapter over the read-only `ProjectState` snapshot (no engine/public API yet).
  It supplies the existing Parser with project-backed content/template/input
  loading (`read_shared_source`), JSON loading (`read_shared_json`), contracts
  (`is_contract_name`/`contract_source`), tracked output lookup
  (`tracked_output_path`), current-output geometry for `@pathto` incl. the 404
  rule (`output_path`, `output_dir`, `has_output_context()=true`), and
  pagination source/geometry (`pagination_output_path`, `build_threads`), plus
  per-render host bindings (`binding`). Preserved: zero project writes, no
  build decisions, no implicit tracking repair, no watch/hash ownership, the
  existing rendering semantics, and the concurrency contract (snapshot read
  caches are mutex-protected). Evidence: `tests/project_host.cpp` renders a
  real project through `Parser(host, info).render()` exactly as the CLI's
  `build_one` does and asserts byte-identical output for tracked `@pathto`
  (confirmed against the actual `nift` CLI build, incl. `blog=blog/` and the
  404 root-absolute rule), contracts, host bindings, `@input`, `@json` and 3
  pagination pages; plus zero-write tree snapshots and 8-thread concurrent
  renders. Retained archaeology items (repeated `tracked_output_path` lookup,
  loader probe-then-read, provisional host-vs-contract precedence) were
  exercised but deliberately not "fixed" — they remain recorded and
  unresolved. No public project-aware Engine API yet (that is PA3).
- **PA3 public project-aware API** — DONE. `Engine(std::filesystem::path)`
  loads and validates the immutable snapshot eagerly, non-throwing, with
  `is_open()`/`open_error()` for construction status; default `Engine()` stays
  deterministic standalone (no implicit discovery). `render("page-name"[, context])`
  drives the same `Parser(host, info).render()` path the CLI uses, so
  `@pathto`/404, `@input`, `@json`, contracts, pagination, dependencies and
  requirements match the CLI exactly. Controlled failures (never throws, never
  prints): non-project root, invalid config/tracking, unknown page name, and
  render failures all surface as RenderResult errors (`open_error()` is
  propagated for construction failures). Precedence preserved from the
  standalone seam: Context overlays > Engine defaults > `@json` > contracts;
  the page-name argument is authoritative (Context page_name ignored) and the
  project defines the current output (Context current_output ignored). Context
  title overrides the tracked title; the environment provider flows through
  ProjectHost. Dependency/requirement reporting is live on the public result
  (`result.dependencies()`/`result.requirements()`), e.g. `@pathto` emits its
  destination as a requirement. Pagination API stays open (PA5): `render("blog/")`
  returns the primary page. No reload/watch in PA3 (that is PA4).
- **PA4 atomic reload lifecycle** — DONE. `Engine::reload(std::string* error)`
  implements atomic immutable snapshot replacement. The Engine holds
  `shared_ptr<const ProjectState>` per generation; a render captures its
  snapshot under a short mutex and then renders freely, so **in-flight renders
  finish on the snapshot they started with** while later renders observe the
  new one (stronger than CP7a's "mutation cannot overlap rendering", and that
  relaxation is scoped to reload only - the other mutators stay non-concurrent).
  A failed reload **retains the last known-good snapshot** (never fail-closed:
  serving A → project becomes malformed → reload fails → A remains usable),
  returns false with the error, and performs zero project writes. reload() is
  also the retry path for an Engine constructed before its project existed.
  `is_open()`/`open_error()` now read under the mutex; `open_error()` returns
  by value (race-free). Engine defaults and the environment provider are
  unaffected by reload; cache ownership is per generation (a fresh snapshot has
  fresh caches). Staleness detection is deliberately the host's job - an
  explicit reload() API, no filesystem watching in the embedded core, per the
  "glue, not universe" direction. Evidence: `tests/engine_reload.cpp`
  (new-page-after-reload, failed-reload-retains-last-good incl. recovery,
  reload-as-open-retry, generation switch, 8-thread concurrent render + reload
  flipper observing exactly one committed generation per render, zero-write
  across success/failure reloads, defaults/environment survival) - all also
  green under ThreadSanitizer (`test-engine-reload-tsan`, plus
  `test-engine-concurrency-tsan` re-validated against the current core).
- **PA5 CLI ↔ Engine parity → portable conformance corpus** — not a handful of
  examples. The seed of the cross-implementation corpus `nift-rs` will inherit
  (CLI ↕ C++ project Engine ↕ nift-rs): tracked-name lookup, index/trailing-
  slash geometry, content/template composition, metadata, Context overlays,
  `@input`, `@json`, schema, contracts, `@getenv`, tracked + concrete `@pathto`,
  404 geometry, dependencies, requirements, pagination, missing sources,
  malformed config/tracking, unknown page, path containment/security.
  Pagination API design stays open through PA1/PA2 but **must be resolved before
  PA5 sign-off** — no magic context keys controlling Engine behaviour; derive a
  typed/runtime API from existing CLI pagination semantics only if explicit page
  selection is required.
- **PA6 archaeology/docs/sign-off** — document the contract, record the
  staleness/reload decision, resolve/forward the now-concrete archaeology items
  (empty-root containment, repeated `tracked_output_path` lookup,
  host-vs-contract precedence).

### Decided semantics (proposed, to be confirmed at PA3/PA4 review)

- Snapshot semantics: project state read once at construction; serves a stable
  snapshot for all concurrent renders; never watches, never writes.
- Page-name lookup: exact tracked name; `/` and trailing `/` map to index (same
  geometry as the CLI). Unknown name → dedicated error (distinct from `@pathto`
  404 rule).
- Current output: the page's own `output_path`, so `@pathto`/404 behave like the
  CLI.
- Failure mode: controlled render/state errors for missing/malformed/stale
  project state; never crashes.

## Architecture (agreed)

- One long-lived `nift::Engine` per process (root, loaders, defaults, caches;
  read-only during serving), one per-request `nift::Context` (page name/title,
  request-scoped values), one `Parser` per render call (as the CLI already
  does). This gives safe concurrent rendering without putting request state
  into shared objects.
- Internal seam: `RenderHost` (CP1). `Parser` no longer names `ProjectInfo`;
  it renders against the capability interface. `ProjectInfoHost` forwards to
  the exact CLI behaviour; `EngineHost` is the embed-side implementation.
- `render_composed(template_source, page_source, require_exactly_one_content)`
  is the shared page+template composition; standalone partial rendering is the
  same operation with no page source, so `@content` in a partial is an error.
- Value model: public `nift::Value` is Nift's contract and is self-contained at
  the compilation boundary (PIMPL; the internal Jsonic++ `json::Document`
  backing lives only in `src/`). A consumer of `<nift/nift.h>` must not need
  `-Isrc`, see `json::Document`, or know Jsonic++ exists.
  The parser resolves values in the order host-supplied bindings -> `@json`
  bindings -> contract bindings -> built-in metadata. The parts directly
  exercised by conformance tests are: Context overlay > Engine default; host
  binding vs `@json` (controlled collision error); host "title" > title
  metadata; structural built-ins (`name`, `content-path`, `output-path`,
  `template-path`, `loop`) non-overridable by `set`. Host-vs-contract
  precedence is provisional until an Embedded contract source exists.

## Checkpoint status

- **CP1 ✅** (`c02e88d`): `RenderHost` seam; `ProjectInfoHost` forwards
  unchanged; full CLI suite green; no public API added.
- **CP2 ✅** (`a4a3139`): first public Embedded Nift seam
  (`include/nift/`: Source, Value, RenderError, RenderResult, Context, Engine);
  `@content` via per-parser page source; `render_composed`; `@input` no-cwd
  guard; `RenderHost::root()` returns const ref; `tests/engine_smoke.cpp`.
  Full CLI suite + sanitizers green. See ASAN-FLAKE-001 below.
- **CP3** (`0795c0a` + review fixes): value bindings (`engine.set`/`set_json`,
  `context.set`/`set_json`), structural built-in rule, collision tests, and the
  title-precedence fix (`Context::set_title` writes the same per-render "title"
  slot as `Context::set("title", ...)` and outranks an Engine default).
  Established and directly exercised: Context overlay > Engine default; host
  binding colliding with `@json` => controlled "already bound" error; host
  "title" binding > built-in title metadata; structural/reserved names rejected.
  **Host-vs-contract precedence is provisional** (Embedded Nift has no contract
  source yet; `EngineHost::contract_source()` returns nullptr). Do not claim it
  frozen until an Embedded contract capability exists and a real conformance
  test exercises it. The public headers are self-contained (PIMPL `nift::Value`,
  no `json::Document`/`src` visibility); `test-public-header` compiles a
  consumer with only `-Iinclude`. Exception contract is truthful: moves are
  pure `shared_ptr` moves (nothrow, no allocation); `impl_ == nullptr` is the
  canonical Null representation (reads non-allocating, mutation materialises
  on demand); static_asserts pin `is_nothrow_move_*` from the consumer view.
- **CP4** (`6159983`): loader semantics. `@input`/`@content`/`@json`/`@dep` route
  their source existence/readability through the host (`source_exists` /
  `source_readable` / `read_shared_source` / `read_shared_json`), so a custom
  `Engine::set_loader` supplies sources from memory with no filesystem; a
  custom `Engine::set_environment_provider` supplies `@getenv`. Defaults
  replicate the CLI exactly (`ProjectInfoHost` forwards to the filesystem).
  Tests: `tests/engine_loaders.cpp` (`make test-engine-loaders`).
  **Archaeology finding (preserved, not silently changed):** with an empty
  engine root, `@json`/`@dep` containment uses `path_within(root, path)`,
  which with an empty base resolves against the process working directory.
  This mirrors the `@input` no-cwd guard decision and should be settled
  deliberately (likely: require a root, or define empty-root containment as
  no-containment) in a later checkpoint; it is not claimed as a contract yet.
- **CP5** (`2266ca2`): `@pathto`/`@pathtofile` through the host path capability.
  `RenderHost` replaces the parser's tracked lookup with `has_output_context()`
  and `tracked_output_path(name)` (CLI resolves tracked names; the embedded
  engine, with no tracking attached, treats every argument as a concrete
  project path — it must not invent tracked state; a project-backed Engine host
  can later implement `tracked_output_path` from `.nift/tracked.json`, see the
  project-attached requirement above). The per-render `Context::set_current_output` supplies the
  current output location; without it `@pathto` errors rather than guessing.
  The shared relative-path computation, the 404 rule (page name "404" ->
  root-absolute web paths), requirements recording and concrete-path existence
  (via `source_exists`) are unchanged. Deferred to API hardening (CP7c): a
  custom `set_path_resolver` callback; the default (root-relative, context
  current output) is implemented. Tests: `tests/engine_pathto.cpp`
  (`make test-engine-pathto`).
- **CP6** (`87f1050`): the CLI render entry is fully host-driven; the parser's
  only remaining direct filesystem calls are `path_within` containment and the
  pagination path (CP7b). Project-aware rendering is recorded (not implemented)
  as an explicit requirement: a project-backed Engine host consumes
  `.nift/tracked.json`/`config.json` and `render("page-name"[, context])` is
  orchestration over the same core.
- **CP7a** (`1f3a15e`): concurrency/thread-safety contract. Concurrent `render()`
  calls on one Engine are supported and safe (provided no concurrent
  mutation); Engine mutators (set/set_json/set_loader/set_environment_provider/
  set_root) are not thread-safe with active renders (configure before serving —
  documented, not serialized behind a global lock); Context is per-render and
  must not be shared or mutated concurrently; the loader/environment provider
  may be called concurrently and must be thread-safe (defaults are); internal
  caches are mutex-protected. Contract documented in `include/nift/engine.h`;
  `tests/engine_concurrency.cpp` proves the supported case (8 threads x 200
  renders on one Engine) and passes under ThreadSanitizer.
- **CP7b** (`e3dcc42`): pagination's remaining direct source/filesystem dependency
  routed through the host boundary: the pagination template/separator
  existence and readability checks now use `source_exists` / `source_readable`,
  and reads already used `read_shared_source`. The only remaining
  `filesystem::` calls in the parser are `path_within` (shared containment
  utility). Archaeology finding for CP7c/spec: pagination does not enumerate
  directories — it discovers the conventional `.separator` by named-source
  existence and reads named sources, so the existing host capabilities express
  it fully (no new capability required). CLI pagination semantics preserved
  (smoke, 18/18 incremental equivalence, ASan/UBSan and TSan pagination).
- **CP7c** (`40fbb73` + repair): API freeze of the **standalone/shared core**.
  The public `include/nift/` surface was audited and the behavioural contracts
  below were documented in the headers; the public-header probe now exercises
  every public type/member (and the documented value-construction example) with
  only `-Iinclude`. Final evidence: all six engine tests (native + ASan/UBSan),
  TSan concurrency + TSan pagination, pagination sanitizer, full CLI suite.
  **What is frozen:** the standalone shared rendering core and its current
  public surface, as the C++ behavioural reference. **What remains open:** the
  project-aware layer (`render("page-name"[, context])` backed by real
  `.nift/tracked.json`/`config.json` knowledge) and the ecosystem bindings /
  C ABI / Rust experiment. See the sign-off sections below.

## Behavioural contracts established (C++ behavioural reference)

- **Values** (`nift::Value`): deep-copy semantics; move leaves the source as a
  valid Null and is nothrow; type-mismatched reads return type defaults;
  `operator[](string)` materialises a Null as Object and throws
  `std::runtime_error` on non-objects; `operator[](size_t)`/`push_back` require
  an Array (assign `make_array()` first) and throw otherwise; a `ValueRef` is a
  transient construction handle.
- **Context / precedence**: Context overlay -> Engine default -> `@json`
  binding -> contract binding -> built-in metadata; structural built-ins
  (`name`, `content-path`, `output-path`, `template-path`, `loop`) rejected;
  `set_title` and `set("title")` share one per-render slot. Host-vs-contract
  precedence is provisional until an Embedded contract source exists.
- **Loaders**: repeatable lookup functions (probe then read), may be called
  concurrently, must be thread-safe.
- **`@pathto`**: requires `Context::set_current_output` (and page name for the
  404 rule); errors without it; the 404 rule yields root-absolute web paths;
  standalone rendering has no tracked pages (concrete-project semantics);
  requirements are recorded in `RenderResult::requirements()`.
- **Pagination**: source requirements are named-source existence/readability
  (no directory enumeration); expressed by the existing host capabilities.
- **Concurrency**: concurrent `render()` on one Engine supported (no concurrent
  mutation); Engine mutation is a pre-serve lifecycle boundary; Context is
  per-render; loader/provider must tolerate concurrent invocation; caches
  mutex-protected.
- **Dependency discovery**: `RenderResult::dependencies()/requirements()`
  spell root-relative paths (or as supplied without a root); persistence is
  the host's decision.

## Deliberately unsupported / future

- **Engine mutation during active renders** is unsupported (documented, not
  serialized).
- **Project-aware rendering** (`render("page-name"[, context])` backed by real
  `.nift/tracked.json`/`config.json` knowledge) is a recorded future
  requirement, not implemented.
- **Custom `set_path_resolver`** is deferred (a runtime route's interaction
  with existence/requirements is not yet designed).
- **Value serialization** (e.g. `dump()` to JSON text) is not in the public
  API.
- **Ecosystem bindings and the C ABI** have not started; the canonical C++
  core (this line) is the behavioural reference for them.
- **The Rust independent implementation** has not started; it is the one
  deliberate independent experiment, used to validate portability and
  conformance against this reference.

## Unresolved known issues

- **ASAN-FLAKE-001**: single historical GCC/ASan stack-instrumentation finding;
  structurally in-bounds, not reproduced in 100k+ constructions, no
  corroborating corruption. Retained; reopen-and-preserve-report on reappearance.
- **Empty-root containment**: `@json`/`@dep`/`@pathto` containment with an
  empty Engine root derives meaning from the process working directory via
  `path_within`; preserved, to be settled deliberately in spec work.
- **`tracked_output_path` repeated lookup**: queried more than once per
  directive; hosts are deterministic so this is correct, but the seam assumes
  stable lookup during a render (resolve-once is a possible later cleanup).
- **Loader probe-then-read**: a custom loader may be called twice per source;
  classified as an API-hardening item (repeatable lookup contract).

Cadence: one or two checkpoints between reviews; CP5 is a hard review gate.

## ASAN-FLAKE-001 (retained, unresolved)

Single historical GCC/ASan `stack-buffer-overflow` during `Parser` construction
(`json_bindings_` member init) on a `build_many` worker thread in `build_one`.

Investigation findings:

- CP2 grew `sizeof(Parser)` 640 -> 784 bytes (verified). `json_bindings_` sits
  at Parser offset 552 (verified via exact layout mirror), so the reported
  4-byte write (the `_Prime_rehash_policy` float, `hashtable_policy.h:601`) is
  structurally **in-bounds** within a 784-byte object.
- Not reproduced after 100,000+ Parser constructions across baseline CP2, CP1,
  heap-allocated Parser, single-thread, concurrent, `detect_stack_use_after_return=1`,
  ASLR-disabled, and `-O2` sanitizer configurations.
- No corroborating corruption: ASan/UBSan, ThreadSanitizer, Valgrind, and the
  full non-sanitized suite were clean.
- Conclusion: **no evidence of a Nift memory-safety defect.** The evidence
  strongly favours a GCC/ASan stack-instrumentation anomaly exposed by CP2's
  changed `Parser` layout (build_one has an unusually large 88-object stack
  frame), but the exact compiler-side mechanism is unproven.

Decision: retained as an unresolved toolchain/instrumentation anomaly, **not**
declared solved. If the finding ever reappears, stop and preserve the complete
ASan report immediately (report layout, object list, shadow state) and pursue a
minimal standalone reproducer before concluding. Do **not** heap-allocate
`Parser` merely to silence it; if `build_one` is later judged structurally too
stack-heavy, refactor on measured grounds.

## Roadmap: semantic archaeology / implementation-caveat audit

Before freezing any Nift specification (multi-language contract), run a
dedicated audit whose purpose is to aggressively discover accidental behaviour,
undefined edges, platform dependencies, implementation artifacts, inconsistent
semantics and "probably fine" caveats. Classify every finding as one of:

```text
DEFINED NIFT BEHAVIOUR    specify it; conformance test it; all implementations reproduce it
BUG / ACCIDENTAL          fix C++ before spec freeze; test intended behaviour
IMPLEMENTATION DETAIL     explicitly outside the contract; implementations may differ
PLATFORM-SPECIFIC         define the portable part; document permitted variation
UNRESOLVED                no spec freeze until decided
```

This is what lets Nift earn *dependable* as a maintained property rather than
website copy: not having no known failures, but having deliberately sought out,
classified, and either specified or removed every piece of weirdness.
