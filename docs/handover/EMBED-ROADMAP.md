# Nift Embed programme — remaining roadmap (canonical)

Status: synchronized 2026-08-25 after CP11 (Go binding) and the tracking-forensics
detour. Repository heads at that point: `nift-embed e3d6267`, `nift-rs 1e429bd`,
`nift-embed-regression-suite 47ef21a`.

Sequencing principle: **production bindings attack the frozen C ABI before the
final hardening/performance campaigns**, exactly as Go did. Go found three real
FFI defects (retained Go pointer, concurrent callback-buffer race, missing
Error(diagnostic) transport) before those assumptions propagated. Do not call a
campaign "final" while new FFI/runtime boundaries are still to come.

```text
CP12  strengthen contracts
CP13  C# + ASP.NET dogfood
CP14  Node/JS + HTTP dogfood
CP15  full historical + expanded regression campaign
CP16  sanitizer / memory / platform campaign
CP17  final performance campaign
CP18  merge decision + canonicalization
        ↓
packages + website + release
```

## CP12 — broader shared regression-suite expansion

Expand the implementation-neutral corpus: CLI/build contract modules, Embed
semantic cases, failure/recovery families, host-resource cases, pagination
cases. Expectations stay implementation-neutral.

Acceptance: expanded frozen corpus; C++ API PASS, nift-rs PASS, C ABI PASS, Go
PASS; negative anti-agreement PASS. Gives the next bindings a stronger contract
to attack.

## CP13 — C# production binding

Architecture: idiomatic C# API → P/Invoke native interop → Nift C ABI → canonical
C++ Embed. No Nift semantic reimplementation.

Cover the same public surface as Go: Engine, Context, bindings, render sources,
RenderPage, pagination, dependencies, requirements, loader/environment providers,
Found/NotFound/Error(diagnostic), ABI compatibility, lifetime/disposal. C#
becomes another shared-corpus adapter.

Dogfood requirement: a small real **ASP.NET Core application** using Nift Embed —
long-lived Engine, repeated + concurrent requests, request-specific bindings,
partial/template loading, host-resource callbacks, Error(diagnostic), pagination
where practical. Uses the .NET 10 SDK already on the machine.

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

## CP15 — full historical + expanded regression campaign

Run the complete campaign now that the production binding set is present:
historical Nift regression suite, ruthless/focused suites, shared Embed corpus
across C++, Rust, C ABI, Go, C#, Node. Divergence becomes an explicit contract
decision, not an adapter exception.

## CP16 — sanitizer / memory / platform campaign

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
cross-platform binding behaviour (C#, Node included)
```

## CP17 — final performance campaign

Split:

- **A. Nift CLI/build**: pre-Embed baseline vs final canonical candidate — 10k
  full build, no-op incremental, single-page incremental, shared-dependency
  rebuild, many-directory, modified/hash/hybrid.
- **B. Embed/API/bindings**: direct C++, C ABI, Go, C#, Node — raw render
  overhead and a realistic repeated/server render workload. Differences are
  evidence, not automatic blockers.

## CP18 — merge decision and canonicalization

- `nift-embed` → canonical main Nift repository.
- `nift-embed-regression-suite` → canonical Nift regression infrastructure.
- Preserve the original pre-Embed repository/suite history as the comparison
  reference point.
- The Rust implementation (jsonic-rs / minify-rs / nift-rs) remains the
  independent experimental/conformance implementation, not a second canonical
  Nift.

## After CP18 — packaging, website, release

Language package/distribution, public Nift Embed docs (C++, Go, C#, Node/JS),
real server examples, website navigation/content, release notes, release
CI/artifacts, final release. Decide later whether additional bindings are
required before the first public Embed release.

## Permanent gates

C++ conformance, CLI/build contracts, Rust tests, NR6, NR12, shared Embed corpus,
negative anti-agreement, Go race tests, C# binding tests, Node binding tests,
zero-mutation/recovery contracts, clean repository state. The zero-`unsafe`
requirement remains scoped to jsonic-rs / minify-rs / nift-rs and does not apply
mechanically to FFI bindings where native interop is inherent.
