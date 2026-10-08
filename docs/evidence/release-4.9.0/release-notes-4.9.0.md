# Nift v4.9.0

Nift v4.9 is a performance and hardening release following v4.8.0. It reduces
shared scripting, collection, JSON-conversion and filesystem-ordering overhead
while preserving existing language behavior, callable identity, ownership and
original-source diagnostics. The C ABI remains **1.3**.

## Callback and collection performance

- Numeric identity callbacks use the canonical live binding directly when
  eligible. Bounded indexed selector plans also prepare supported array reads,
  such as `x => a[x]`, while resolving current bindings and copying results at
  each invocation. Unsupported shapes/types and failures retain compatibility
  evaluation and diagnostics.
- Numeric object-member selectors, such as `x => x.v` and supported arithmetic
  or comparisons on direct members, use bounded prepared plans. This improves
  aggregate sorting and grouping without introducing persistent member indexes
  or caching mutable runtime values. Missing members, structs, nonnumeric
  operands and unsupported shapes retain existing evaluation paths.
- Sort decoration moves private owned receiver snapshots after evaluating their
  keys, reserves known capacities and moves values into the result. Stable ties,
  multi-selector factory order, callback counts and source-array independence
  remain unchanged.
- Callback argument vectors are constructed directly, avoiding intermediate
  const initializer-list copies. Owned map/filter results also move into their
  output collections instead of being copied again. Aggregate parameters retain
  their existing binding/value semantics.
- Ordered `group_by` construction uses a temporary key-to-position index to
  avoid repeated scans of the growing output object. Insertion order, rendered
  key collisions and independent owned group values are preserved. The index
  adds bounded temporary memory; this is not an object-storage redesign.
- Prepared scalar map/set reads reuse their already-computed canonical key for
  special numeric handling instead of performing the same fingerprint work
  twice. Numeric equality, NaN handling and canonical compatibility fallbacks
  remain unchanged. The scalar indexes themselves were introduced in v4.8.

## Calls, JSON and filesystem work

- Ordinary bare value arguments avoid redundant argument-location parsing after
  live handle checks. Nested logical-location aliases retain canonical probing,
  and location scratch storage is created only when an actual location resolves.
- Named and native outcome dispatch transfers owned argument vectors when there
  is a sole consuming handler. Secondary-handler retries retain untouched
  arguments even if the primary consumes its copy and returns Unsupported.
- Prepared synchronous functions share an immutable owned defining path rather
  than repeatedly copying it into successful call contexts. Defining-file
  authority, source views and diagnostic origins are preserved; this is not
  borrowed source/path storage or a call-frame redesign.
- Runtime-to-JSON conversion performs one recursive timer-resource preflight at
  the public conversion boundary, rather than repeating it at every descendant.
  Unsupported/opaque resources remain rejected, and failure leaves the caller's
  output unchanged. This improves conversion/serialization of nested values;
  the vendored Jsonic++ parser is unchanged.
- Filesystem glob ordering computes each existing generic path key once and
  sorts by that exact key, avoiding repeated path serialization in comparisons.
  Ordering, absolute-path spelling, matching, symlink behavior and directory
  traversal restrictions remain unchanged. Temporary ordering keys consume
  bounded extra storage.

These are general runtime improvements with workload-dependent gains, not
portable speed guarantees. Independent local profiles and mixed-workload,
allocation and lifetime controls support the retained changes. They are not
new official release benchmark results. The historical workload called
`sort-index` uses `sort_by(x => x)`; it must not be confused with the indexed
selector `x => a[x]` or assigned that selector's measured gains.

The frozen official `20261008-v480` series remains unchanged. The official
post-v4.9 benchmark campaign is intentionally deferred until after release.

## Binding maintenance and compatibility

- Maintained Node and Python native wrappers now pass strict GCC/Clang
  warnings-as-errors checks without warning suppressions. Unused callback
  parameters, misleading control-flow formatting and Python type/module
  initialization were corrected without changing their public APIs.
- Maintained Go, C#, Node and Python binding tests and aggregate build gates
  remain green; Go/C# warning checks are also maintained. These are experimental
  wrappers. This release makes no new formal binding publication or support
  commitment.
- Public C headers/signatures and C ABI **1.3** are unchanged from v4.8.0.
  No command or language syntax is removed or deprecated by this delta, and no
  new major language feature is introduced.
- Callback evaluation order, fresh callable identity, live captures, aliases,
  logical root/path references, async/future behavior, module ownership and
  original-source diagnostics retain their existing contracts. Added parity,
  deterministic scaling, ownership and fallback controls protect the optimized
  paths across Linux, macOS and Windows (MinGW).

The v4.8 migration/rewrite/redesign workspaces remain available with their
existing contracts. Rewrite/redesign remain experimental; they are not new
v4.9 features or newly established production-maturity claims. Native resource
lifetimes and the absence of an explicit FFI buffer-release API are unchanged.

## Remaining costs and deferred work

Identity sorting still evaluates selector factories per element, creates fresh
callable instances and copies visible capture environments. Those costs have
been measured separately from its already-prepared numeric body. This release
does not claim to fix the remaining identity-sort architecture.

Capture-map insertion tuning, owned sort-key layout experiments, callable
plan/instance separation, capture-set or frame/arena redesign, persistent slot
or object indexes, RuntimeValue/storage changes and VM/JIT work are deferred
until after v4.9. None is a release blocker. Factories are not globally hoisted,
and apparently unused captures are not removed based only on syntactic
free-variable analysis.

## Certification and release boundary

The retained runtime passed native/binding walls, GCC/Clang first-party and native
wrapper warning gates, sanitizers, independent NRS 93/93 and pinned PRS 12/12,
deterministic performance guards and Linux/macOS/Windows hosted certification.
The release preparation repeats the authoritative local matrix and exact-SHA
hosted readiness checks before the non-publishing artifact rehearsal.

The canonical rehearsal stages four public archives: Linux x86_64, macOS ARM64,
macOS x86_64 and Windows x86_64, plus their SHA256SUMS manifest. Build-only Linux
ARM64 package evidence does not add a portable release archive. Installer
preflight, live-installer byte equality, extracted-archive smokes and exact-set
inspection remain release gates. These reviewed notes do not authorize a tag,
publication or Chocolatey submission; explicit final release authorization is
required after rehearsal and artifact inspection.
