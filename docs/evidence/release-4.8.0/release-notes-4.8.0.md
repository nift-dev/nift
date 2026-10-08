# Nift v4.8.0

Nift v4.8 adds agent-oriented transformation workspaces, improves scripting hot
paths, fixes prepared/ordinary evaluation parity, and reports diagnostics against
original source text. It follows v4.7.2; the C ABI remains 1.3.

## Language and correctness

- Added `warn(value)` in scripts and templates. It emits a non-fatal canonical
  warning to stderr, preserves ordinary/template output, continues execution,
  and returns `null`.
- Computed bytes indexes now work consistently in ordinary expressions,
  functions, arguments, prepared loops and templates. Existing bounds and type
  errors remain in force.
- Direct condition equality on arrays, objects, bytes and nested collections
  now uses the same deep equality as ordinary expressions and prepared bodies.
  Ordering comparisons remain restricted to numbers and strings.
- Named functions used as first-class values now retain parity when passed,
  assigned or returned inside prepared loops, bodies and templates. Callable
  identity, shadowing, argument checks and throw behavior are preserved.
- Fixed stale location-backed method receivers after a parent array reallocates
  or the referenced location is removed.
- Object-literal arguments containing commas are split correctly rather than
  being mistaken for multiple call arguments.

## Scripting performance

- Scalar-key map lookups/replacements and set membership use auxiliary indexes
  instead of repeatedly scanning successful hits. Authoritative ordered storage,
  iteration and replacement order are preserved. Reference-bearing/marked keys
  and non-injective numeric keys retain compatibility handling. Key removal and
  sorted-map mutation still require linear work; ordinary object member lookup
  is not redesigned. Indexes add some per-key memory overhead.
- Prepared object literals and supported collection method calls avoid repeated
  legacy expression parsing in loops and templates. Object member values retain
  left-to-right, exactly-once evaluation, ordering and duplicate-key rejection.
  Unsupported shapes continue through the compatibility evaluator.
- Plain identifiers resolve earlier through the canonical resolver, preserving
  named-callable precedence, references, receiver synchronization and errors.
- Bounded immutable lambda syntax and pure numeric expression plans reduce
  callback parsing work. Each invocation retains fresh closure identity, live
  captures and current bindings; factories are not hoisted. Unsupported syntax,
  operand types and deeper expressions keep existing evaluation paths. Numeric
  overflow, large-integer precision and error behavior remain protected.

These are workload-specific improvements, not a universal build-speed promise.
Existing measurements are scoped evidence; no new public benchmark campaign was
run for this release preparation. Retaining diagnostic provenance also has a
measurable time/memory cost on some workloads.

## Original-source diagnostics

Diagnostics now retain canonical original-source provenance through translated
syntax, prepared execution and legacy fallback. Errors in loops, expression
slices, callable definitions, imports, templates/includes, struct initializers
and worker execution point to the original file and source location rather than
normalized or generated evaluation text. Definition origins survive repeated
execution and worker transfer. Error codes, disposition and callback/error
boundaries remain unchanged; consumers should expect corrected locations and
source excerpts where earlier attribution was inaccurate.

## Migration, rewrite and redesign workspaces

- `nift init --migration` creates a parity-first workspace with canonical
  `MIGRATION.md`, managed `AGENTS.md`, `HANDOVER.md`, `README.md`, and resumable
  investigation records. It scaffolds a project; it does not automatically
  convert an upstream framework or complete a migration.
- New **experimental** `nift init --rewrite` preserves the product, design,
  content, routes and behavior while allowing a new implementation. Its
  `REWRITE.md` and investigation records freeze reference/design/behavior
  contracts rather than copying the old architecture.
- New **experimental** `nift init --redesign` treats the old project as
  requirements and content evidence. Its `REDESIGN.md`, design brief and route
  map require explicit decisions for changed design, functionality and routes.
- `--migration-existing`, `--rewrite-existing` and `--redesign-existing` accept
  `error|keep|append|replace`. Canonical conflicts fail closed by default;
  unrelated AGENTS content is preserved. Cross-mode contracts, malformed
  managed markers, mismatched policies and mutually exclusive modes are rejected
  before scaffold writes. Repeated managed-block replacement no longer
  accumulates owned blank lines. Ordinary init and handover behavior are retained.
- Transformation intent is independent of authored, rendered or hybrid source
  models. React, Vue, Svelte, Solid, Web Components and vanilla JavaScript islands
  may coexist with Nift-generated pages. Browser bundles are prepared separately;
  Nift does not claim to compile every client framework natively.
- All three workbooks require complete transformation and acceptance, a measured
  performance campaign, full revalidation, final production-pipeline benchmarks,
  clean-checkout proof and handover. Changed redesign workloads require qualified
  comparisons rather than artificial parity or speed claims.

Migration has production-corpus dogfooding evidence. Rewrite/redesign are
experimental agent workflows and do not have that same evidence base. Internal
production-scale hardening does not establish widespread independent production
operation or long-running live-traffic maturity.

## FFI, embedding and compatibility

- FFI buffer lifetime is documented explicitly: storage belongs to the active
  Parser; an embedded Engine retains its script Parser across calls. Dropping
  a script handle, leaving a function or destroying a result does not release
  the buffer. Reuse fixed-size buffers for repeated native work where practical.
- Buffer/pointer addresses require the owning Parser/Engine to remain alive.
  Assignment and `deepcopy` retain opaque identity; copying a result or marker
  does not extend native resource lifetime. Failed import rollback may invalidate
  newly created resources. `ffi_close` closes a library, not its buffers.
- There is **no explicit buffer-release API** in v4.8. Reclamation/API redesign
  remains deferred. Existing callback, ownership and native-library boundaries
  are preserved.
- **C ABI remains 1.3**, with no public C header/signature changes from v4.7.2.
  No existing command or language surface is removed or deprecated by this delta.
  The new `warn` primitive and init flags are additions; evaluation parity fixes
  intentionally correct earlier inconsistent behavior.

For upgrades, review newly corrected diagnostic coordinates, use the workbook
matching the intended transformation contract, and choose existing-file policies
explicitly when adding guidance to user-owned files. Treat rewrite/redesign as
experimental. Native integrations must obey Parser/Engine resource lifetimes.

## Certification and release status

The frozen implementation passed Linux/macOS/Windows (MinGW) certification,
GCC/Clang first-party core and embedding warnings-as-errors, sanitizer and
parity/scaling gates, 93 independent Nift contract modules, and 12 pinned package
contract modules. Package ecosystem compatibility is release-blocking; package
sources and certification identities are recorded rather than inferred from a
successful build. The installer smoke now deterministically corrupts negative
checksums, including digests whose first byte is `00`.

These reviewed notes prepare the v4.8.0 release. A passing Release artifacts
rehearsal and explicit release authorization are still required before tagging
or publication. The existing fail-closed reviewed-notes requirement is retained.
