# CP19: Rendering API direction (render / render_path / render_text)

The rendering API is explicit and non-ambiguous. Meanings are stable and never
dispatch on filesystem state.

## Canonical C++ surface

```cpp
// Tracked project page (always a name, never a path or literal source).
engine.render(page_name);
engine.render(page_name, context);

// Standalone filesystem source (always a path; a missing path is a
// missing-path error and is never reinterpreted as text).
engine.render_path(path);
engine.render_path(path, context);

// Standalone in-memory source (always template bytes; never checked against
// the filesystem).
engine.render_text(text);
engine.render_text(text, context);

// Full content/template composition (typed sources; path/path, text/text and
// mixed; no string guessing).
engine.render(page_source, template_source);
engine.render(page_source, template_source, context);
```

An omitted context behaves exactly like a fresh empty context; request state
never leaks between no-context renders. `render_string`/`RenderString`/
`renderString` terminology is gone.

## Binding mappings

| Surface | C++ | Go | C# | JavaScript | Python | Rust (conformance) |
|---------|-----|----|----|-----------|--------|-------------------|
| tracked page, no ctx | `render(name)` | `Render(name)` | `Render(name)` | `render(name)` | `render(name)` | `render_page(name, &ctx)` |
| tracked page, ctx | `render(name, ctx)` | `RenderWithContext(name, ctx)` | `Render(name, ctx)` | `render(name, ctx)` | `render(name, ctx)` | `render_page(name, &ctx)` |
| filesystem path | `render_path(path)` | `RenderPath(path)` | `RenderPath(path)` | `renderPath(path)` | `render_path(path)` | `render_path(path, &ctx)` |
| in-memory text | `render_text(text)` | `RenderText(text)` | `RenderText(text)` | `renderText(text)` | `render_text(text)` | `render_text(text, &ctx)` |
| typed composition | `render(Source, Source)` | `RenderSources(...)` | `Render(RenderSource, RenderSource)` | `renderSources(...)` | `render_sources(...)` | `render(&Source, &Source)` |

Go has no arity overloading, so the context-taking forms are
`RenderWithContext` / `RenderPathWithContext` / `RenderTextWithContext`; the
no-context forms are `Render` / `RenderPath` / `RenderText`. No `nil` is
required for the common no-context operation and no variadic parameter suggests
multiple contexts.

The C ABI retains `nift_source` as the ownership-explicit composition primitive
and adds distinct `nift_engine_render_path` / `nift_engine_render_text` entry
points alongside `nift_engine_render_page`, `nift_engine_render` and
`nift_engine_render_partial`, so production bindings never infer a source kind
from filesystem state.

## Required API tests (all green)

* `render(name)` renders a tracked project page; unknown tracked names are
  controlled unknown-page errors.
* `render_path(existing)` renders the file; `render_path(missing)` is a
  controlled missing-path error and is never reinterpreted as literal text.
* `render_text(text)` renders the supplied bytes and never checks whether its
  argument names an existing file.
* All three work with and without context; omitted context is a fresh empty
  context and request state never leaks between no-context renders.
* Typed composition supports path/path, text/text and mixed sources.
* The C ABI and the shared cross-binding corpus remain green.

## Product-positioning boundary

> Embedded Nift is optional request-time rendering infrastructure. Applications
> that need a backend can continue using ordinary Nift-generated assets with
> JSON APIs, WebSockets or any backend framework. A backend does not require
> Embedded Nift.
