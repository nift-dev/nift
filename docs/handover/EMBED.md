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

Longer term the C++ implementation should become one of several complete Nift
implementations (Rust, Go, JS, ...), each with CLI and embed API, sharing a
portable behavioural contract and conformance tests. A consequence of this work
is a formal distinction between genuine Nift contracts and C++ implementation
details.

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
  Then CP7c API freeze + docs.

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
