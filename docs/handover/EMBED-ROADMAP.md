# Nift Embed programme — remaining roadmap (canonical)

Status: synchronized 2026-08-26 after CP12/CP12.1 (contract strengthening) and
the product-scope decision that Python joins the initial production binding set.
Repository heads at that point: `nift-embed 5ab5a8a`, `nift-rs 808b9a0`,
`nift-embed-regression-suite f4c3c55`.

Sequencing principle: **production bindings attack the frozen C ABI before the
final hardening/performance campaigns**, exactly as Go did. Go found three real
FFI defects (retained Go pointer, concurrent callback-buffer race, missing
Error(diagnostic) transport) before those assumptions propagated. Do not call a
campaign "final" while new FFI/runtime boundaries are still to come.

```text
CP13  C# + ASP.NET Core dogfood
CP14  Node/JavaScript + HTTP dogfood
CP15  Python + real Python web-app dogfood
CP16  full historical + expanded regression campaign
CP17  sanitizer / memory / platform campaign
CP18  final performance campaign
CP19  merge decision + canonicalization
        ↓
packages + website + release
```

## Product scope: the initial production binding set

Nift Embed is consistent with Nift's philosophy precisely because it lets Nift
remain glue inside somebody else's backend rather than becoming the backend
framework itself. Nift should NOT grow: HTTP framework, router, ORM, database
layer, auth framework, job system, deployment runtime, application framework.
The model stays:

```text
user's backend/framework
        ↓
    Nift Embed
        ↓
template rendering
```

The intended initial production binding set is **C++, Go, C#, JavaScript/Node,
Python**, with **Rust as the independent experimental/conformance
implementation**. **Python is the final planned initial production binding.**

After Python, STOP adding languages by default. Additional bindings after the
initial release require: demonstrated user demand, OR a compelling dogfood/use
case, OR a materially different runtime/FFI model that teaches us something
important. Do not turn language coverage into a completeness contest. The
product-level mental model remains: *Nift is a fast templating and build tool.
Nift Embed lets applications use the same templating engine directly.* Bindings
are distribution/integration surfaces, not new Nift subsystems.

## CP13 — C# production binding

Architecture: idiomatic C# API → P/Invoke native interop → Nift C ABI → canonical
C++ Embed. No Nift semantic reimplementation.

Cover the same public surface as Go: Engine, Context, Engine-default vs Context
binding precedence, string/number/bool/JSON values, composed/partial/page
renders, pagination, dependencies, requirements, loader/environment providers,
Found/NotFound/Error(diagnostic), invalid-binding/setup failures, malformed-JSON
failure family, ABI compatibility, lifetime/disposal (SafeHandle/IDisposable,
delegate rooting — do not copy Go's ownership machinery mechanically). C# becomes
another shared-corpus adapter (fifth).

Dogfood requirement: a small real **ASP.NET Core application** using Nift Embed —
long-lived Engine, repeated + concurrent requests, request-specific Context,
Engine defaults + Context precedence, partial/template loading, host-resource
callbacks, Error(diagnostic), pagination where practical, deterministic disposal.
Uses the .NET 10 SDK already on the machine (verify `dotnet --list-runtimes`
before assuming an ASP.NET runtime package is required).

STOP FOR REVIEW after CP13.

## CP14 — Node/JavaScript production binding

Architecture: JS API → native Node boundary → Nift C ABI → canonical C++ Embed.
No Nift semantic reimplementation.

Attention: JS runtime/thread restrictions, callbacks from C++ pagination workers,
native object lifetime, GC interaction, exceptions across native boundaries,
concurrent renders, callback diagnostic preservation. Do not blindly copy the Go
callback architecture if Node's runtime model needs a different safe bridge. Node
becomes another shared-corpus adapter.

Dogfood requirement: a small real Node HTTP application exercising the same
practical concerns as the ASP.NET dogfood.

STOP FOR REVIEW after CP14.

## CP15 — Python production binding

Architecture: Python API → Python C-API/FFI boundary → Nift C ABI → canonical C++
Embed. No Nift semantic reimplementation.

Attention: GIL interaction with C++ pagination worker callbacks, callback buffer
lifetime across the Python boundary, reference counting and object lifetime,
UTF-8/native string ownership, exceptions across the native boundary, concurrent
render behaviour, `error_prefix` semantic-family handling (do not manufacture C++
parser wording). Python becomes another shared-corpus adapter.

Dogfood requirement: a small real Python web application (e.g. WSGI/ASGI)
exercising the same practical concerns as the ASP.NET dogfood.

This is the final planned initial production binding. STOP FOR REVIEW after CP15.

## CP16 — full historical + expanded regression campaign

Run the complete campaign now that the production binding set is present:
historical Nift regression suite, ruthless/focused suites, shared Embed corpus
across C++, Rust, C ABI, Go, C#, Node, Python. Divergence becomes an explicit
contract decision, not an adapter exception.

## CP17 — sanitizer / memory / platform campaign

ASan, UBSan, TSan where applicable, race detector where applicable, native/FFI
lifetime stress, repeated engine construction/destruction, concurrent renders,
malformed foreign inputs, callback failure/panic/exception boundaries, Windows /
macOS / Linux.

Retained items scheduled here:

```text
Go callback-output buffer lifetime bound
    current:  callback C allocations live until Engine.Close()
    required: bounded / provably safe render-active lifetime
    (do NOT resurrect free-on-next-callback; CP11 proved it unsafe)
loaderKeys separator normalization
write_file_atomic-style helper audit
cross-platform binding behaviour (C#, Node, Python included)
```

## CP17 — sanitizer / memory / platform campaign — ACCEPTED 2026-08-26

Sanitizer (ASan/UBSan + TSan), Go/C# native-lifetime enforcement (render epoch,
render and non-render disposal, provider installers, callback containment),
loaderKeys separator normalization, write-path audit, cross-platform binding
audit. Acceptance recorded in the regression-suite contract history.

## CP18 — final performance campaign — COMPLETE 2026-08-26

Split:

- **A. Nift CLI/build**: pre-Embed baseline vs final canonical candidate — 10k
  full build, no-op incremental, single-page incremental, shared-dependency
  rebuild, many-directory, modified/hash/hybrid.
- **B. Embed/API/bindings**: direct C++, C ABI, Go, C#, Node, Python — raw render
  overhead and a realistic repeated/server render workload. Differences are
  evidence, not automatic blockers.

Results: see docs/handover/CP18-PERFORMANCE-REPORT.md. 10k builds ~0.1 s and
near-linear; raw render 1.4-2.8 us for C++/C ABI/Go/C#/Python and ~12.8 us for
Node (async bridge); request-loop totals sub-20 ms/1000 everywhere.
Benchmark-only fixes (stale CLI grammar in two suite scripts); no optimization
changed observable semantics.


## CP19 — rendering API direction (render / render_path / render_text) — COMPLETE 2026-08-27

Stable, non-ambiguous rendering surface across the canonical C++ API, C ABI and
every production binding (Go, C#, Node/JS, Python) plus the Rust conformance
implementation:

- `render(name)` / `render(name, context)`  -> ALWAYS a tracked project page
  name (never a filesystem path or literal source; unknown names are
  controlled unknown-page errors).
- `render_path(path)` / `(path, context)`    -> ALWAYS a filesystem path
  (missing path is a controlled missing-path error, never reinterpreted as
  text).
- `render_text(text)` / `(text, context)`    -> ALWAYS in-memory template
  source (never checked against the filesystem).
- `render(Source, Source)` / `(..., context)`-> typed full composition
  (path/path, text/text, mixed; no string guessing). `render_string`/
  `RenderString`/`renderString` terminology removed.
- Omitted context == fresh empty context; request state never leaks between
  no-context renders.

Binding specifics: Go uses `Render`/`RenderWithContext`/`RenderPath`/
`RenderPathWithContext`/`RenderText`/`RenderTextWithContext` (no arity
overloading); C# `Render`/`RenderPath`/`RenderText`; JS `render`/`renderPath`/
`renderText` (composition via `renderSources`); Python `render`/`render_path`/
`render_text` (composition via `render_sources`). The C ABI adds
`nift_engine_render_path` / `nift_engine_render_text` alongside the existing
tracked-page, composed and partial entry points; `nift_source` remains the
ownership-explicit low-level composition primitive.

Required API tests added for every surface (tracked/unknown, path existing/
missing, text-literal, with/without context, no-state-reuse, typed
composition); cross-binding corpus and C ABI remain green. See
docs/handover/CP19-RENDER-API.md.

## CP19 — merge decision and canonicalization

- `nift-embed` → canonical main Nift repository.
- `nift-embed-regression-suite` → canonical Nift regression infrastructure.
- Preserve the original pre-Embed repository/suite history as the comparison
  reference point.
- The Rust implementation (jsonic-rs / minify-rs / nift-rs) remains the
  independent experimental/conformance implementation, not a second canonical
  Nift.

## After CP19 — packaging, website, release

Language package/distribution (C++, Go, C#, Node/JS, Python), public Nift Embed
docs, real server examples, website navigation/content, release notes, release
CI/artifacts, final release. The initial production binding set is complete; no
new languages by default after this.

## Permanent gates

C++ conformance, CLI/build contracts, Rust tests, NR6, NR12, shared Embed corpus,
negative anti-agreement, Go race tests, C# binding tests (incl. ASP.NET
Core dogfood smoke), Node binding tests, Python binding tests, zero-mutation/recovery contracts, clean repository state.
The zero-`unsafe` requirement remains scoped to jsonic-rs / minify-rs / nift-rs
and does not apply mechanically to FFI bindings where native interop is inherent.
