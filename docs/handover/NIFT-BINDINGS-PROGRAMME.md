# Nift Bindings Programme — strategic handover

Status: **direction agreed, not yet a funded workstream.** This document records
the strategic case and the proposed shape for making Nift's template language
usable server-side across the popular backend stacks, reached by review of the
embedded-engine work (`EMBED.md`) and the independent Rust conformance
programme (`nift-rs` NR0–NR12).

The current C++/Rust conformance work is **not** changed by this document. This
is the forward-looking plan for a separate, deliberate workstream that begins
after Embedded Nift C++ and `nift-rs` are frozen.

## The strategic case

The product thesis that has converged after review:

> **Nift's template language should be usable server-side across popular backend
> stacks. Cross-language availability is not ancillary — it is part of the
> feature.**

Not "Nift happens to have a Python binding", but:

```text
Same Nift templates
same semantics
same contracts
same dependency behaviour
same rendering model

C++    Rust    Go    C#    Python    Ruby    JavaScript
```

That is a materially different product from yet another language-specific
templating library. The differentiator is **cross-language consistency**,
especially combining static and dynamic rendering from the same templates.

The conceptual headline:

> **Build it statically. Render it dynamically. Keep the same templates.**

And the eventual concrete claim:

> Every supported Nift runtime and language binding is tested against the same
> Nift conformance corpus.

### The static/dynamic duality is the killer feature

None of the incumbents offer this. Jinja2 does not drive your static pipeline;
Razor/ERB/`html/template` do not do static generation. Nift does both from one
language:

```text
                templates/
                    │
             Nift language
              /          \
             /            \
      nift build         Go backend
          │                  │
     static pages         dynamic pages
```

A site can statically generate most product pages (`/products/widget`) while a
user-specific page (`/account`) is rendered dynamically by Go — **without**
maintaining "static templates → Nift, dynamic templates → html/template".

This fits Nift's philosophy: Nift is the rendering layer, not the application.
It does not care whether the server is Django, FastAPI, Gin, Echo, ASP.NET,
Express, Rails, Axum, or something written next year. **Glue, not universe.**

### The adoption funnel runs through the static case

"Replace Jinja with Nift" is not compelling to an established Django app. But
this is:

> We are already using Nift to build marketing pages, docs, public content and
> the application shell — and now our Python backend can render the exact same
> template language dynamically.

The duality is the wedge; the static pipeline is the entry point that makes the
switch worthwhile.

## Two programmes

### 1. The current conformance programme (unchanged)

```text
NIFT-RS CONFORMANCE PROGRAMME

NR0
 ↓
 ...
 ↓
NR9       ← current (reload + concurrent serving lifecycle)
 ↓
NR10  cross-implementation differential validation
 ↓
NR11  hardening
 ↓
NR12  DX + performance + docs + final cold review

Goal: prove an independent Rust implementation can reproduce frozen portable
Nift semantics.
```

`nift-rs` stays an **independent native implementation, not a C-ABI wrapper**.
It is the independent witness:

```text
C++ implementation ───┐
                      ├── same corpus → same semantics
Rust implementation ──┘
```

The Rust programme has repeatedly forced discovery of what Nift *actually
means* (e.g. `relative(root)` → `.`, `@for` multiline structural
normalisation), including correcting earlier review assumptions. That is the
value of an independent implementation, and it must not be lost by turning Rust
into `Rust API → FFI → C++`.

### 2. The proposed bindings programme (shape, not yet canonised)

```text
NIFT BINDINGS PROGRAMME

NB0  ABI requirements + ownership inventory
NB1  experimental C ABI
NB2  C ABI conformance + adversarial hardening

NB3  Go binding + real application
NB4  Python binding + real application

     ← ABI DESIGN REVIEW / LAST CHEAP BREAK POINT

NB5  freeze C ABI v1

NB6  C# binding
NB7  Node/JavaScript binding
NB8  Ruby binding

NB9  cross-platform packaging/distribution
NB10 full binding conformance matrix
NB11 real multi-stack dogfooding
NB12 release/readiness/website evidence
```

Checkpoint numbers are a working shape, not canon. Go and Python are deliberately
placed **before** ABI v1 freezes: they stress very different FFI models, and two
languages feeling natural without exposing C++ implementation concepts is the
best evidence the ABI is genuinely language-neutral. If they reveal a function
that "only makes sense because we were thinking like C++ programmers", that is
the last cheap break point — once ABI v1 is declared, that freedom largely
disappears.

## Core principles

### 1. The C ABI is the critical design investment

The canonical engine sits behind a stable, versioned C ABI; every binding wraps
the ABI and isolates itself from C++ ABI concerns:

```text
                 canonical Nift engine
                         │
                    stable C ABI
                         │
       ┌─────────┬───────┼───────┬─────────┐
       │         │       │       │         │
      Go       Python    C#     Ruby       JS
    binding    binding  binding binding   binding
```

Rust is the unusual case: the independent native implementation already exists.

The ABI surface must be small enough that ABI stability is realistic — roughly:

```text
engine lifecycle
project reload
context/bindings
render text
render page
render result
errors
dependencies
requirements
```

Do **not** expose `Parser`, `ProjectState`, `RenderHost`, `TrackedInfo`, etc.
Those are implementation concepts. The ABI exposes what a backend developer
actually wants.

### 2. Bindings must be boring

Language layers should contain **almost no semantic logic whatsoever**. A Python
binding should map a call like:

```python
engine = nift.Engine.project(".")
result = engine.render_page("users/show", {"user": user})
print(result.output)
```

onto ABI calls, and must not know how `@for`, `$[...]`, contracts, pagination or
`@pathto` work. Design rule: **if a binding starts knowing those things, we have
designed it wrong.**

### 3. Per-binding conformance gate (non-negotiable)

A binding is not "done" because its API works and its package installs; it is
done when rendering **through that binding's public language API** passes the
canonical behavioural corpus. Test each binding through its public API, not
merely test the C ABI once and declare all wrappers equivalent.

The evidence matrix becomes:

```text
                         Canonical corpus
                              │
             ┌────────────────┼────────────────┐
             │                │                │
          Nift CLI        C++ Engine       Rust Engine
             ✓                ✓                ✓
                              │
                           C ABI
                              ✓
             ┌───────┬────────┼────────┬───────┬───────┐
             │       │        │        │       │
            Go     Python    C#       Node    Ruby
             ✓       ✓        ✓        ✓       ✓
```

The claim is then:

> The same conformance corpus is executed through every supported Nift runtime
> and language binding.

This makes cross-language consistency a regression-tested property, not an
aspiration.

### 4. Distribution is the real burden — solve it architecturally

The wrapper is small (hundreds of lines); the burden is native packaging
permutations:

```text
Linux x86_64 / arm64
macOS x86_64 / arm64
Windows x86_64

Python versions
Node ABI/N-API
NuGet packaging
Ruby gems
Go/cgo build expectations
```

The binding programme must be designed around **minimising native packaging
permutations**: a versioned C ABI, disciplined native library releases,
automated cross-platform artifacts, and binding packages that consume those
artifacts rather than each reinventing the native build.

### 5. Explicit ownership of official bindings

Supported bindings (C++, Rust, Go, Python, C#, JavaScript, Ruby) are owned by
the Nift project with the same release discipline as Nift itself:

```text
supported binding
    ↓
maintained by Nift project
    ↓
canonical conformance gate
    ↓
supported platform matrix
    ↓
release CI
    ↓
documented compatibility
```

Third-party/community bindings (Java, Kotlin, PHP, ...) are welcome but are an
explicitly distinct category. Supported bindings must never be "something
somebody contributed in 2027; hopefully it still works".

## Sequencing recommendation

1. Finish the current C++ Embed + Rust conformance programme (NR9 is current).
2. Design and freeze a very small C ABI.
3. Build the Go binding and dogfood a real SSR application.
4. Build the Python binding and dogfood another real application.
5. Correct the ABI based on lessons **before** calling ABI v1 stable.
6. Then C#, Node and Ruby.
7. Once ABI v1 is stable, backward compatibility becomes serious.
8. Build all official bindings **before** loudly marketing Nift as
   cross-language SSR, so the website claim can be concrete rather than a
   roadmap promise.

The bindings programme is a deliberate major Nift workstream, not an optional
collection of wrappers.

## Related documents

- `docs/handover/EMBED.md` — the embedded-engine architectural programme this
  document builds on.
- `nift-rs/docs/checkpoints.md` — the NR0–NR12 conformance programme.
- `nift-rs/docs/authorities.md` — the conformance corpus as the semantic
  authority (the per-binding gate depends on it).
- `nift-embed/tests/conformance/` — the canonical corpus driver (CLI vs C++
  Engine vs golden), the pattern the binding conformance matrix extends.
