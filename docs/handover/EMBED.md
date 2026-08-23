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
- Value model: public `nift::Value`, backed internally by Jsonic++
  `json::Document` (not an alias). The parser resolves values in the order
  host-supplied bindings -> `@json` bindings -> contract bindings -> built-in
  metadata, with structural built-ins (`name`, `content-path`, `output-path`,
  `template-path`, `loop`) non-overridable by `set`.

## Checkpoint status

- **CP1 ✅** (`c02e88d`): `RenderHost` seam; `ProjectInfoHost` forwards
  unchanged; full CLI suite green; no public API added.
- **CP2 ✅** (`a4a3139`): first public Embedded Nift seam
  (`include/nift/`: Source, Value, RenderError, RenderResult, Context, Engine);
  `@content` via per-parser page source; `render_composed`; `@input` no-cwd
  guard; `RenderHost::root()` returns const ref; `tests/engine_smoke.cpp`.
  Full CLI suite + sanitizers green. See ASAN-FLAKE-001 below.
- **CP3** (in progress): value bindings (`engine.set`/`set_json`,
  `context.set`/`set_json`), precedence + structural built-in rule, direct
  collision tests. Then CP4 loaders, CP5 `@pathto` (own review gate), CP6 CLI
  fully on the core, CP7a concurrency / CP7b pagination-through-host /
  CP7c API freeze + docs.

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
