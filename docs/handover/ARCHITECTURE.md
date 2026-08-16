# Nift architecture

This is the living implementation-oriented map of modern Nift. It records the
mental model, historical rationale, hazards, performance model, and orientation
work needed before changing the C++ implementation.

Source, tests, and Git history are authoritative for exact symbols and behavior.
Remembered names such as Parser, ProjectInfo, TrackedInfo, WatchList, Path, and
JSON helpers must be reconciled with the live tree. Never alter source merely to
match this document. A disagreement may mean the recollection is stale, the
architecture evolved, this document needs correction, or the source has drifted.

## Reconciled implementation snapshot (2026-08-16)

The inherited model has now been checked against the C++ implementation and the
independent black-box suite. The principal ownership map is:

| Area | Current owner |
|---|---|
| CLI parsing and project mutation commands | `CLI.cpp` |
| Config, tracking, incremental selection, parallel builds, page metadata | `ProjectInfo` |
| Template rendering and discovered dependencies/requirements | `Parser` |
| Watched-directory persistence and reconciliation | `WatchList` |
| Containment, canonicalization, reads/writes, hashes | `FileSystem` |
| JSON and supported schema subset | `JsonFile`/`JsonSchema` |
| Optional final-output optimization | embedded `minifypp` public API |

`Config` defaults to `content/`, `public/`, `.html`,
`templates/template.html`, modified-time incrementality, no extensions selected
for minification, and automatic build-thread selection. `TrackedInfo` stores
logical name/title plus per-page template, content/output extensions, and optional
minify override. `RenderResult` carries output, content-use state, a structured
source error, dependencies, and requirements.

`Parser` is a recursive single-pass renderer over source strings, not an AST or
general expression engine. It carries one result accumulator, an input stack,
code/HTML-comment depth, immutable shared JSON documents, and lexical binding
scope records. Recursion is capped at 64 and explicit stack checks diagnose input
loops. It recognizes Nift source/line comments, HTML comments, `<pre*>` escaping,
metadata/JSON `$[...]`, constrained `@if`/`@else` and `@for`, and the current
directive family (`content`, `input`, `pathto`/`pathtofile`, `getenv`, `ent`,
`json`, and `dep`). Unknown lowercase `@name` prose is preserved unless it forms
an actual parameterised call; ordinary web-language at-rules therefore remain
transparent.

The current `$[...]` implementation is exactly one renderer branch. It resolves
built-in metadata or scalar JSON paths and otherwise leaves an unrecognised form
literal. Arrays/objects selected for direct rendering are diagnosed. Crucially,
`parse_parameters` currently removes quote delimiters, performs its established
backslash handling, splits source commas, and returns plain strings; it does not
interpolate `$[...]`. Therefore parameter interpolation described later in this
document is the agreed next design, not shipped behavior. It must reuse the same
value resolver after source argument boundaries are fixed and must not recursively
parse the resolved data.

Dependencies and requirements are separate contracts. Content, templates,
`@input`, JSON/schema sources, `@dep`, and user `.deps.json` data contribute to
dependency invalidation. `@pathto`/`@pathtofile` add requirements whose continued
existence is checked without making implementation changes to an existing asset
force page rebuilds. Successful page metadata records identity, paths,
minification setting/version, dependencies, and requirements using escaped JSON.

Incremental validation checks missing output/metadata, tracked-field and path
changes, minifier contract version, dependency removal/change, missing
requirements, invalid/untrusted page metadata, and user dependency sidecars.
Modes are modified time, hash, and hybrid. Build selection and page building are
parallelized; shared source/JSON/hash/path-safety caches are mutex-protected and
rendered page state remains per parser. Outputs are written read-only before page
metadata is written, so a metadata-write failure can leave a new output with old
or absent metadata; the next build detects the metadata problem. This is the
actual recovery model, not a fully atomic two-file transaction.

The standalone `nift-regression-suite` is genuinely implementation-independent:
it takes only a candidate executable, copies its mutation-heavy historical suite
to a temporary directory, and runs focused executable-level modules. Direct
JSON/schema C++ tests stay local. On this snapshot the full independent run passed
all 14 modules, including the 578-assertion historical/ruthless layer.

## Current architectural pressures

These are observed pressures, not an instruction to refactor them all now:

- built-in metadata names are repeated across lookup, conditions, reserved-name
  validation, and `$[...]` recognition, even though they define one language contract;
- the upcoming parameter feature needs one small shared value resolver/interpolator,
  while directive execution must remain outside it;
- output and page metadata together describe a successful build but are committed
  separately;
- implementation-local and standalone contract scripts can drift without a
  machine-checkable ownership/synchronization rule;
- `ProjectInfo` and `Parser` are large coherent owners whose future extraction
  should follow real semantic boundaries, not line-count targets;
- `WatchList` has manual owning-pointer lifetime that can be modernized if no
  incomplete-type/ABI reason requires it.

For persistence, test failure and interruption states before redesigning. Establish
whether output-only commit, metadata-only commit, write failure, and process
interruption can falsely mark a page current or whether the next invocation always
recovers conservatively. Strengthen atomicity in proportion to that evidence. The
desired contract remains preservation of the last known-good result where practical,
but do not replace a recoverable design merely to make it look transaction-like.

## Identity and boundary

Modern Nift resulted from deliberate subtraction. LuaJIT, ExprTk, system
execution, hooks, and script-like template machinery were removed to reduce
semantic responsibilities, not merely source size. Its intended boundary is:

~~~text
project tracking and website generation
+ template composition and structured build-time data
+ dependency and requirement discovery
+ precise incremental rebuilding
+ filesystem and project safety
+ optional final-output minification
~~~

Arbitrary execution, general expressions, package orchestration, and
framework-specific runtimes remain outside that boundary without compelling new
evidence.

The high-level flow is:

~~~text
CLI and command dispatch
→ project/config/tracking model
→ build selection and incremental validity
→ per-artifact parsing, rendering, and graph discovery
→ optional final-output Minify++
→ output transaction
→ persistent successful-build state
~~~

Cross-cutting concerns are paths, JSON, lexical scope, watch mode, hashing and
timestamp precision, diagnostics, concurrency, determinism, and CPU/memory scale.

## Major subsystems

### CLI and project mutation

The CLI determines the operation, validates complete arity and options, dispatches
to the project operation, and returns a truthful status. It must not become a
second project model. Unknown input must not be silently ignored, and printed
failure must not return success.

Mutating commands such as track, rm, cp, and mv should validate the complete
request—names, paths, collisions, and destinations—construct candidate state, and
only then commit. Item-by-item mutation can leave half-applied commands.

### ProjectInfo and TrackedInfo

ProjectInfo historically represents the persistent and build model rather than
mere configuration. It coordinates tracked resources, extensions and directories,
path derivation, build orchestration, persistence, and project mutation. Its exact
current ownership must be mapped.

A TrackedInfo-like record connects logical tracked identity to content, template,
derived output, metadata, and build state. A tracked name such as
docs/getting-started is not the same semantic object as its derived output
public/docs/getting-started.html. Keep this distinction visible, especially in
path-facing APIs such as @pathto.

Derived content and output paths require central collision validation: tracked
items may collide with each other, authored content, generated output, or Nift
state even when a physical destination does not yet exist.

### Parser as renderer and graph discovery

Parser is the semantic center of the deliberately small template language. It
copies ordinary text, dispatches recognized @ operations, resolves $[...] values,
and handles static control flow and lexical scope. It also discovers dependencies
and requirements during rendering.

Parser state should have a clear rendering or graph reason: current tracked item,
output buffer, content-insertion state, dependencies, included files, scope stack,
and context such as comment or code-block depth. Do not restore generic scripting
residue.

Use positive lexical grammar rather than expanding blacklists of terminators.
Unknown non-Nift @ constructs must coexist with normal CSS such as @media,
@supports, and @keyframes. Ordinary HTML, CSS, and JavaScript without recognized
Nift constructs should remain essentially transparent.

Single and double quotes are the parameter quote forms. Backticks were rejected to
avoid JavaScript template-literal ambiguity. Escaping and parser-context behavior
must be shared with and derived from current tests.

### Values, operations, and parameters

The key language boundary is:

~~~text
$[...] values resolve data and produce text
@... operations perform build actions and may have side effects
~~~

There must be one semantic implementation of value lookup for metadata, JSON,
loop bindings, scope shadowing, output, and directive arguments. Never create a
second $[...] interpreter.

Parameter interpolation belongs after parameter parsing has produced a logical
text argument and before the directive executes:

~~~text
parse argument
→ interpolate values once
→ resolved string
→ existing directive path
~~~

Resolved output is data. Do not recursively reparse values containing another
$[...] or @ operation. Once partials/$[layout].html resolves to a concrete path,
normal @input validation, dependency registration, recursion handling, and error
behavior must apply. Resolved data remains untrusted; path containment belongs to
the outer operation.

If lookup is fused with output emission, make the smallest extraction that lets
both callers share it. A recursive Parser, separate scope or JSON resolver,
general expression engine, directive registry rewrite, or new dynamic graph
system would suggest implementation at the wrong layer.

### Operations and scope

@content expresses the structural relationship between a tracked page and its
template. A tracked entry may instead omit `template`; in that case its content
file is parsed directly as the top-level source, and no template dependency is
created or retained. An explicit empty template value is invalid so omission is
deliberate and unambiguous. @input both contributes rendered bytes and records a
content dependency; included-file state may prevent recursive cycles. @dep
records an external content dependency without rendering it. @json loads
build-time data, establishes a
lexical binding, and records the data file as a dependency. @pathto commonly
records a requirement rather than a content dependency.

JSON and loops use lexical scope:

~~~text
outer frame
→ push inner binding
→ resolve innermost match
→ render
→ pop
→ restore outer state
~~~

Nested loops, shadowing, empty iteration, conditions, errors, and loop metadata
must restore state correctly. Map whether bindings own JSON values or refer into a
DOM and which object owns each document before refactoring. Current tests define
loop syntax, object/array iteration, sorting, and metadata; historical pseudocode
must not create compatibility aliases.

Block parsing must respect nesting, quotes, comments, and parser contexts rather
than finding the next closing brace. Stable sorting and deterministic iteration
matter for generated output. Static control flow is not a gateway to a general
runtime or necessarily a full AST.

## Graph and incremental state

Maintain two graph edge types:

~~~text
content dependency
    changes may alter generated bytes

requirement
    target must exist or remain valid, while byte changes may not alter output
~~~

An input partial or JSON document is a dependency. A stable browser asset path can
be a requirement: changed bytes need not regenerate HTML, while disappearance or
movement invalidates the reference. Collapsing both into files-mentioned loses
incremental precision.

Dynamic selection must replace successful graph state:

~~~text
successful render uses A, B, C
next successful render uses A, D
new graph is A, D
~~~

Do not accumulate stale edges. Candidate relationships should be discovered during
rendering and committed with a successful artifact. A failed attempt selecting a
missing B must not casually destroy last-known-good state for A.

Dependency sidecars, or their current equivalent, persist relationships discovered
while rendering. Their lifecycle must cover success, failure, replacement,
removal, clean/rebuild, missing output, tracked-item deletion, and watch mode.

Incremental validity may include source, template, metadata, configuration,
dependencies, requirements, expected output, timestamps, and hashes. Modified,
hash, and hybrid modes may have different cache rules. A correct system converges:
build, edit, rebuild, then no edit means no further rebuild.

Sub-second mtimes are intentional. Hash width was strengthened after a practical
collision; it supports change detection rather than an assumed cryptographic
guarantee. A deleted output is invalid even when sources are unchanged. Metadata,
extensions, directories, templates, and minification configuration can invalidate
artifacts too. Directory dependency semantics need explicit tests.

Persistent state is untrusted even when Nift wrote it. Validate JSON structure and
types before access; never use assertions for malformed user or state data. Cover
empty, singleton, and many-item serialization, duplicate keys, valid JSON with
wrong types, corruption, and real JSON escaping. Old-format readers need an
explicit compatibility purpose, tests, and removal policy.

## Output and Minify++

The preferred transaction is:

~~~text
render complete output buffer
→ optional Minify++
→ validate replacement
→ commit output
→ commit dependency and build state
~~~

Inspect current ordering before changing it. Failed rendering or minification
should produce a controlled non-zero result and preserve known-good output/state
where possible. Do not truncate a valid artifact before its replacement succeeds.

Minify++ belongs at the completed-artifact boundary, never inside fragment parsing.
It is opt-in by configured extension. Standalone Minify++ is intended to be
canonical and the embedded Nift copy synchronized from it; verify this and prefer
machine-checkable synchronization.

## Filesystem, watch, and concurrency

Path handling is a safety boundary. Defend against parent traversal, absolute
paths, symlink escape, normalization tricks, prefix collisions, and nonexistent
destination components. String-prefix containment is wrong: /site-evil is not
inside /site. Canonicalization must account for missing destinations and symlinked
parents. Copy/move/track validation must cover logical as well as physical
collisions before mutation.

Cross-platform behavior requires direct evidence for Windows separators, drives,
case folding, permissions, timestamp resolution, symlinks, and rename-over-target
semantics.

WatchList historically represents watched-project state. Watch mode should
reconcile project/filesystem changes with incremental logic rather than blindly
rebuild everything. Initialization must create coherent state, not infer validity
from one control file.

Before concurrency changes, map which work is parallel, which Parser data is
per-artifact, which project data is shared and read-only, how diagnostics are
synchronized, and when graph/state commits occur. Per-render buffers, dependencies,
included files, scopes, and tracked data should not become global mutable state.
Use TSan or stress testing when threading behavior changes.

## Performance and determinism

Performance includes CPU, memory, allocation, and representation lifetime.
Collision validation was once quadratic. Hash structures improved CPU behavior but
multiple large tables and duplicated path/JSON representations raised peak memory.
The later staged design extracts needed data, releases heavy representations,
sorts compact vectors, and scans adjacent values.

Do not replace sort-and-scan with several unordered sets merely because lookup
looks asymptotically faster. Measure CPU, RSS, allocations, and lifetime. Watch for
per-page full-project scans, nested linear searches, repeated path construction,
and accidental quadratic work.

Large evidence should include flat 10,000-item scaling, no-op incremental, one-page
incremental, shared-dependency fan-out, realistic JSON/dependency topology,
requirements, CPU, and peak memory. Full-build speed must not hide regression in
the crucial no-op path.

Output should be deterministic for identical inputs except where explicitly
time-derived. Unordered containers, filesystem enumeration, unstable sorting,
thread completion, and current time must not silently define visible order.

## Error and testing philosophy

Prefer a specific diagnostic, non-zero status, and preservation of unrelated state
over assertions, aborts, partial mutation, or success after failure. Assertions
are for internal impossibilities, not malformed templates, CLI input, JSON, config,
filesystem state, or persisted data.

Be cautious with string views, temporary substrings, references into JSON DOMs or
vectors, and scope bindings. Ownership clarity is more important than avoiding one
small allocation.

Architecture evidence has three layers:

~~~text
focused C++ tests
+ external executable regression contract
+ the real Nift website
~~~

Use meaningful ASan and UBSan workloads; use TSan for concurrency changes.
Templates, values, parameters, JSON/schema, and state loaders are good fuzz
targets, with findings minimized into deterministic regressions.

Behavioral invariants belong in tests. Local reasons for nanosecond timestamps,
canonicalization, scope restoration, or sorted vectors belong in code comments.
Durable system rationale belongs here and in DECISIONS.md.

## External composition

Nift remains agnostic about React, Vite, Sass, TypeScript, and image tooling.
External producers run through npm scripts, Make, CI, or another task runner. Nift
consumes their filesystem results. A stable asset can be a requirement; a hashed
asset manifest loaded through JSON is a dependency because its value changes
emitted HTML. Producer-before-consumer order is normal graph causality, not a
reason to embed external orchestration in Nift.

Changes to nift init defaults also affect the barebones archive, website Getting
Started material, templates, regression fixtures, and AI context.

## Invariants and warning signs

- Nift remains a website generator, not a general scripting host.
- Values produce data; operations perform build work.
- One value resolver serves output and parameter contexts.
- Interpolation is one pass.
- Tracked identity differs from derived paths.
- Dependencies differ from requirements.
- Rendering discovers candidate graph state; success commits it.
- Persistent state and config are untrusted.
- Paths are semantic safety objects.
- Correctness outranks incremental precision: an extra rebuild is a performance
  problem; a missed rebuild is a correctness bug.
- Performance includes memory and object lifetime.
- Minify++ transforms complete artifacts and remains opt-in.
- Tests preserve discovered bug families.

Scrutinize global mutable parser environments, duplicate value resolvers, recursive
template evaluation in arguments, separate dynamic graph systems, framework
runtimes, independent Minify++ forks, string-prefix path security, project-wide
scans in page hot loops, unchecked JSON typed access, mutation before validation,
state commit before success, and visible ordering from unordered containers.

Good changes usually reuse existing semantics, add a small explicit helper, retain
graph and safety boundaries, reduce duplication, make invariants clearer, add a
focused regression, improve deterministic failure, and preserve web-language
transparency.

## Required source reconciliation

Before parameter interpolation or another architectural change, record current
files, types/functions, ownership/lifetime, tests, and historical invariants for:

~~~text
CLI and command dispatch
ProjectInfo and persistent project model
TrackedInfo and path derivation
Parser entry, text scanning, @ dispatch, $ dispatch, and block parsing
$ expression parsing, value representation, resolution, and output emission
scope frames, JSON ownership, loops, push/pop, and lookup
@content, @input, @dep, @json, and @pathto
dependency and requirement sets, sidecars, and commit timing
incremental selection, hashes, mtimes, and convergence
watch initialization and reconciliation
Path containment and collision validation
render buffer, Minify++, output write, and transaction ordering
project/config/tracking persistence and compatibility readers
parallel build and diagnostic synchronization
~~~

Trace four templates through source: metadata value output, literal @input, @json
binding, and a current-syntax loop over JSON. Then trace one mixed incremental
build and its sidecar state, one stable @pathto requirement through byte change and
disappearance, one filesystem mutation from CLI through commit, and the 10,000-item
collision-validation path.

Report material contradictions. The first reconciliation should update this
document to distinguish verified current architecture from inherited guidance.

## Production-aware roadmap

Nift is inherited as release-near, not unfinished architecture research. Finish
intended semantics, prove them, harden interactions, and release rather than adding
a large new feature set. High-information areas remain parameter interpolation,
dynamic graph lifecycle, scope interactions, state recovery, watch reconciliation,
cross-platform paths, package behavior, Minify++ failure, and JSON/control-flow
interactions.

At every substantial checkpoint ask whether work exposed architecture debt,
duplicated semantics, altered graph coherence, changed time or memory scale,
weakened transactionality, or changed production risk. Update this document,
tests, decisions, and roadmap.

In compressed form: Nift knows what a project intends to generate. For each tracked
item it renders ordinary web source through a small build-time language, discovers
the dependencies and requirements that keep the artifact valid, persists enough of
the previous successful build to test validity cheaply, contains unsafe project
relationships rather than corrupting state, and avoids owning responsibilities
outside website generation.

Keep this document living. Correct stale architecture, record new durable lessons,
and simplify the documentation whenever the implementation becomes simpler.
