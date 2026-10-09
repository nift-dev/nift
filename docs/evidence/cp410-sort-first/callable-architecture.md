# Callable ownership proposal — CP410 S1–S10

Status: design for review; no LambdaPlan/instance, capture, lifetime or frame
architecture has been implemented. Sort remains the primary performance target.

## Observable contract

The accepted executable is `4d80ca47e94beb68544904e736a9f3415b303157`, 4.10.0,
C ABI 1.3. `tests/v410_sort_factory_parity.py` preserves 31 exact baseline
stdout/stderr/exit contracts. Only the temporary fixture directory is normalized;
file, line, column, text and frames remain exact. The original 629b1f2 executable
also matches these oracles. They cover factory count/order/effects, fresh and
escaped identities, invoking escaped instances, mutable captures and rebinding,
nested/returned closures, root/path growth, imports/module ownership, errors,
unsupported keys, async invocation/transferability, empty sort and stable compound
ties. The maintained 58-case selector/location/error matrix is complementary.

Factories must still run in element/spec order. An empty array runs no factories.
A pre-created selector is an explicit different program, not permission to hoist
inline factories. Capturing all visible bindings is observable: dynamic named
calls can read an otherwise unused captured binding, and an unused timer can make
an async callable non-transferable. Global hoisting and selective capture are
rejected, not optimization candidates.

## Current ownership and duplication

`LambdaSyntax` already shares parameter/body syntax in a bounded thread-local
text cache. Each `LambdaInstance` nevertheless copies the parameter vector,
variadic/body strings, flags, filesystem path and a SourceView; it separately
owns an unordered capture table and module-environment reference. The registry
creates a fresh numeric string identity and retains the instance until parser
teardown. Prepared numeric/identity selector bodies are already cheap.

The capture table copies canonical VariableBinding state, preserving shared live
value slots and root/path references. The callback constructs another table,
then overwrites parameter names. Its local binding metadata is distinct even
when value/slot ownership is shared. Rebinding an entire Binding object must not
silently start modifying the instance's metadata.

Production async transfer copies each instance, explicitly clones captured
bindings/module graphs with memoized alias preservation, and sets the worker's
selected instance `async=false`. Sharing that mutable flag would be incorrect.

## Proposed separation

| Owner | State | Sharing rule |
| --- | --- | --- |
| Immutable syntax plan | params, variadic name, body, declared block/async kind, immutable prepared expression | share `shared_ptr<const ...>` by syntax, including across workers |
| Immutable defining-origin descriptor | exact SourceView/document/mappings, defining path and provenance | share only for the same proven lexical occurrence; never merely by body text |
| Fresh instance | fresh registry identity, complete captures, live module owner, effective async execution flag | remain per instance; clone runtime graph under existing async rules |
| Invocation frame | local binding metadata, parameters, control state, call/source context | remain distinct; shared values are resolved through existing slots/paths |

Plans and origin descriptors must never own runtime values, mutable captures,
caller scopes, root/path locations or module runtime state. The module owner
remains on the instance. Source documents and mappings may be shared because
they are immutable; a new caller origin must not replace defining authority.

A parser-owned occurrence descriptor should originate from prepared source/AST
ownership, rather than a global text cache. SourceView may contain mapped spans:
document pointer plus one byte offset is not a sufficient identity for every
generated/mapped view. Repeated bodies from different modules/files or transformed
views must retain their own descriptor. Unproven/generated occurrences should
keep their existing per-instance origin rather than invent a cache key.
The thread-local syntax cache can retain only syntax, never source occurrences.

The effective async flag stays on the instance even if declared async kind is
on the plan. Worker clones can share const plans/descriptors while remapping
captures/module environments and changing only their own effective flag. Source
ownership must outlive escaped callbacks and worker execution.

## Registry and lifetime

Keep fresh IDs, current tag strings and parser-owned retention in the first
architecture tranche. RuntimeValue callables are tagged strings; their copies,
external consumers, collections and captures do not provide a complete escape
reference count. Per-element reclamation cannot be inferred from sorting itself.
Async transfer also traverses the retained registry. Removing entries can alter
subsequent invocation, transferability and cleanup. Reclamation is a separate
review boundary requiring a real handle/escape ownership model.

Sharing syntax and origin may reduce both creation and later destruction work,
but does not remove registry nodes, fresh identity strings, all-visible capture
nodes, or callback frames. Do not claim that the entire factory phase disappears.

## Frame and key alternatives

A frame overlay could avoid repeatedly inserting captured bindings, but lookup,
shadowing, const/mutable flags, rebind/sync, root/path refresh, nested closures and
async snapshots must all retain current behavior. Borrowing the instance's
Binding objects directly is insufficient. A future design needs per-invocation
metadata or a precisely defined copy-on-write overlay; it is not authorized here.
No reused frames, arenas or speculative lifetime reclamation are proposed.

Scalar keys are already returned without deep aggregate copies. Each decorated
row currently allocates its key vector; contiguous key storage could remove those
N allocations while retaining callback order and stable ties. A single inline
RuntimeValue instead enlarges stable-sort temporary storage and can increase RSS.
The measured key phase is small. These are bounded category A candidates, but
should not displace the factory/frame ownership campaign or be forced without
material measured benefit. Stable-sort algorithm tuning is lower priority.

## Payoff and decision

The controlled post-acceptance decomposition and production allocation/free
profiles are in `sort-decomposition.json`. Phase instruction figures come from
an instrumented diagnostic build; nested phases overlap and must not be summed.
Whole-process instructions and destructor instructions/frees come from the production build.
The diagnostic destructor-body scope does not include automatic member teardown;
its `destruction` counter is intentionally not used as a parser teardown measure.
Production Memcheck xtree `totFdBk` attribution supplies the latter.

Source filename length crosses a filesystem-path small-string boundary. The
first scaling run observed three metadata allocations per instance at N=2k/4k/8k
but four at N=16k because its basename gained one character. That run is retained
in `filename-boundary-study`; it is not claimed as nonlinear N scaling. Final
probes use fixed-length directories and one common long basename at every N and
for every control, with exact official-equivalent identity-sort source content.
Pre-created selectors are constructed after the input array/loop bindings, so
their capture cardinality matches inline identity selectors. The earlier
size-only component probes are retained separately and are not official results.

Sharing metadata has a finite ceiling: its whole measured instruction phase is
only a high single-digit fraction of sort, before the new plan/origin lookups and
reference-count work. A smaller instance also reduces its allocation bytes, and
sharing eliminates corresponding metadata destructor work. These estimates are
upper bounds, not a measured production speedup. Fresh identity/captures/registry
and most callback-frame work remain. A metadata-only change cannot make a roughly
50-times-Python headline competitive.

Ranked decision: syntax/origin sharing is category B for review; capture/frame
storage and escape reclamation require further ownership proof (category C).
Small parameter/default-slot or key-storage changes are category A but have a
small ceiling and are not forced. The sorting algorithm is category D for this
campaign. Return the proposed split and controlled payoff for review; do not
implement the broad architecture under this checkpoint's design-only boundary.

## Controlled identity-sort totals

| N | Production instructions | Allocations = frees | Allocated bytes | Metadata instructions | Metadata allocations | Parser teardown instructions | Parser teardown frees |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 2000 | 55,276,480 | 60,457 | 10,651,414 | 4,288,579 | 8,000 | 5,434,309 | 22,025 |
| 4000 | 108,636,617 | 120,459 | 21,164,702 | 8,576,579 | 16,000 | 10,661,537 | 44,025 |
| 8000 | 214,527,593 | 240,461 | 42,192,070 | 17,152,579 | 32,000 | 21,331,565 | 88,025 |
| 16000 | 431,538,781 | 480,463 | 84,254,494 | 34,304,731 | 64,000 | 42,673,741 | 176,025 |

Metadata is 7.76–8.00% of measured whole-process instructions. Parser teardown
is about 9.8–9.9% of production instructions. Those are overlapping cost views,
not additive savings. The pre-created 16k control uses 300,291,502 instructions
and 288,468 allocations; hoisting the official inline factory would change
observable fresh identity, effects and captures and is not a valid optimization.

At N=2k the identity body uses 724,000 diagnostic instructions and allocates
no blocks; the whole factory phase uses 15,195,703 and the frame phase
12,287,346. Sixteen unused captures raise factory/frame phases to
38,389,497 / 38,129,827 while the key/sort phases remain essentially unchanged.
Map constructs one instance but still inserts 8,000 frame capture entries across
2,000 callbacks. These controls distinguish metadata reuse from capture/frame
work; they do not authorize dropping unused visible captures.
