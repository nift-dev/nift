# NIFT v4.10 — SORT CAPTURE/FRAME ARCHITECTURE REVIEW

**STOP FOR REVIEW.** Sort remains the primary scripting target. Recommend a bounded design combining an immutable complete capture snapshot with a mutable callback-frame overlay, and exact-descriptor snapshot reuse at factory time. Do not implement production architecture, reclamation, a callable metadata plan, or object indexing as part of this checkpoint. The preferred 20–30% whole-process identity-sort improvement has **not** been demonstrated. The ordinary-allocator model projects about 17.5% fewer whole-process instructions for identity and 16.1% for captured sort, with much larger isolated improvements for reused callbacks and large environments. This supports reviewing the broader-benefit option, not declaring the sort problem solved.

## Baseline and evidence boundary

The separate one-fetch object change is accepted. All ten exact-SHA hosted walls for `8df185e2095037512d2d7315204cd5e3991a65be` passed; see [publication receipt](publication.json), including the original Windows failure and its corrected path-normalization guard. ABI remains 1.3. The core baseline binary SHA256 is `1b70a37a77cea5328a960de3145fbdef5f5b95d1a6f0e7fa85af4e69109962a8`. No official benchmark, Labs, release, or provisioning action was performed.

[40 whole-process instruction runs](whole-core.json), [40 structural capture/frame runs](capture-counts.json), and [80 collection-scoped instruction runs](core-phase-instructions.json) cover identity/captured sort at N=2k/4k/8k/16k with 0/5/16/50/100 additional unused bindings. Callback selectors are prepared; repeated factory creation and ownership still scale with every visible binding. The cases contain no timer. Eight whole-process endpoint Memchecks report zero errors and zero exit heap. [120 isolated whole-process CPU/wall/RSS samples](core-timing.json) provide three samples per matrix cell; [medians](core-timing-summary.json) atN16k/U0 are identity86.6msCPU/87.2mswall/49,960KiB and captured97.4msCPU/98.3mswall/52,228KiB. WithU100 they grow to identity468.0msCPU/471.6mswall/289,288KiB and captured479.5msCPU/485.9mswall/291,668KiB. These are local scale probes, not new official rankings.

The private structural binary adds counting operations, and its private expression instrumentation predates the accepted object fetch change. It is used only for logical counts and allocation accounting of equivalent numeric selectors. The stock phase binary omits additional binding counters, but still has allocation hooks. Phase scopes include instrument overhead and can overlap factory scopes. They are not percentages of the uninstrumented production program. Ordinary production-binary whole-process Callgrind results are the reference for whole-language estimates.

## Capture and frame costs

`Parser::VariableBinding` is 88 bytes here. Copying it shares the RuntimeValue pointer, slot-cell owner, and optional root-slot owner; it copies type/flags, reference validity and the path descriptor vector (including owned key strings). It does **not** deep-copy the captured values. Module, source path, provenance, prepared body view, and effective async flag belong to the instance/context, not each binding.

Identity factories copy N×(U+4) binding descriptors into instance captures and the same number into callback frames. Captured selectors add one binding. At N16k/U100 this is 1,664,000 copies into instances plus 1,664,000 into frames for identity; captured sort makes 1,680,000 in each destination. All these simple cases increment the value and slot shared owners; they have no root/path aliases. Capture copying creates no new slot cells. Parameter insertion creates one explicit slot per callback plus one default slot discarded by `operator[]` when the parameter name is new; a RuntimeValue cell allocation is separate. When a parameter already names a capture, the unnecessary default insertion does not occur.

[Additional counters/oracles](additional-counts.json) demonstrate inner-scope overwrite, an alias root slot/path, module ownership, timer rejection despite parameter shadowing, and escaping factory-created handlers. The frame counter hooks cover the collection callback path; a zero frame count in a direct-call supplemental case does not mean the direct call has no capture frame.

At N16k/U0 the production identity case uses 442,817,357 instructions; captured uses 532,354,743. The identity callgraph attributes 56,969,810 inclusive instructions to `copy_capture_bindings` and 45,890,922 to the **actual** `Parser::~Parser`, including member destruction. This teardown is meaningful work. A phase around only the destructor body would omit the member destruction; its counter is not a full teardown estimate. Do not add overlapping callgraph/phase totals.

## Required semantics

[Lookup oracles](lookup-oracles.json), existing 31 sort/factory oracles, and supplemental cases establish these rules:

| Context | Existing behavior to preserve |
| --- | --- |
| Captured names | Inner visible scopes replace outer names at creation; the resulting names/descriptors are snapshotted. Values remain live through shared slots. |
| Previously absent names | Later caller/global scopes can supply them; this is not a closed lexical environment. |
| Parameters | Replace same-name capture bindings in the fresh frame; do not rebind the caller's slot. |
| Declarations | An unhydrated captured name still counts as declared in the current frame, **except the existing injected cmd/args redeclaration allowance identified by is_script_invocation**. A replacement parameter/local binding must lose that exception as today. |
| Bare identifiers | Module/global named-callable resolution can precede variable lookup. |
| Callable invocation | Builtins and special diagnostics have their own precedence; indirect callable bindings can precede named-function fallback. Preserve each existing resolver path. |
| Variable helper | Reverse scope lookup, then receiver fields, with module scope switching/restoration where applicable. |
| Mutable alias metadata | `sync`/`rebind` may change cached value, path validity and descriptors per frame. Never expose the immutable capture descriptor as a mutable frame binding. |
| Nested factories | Capture all visible bindings, including untouched snapshot entries and hydrated/parameter/local overlays with their shadowing precedence. |
| Async transfer | Examine cached value, live slot, root slot and nested callable/module reachability. Unused timer captures, even a subsequently shadowed parameter name, can prevent transfer. |
| Instance metadata | Keep source owner/origin, module owner, provenance, fresh identity and effective async flag per instance. A worker's async override must not alter siblings or shared metadata. |

Module entry currently copies global/module variable maps and retains/restores caller scopes. This proposal does not claim to eliminate those copies. Existing name-resolution, reference-root authority, diagnostics and hidden named-function captures remain separate responsibilities.

## Models and measured tradeoffs

Isolated C++ models use the actual binding layout and slot/root synchronization. They do not instantiate a Parser or implement Nift's full factory dispatch, registry tag strings/hashing, prepared AST, source metadata or worker cloning. A retained environment vector represents registry lifetime. The expanded model also captures later caller names when creating a nested closure; erasing/recreating the caller binding must leave that closure attached to the original slot. The real-runtime oracle returns11 after the caller scope exits and a new global late=22 is declared. Early v1/v2 nested profiles omitted this extra caller binding and are historical decomposition data; use the expanded/ordinary-allocator model for nested performance. Local assertions exercise live slots, parameter shadowing, captured-name membership, later caller fallback, vector growth, parent rebinding, root-path aliases and nested capture flattening. [Supplemental semantic variants](supplemental-semantics.json) also assert the injected cmd redeclaration exception in all five models, matching the real-runtime oracle. The performance models use ordinary numeric marker bindings and do not constitute a VM semantic certificate. Named-callable priority is an external resolver contract, not a newly implemented resolver in these models.

[Four-model allocation profiles](models.json) contain 160 cases; [snapshot-memo profiles](memo-models.json) contain 90. All pass exact-output comparison and Memcheck with zero errors/exit heap. [Expanded lookup/mutation/teardown profiles](model-extra.json) add 226 instruction cases; [extended memory checks](model-memory.json) add 58 cases, with peak RSS at N16k and Memcheck at N2k. [88 ordinary-allocator instruction runs](plain-models.json) avoid charging custom allocation-count hooks as ownership savings. Use those ordinary-allocator results for the table:

| N2k/U0 subsystem | Current instructions | Overlay | Exact snapshot memo + overlay |
| --- | ---: | ---: | ---: |
| Fresh identity factory/callback/retention/teardown | 14,876,557 | 11,137,361 (−25.1%) | 5,176,007 (−65.2%) |
| Fresh captured selector | 17,510,660 | 14,021,581 (−19.9%) | 6,765,894 (−61.4%) |
| Reused identity callback | 6,722,124 | 2,978,562 (−55.7%) | 2,978,774 (−55.7%) |
| Reused captured callback | 7,812,299 | 4,306,214 (−44.9%) | 4,299,469 (−45.0%) |
| Source metadata rebound every factory | 18,879,417 | 15,399,591 (−18.4%) | 16,557,525 (−12.3%) |

The overlay copies complete captures once per fresh factory, then adds parameters/locals and lazily hydrates captured bindings on lookup. It therefore reduces frame work but leaves factory capture and registry memory. At N16k/U100, current and overlay identity model peak RSS are both about 256 MiB; the memo model is about 6.4 MiB because instances share one stable snapshot. Those are model RSS results, not production RSS forecasts. With descriptor changes every factory, memo loses that memory benefit and is slightly larger than current.

The immutable parent-chain model is an optimistic stable-source lower bound (−71.7% identity subsystem at U0), not a safe implementation: it cannot preserve a fresh metadata snapshot after arbitrary external binding rebinding without persistent versions. It is deliberately excluded from the changing-source scenario. Naive COW makes a full copy on every parameter insertion and gives essentially no benefit. Reject it as a production direction.

Lookup cost matters. At U0 with 256 repeated missing-name reads, the hooked memo model is **20.7% slower** than current and the simple parent model is 71.5% slower. Parameter/local/captured lookups retain savings but amortize construction less. These percentages include model instrumentation and are not production regressions; they identify a real extra immutable-map miss lookup. A production design needs a measured lookup policy: for example, promote a frame to full materialization after enough repeated misses, copying only untouched capture entries and never overwriting parameters/locals or hydrated mutable metadata. No universal threshold is selected yet. Do not cache caller values/absence across mutations.

Scaling the ordinary-allocator U0 identity subsystem saving by eight estimates 77,604,400 saved instructions, about **17.5%** of the measured 442.8m whole program. Overlay alone projects about 6.8%. Captured sort projects about 16.1%. These are feasibility estimates, not measured production speedups or wall-time predictions. At large U, subsystem improvements are much larger, but the acceptance bar still requires production whole-process validation. Fresh identity-sort 20–30% is not yet proven; broader callback and memory benefits are the justification for considering the smaller headline option.

## Proposed bounded design

1. Keep the existing fresh factory instance and ID on every evaluation. Share only an immutable **complete resolved capture map**, not the instance, and do not hoist the factory.
2. Maintain a bounded parser-local last-snapshot memo as a weak reference to a snapshot already owned by a registered instance. Publish it after successful registration; do not introduce extra resource retention beyond the registry.
3. Reuse a snapshot only after exact comparison of every resolved visible descriptor/name and name count, with inner-scope precedence. Compare both raw pointer and shared-owner identity for cached value, slot and root; compare type/flags, validity and complete path descriptors. Slot/root equality alone is insufficient: cached values themselves own resources. No hash-only match, raw interior-pointer cache, free-variable selection, or broad mutation epoch guessed to be complete.
4. A frame owns parameters/locals and mutable hydrated descriptors. Capture membership and the injected cmd/args exception participate in redeclaration checks before hydration. Variable lookup hydrates a private copy before sync/rebind; later caller fallback remains available for absent names. Nested capture enumeration merges untouched snapshot bindings with overlays in current shadowing order.
5. Keep module/source/effective-async context per instance. Update async cloning and transfer/reachability enumeration to visit shared snapshots fully, including hidden/unused captures and root slots. Keep worker-owned overrides local.
6. Measure cache-hit, cache-miss, wide captures, alias metadata, dynamic caller fallback and heavy missing lookups before choosing a promotion policy. Run the existing factory/order/source/module/root/corruption/async oracle walls and actual whole-process benchmarks before acceptance.

The cache avoids repeated map node allocation and shared-owner increments but still enumerates/computes the final all-visible name set. The flat-map model excludes production scope-stack shadow resolution, which can erode the projection. A simple generic first implementation can reuse only when total visible entry count equals snapshot cardinality and each descriptor matches; any duplicate/shadowed name falls back to the existing complete copy. This conservatively misses sharing opportunities without allocating a fresh seen-name set or weakening capture semantics. A broader exact shadow-aware comparator needs its own measured cost. It does not promise O(1) creation or avoid dynamic metadata misses. A parser-local weak memo also needs exception-safe lifetime and teardown-order review. These are implementation prerequisites, not work already shipped.

## Registry and escape proof

Registry-held tagged strings do not supply callable reference counts. Instance identity, lookup, nested factories, saved callbacks, returned closures, module graphs, hidden named-callable captures and worker cloning all depend on registry reachability. A factory can save the handler while returning it to sort; later calls and distinct identities are observable. Even an apparently inline selector is not a sufficient reclamation class without proving no factory side effects or hidden escape. **No reclamation class is accepted.** Keep parser-owned instance retention and IDs unchanged. Snapshot sharing reduces duplicate capture storage while respecting that lifetime.

## Canonical Jsonic++ side investigation

Only scratch copies of `/home/nick/Repositories/nift/jsonic/jsonic/include/json.h` were changed. The canonical and vendored headers remain unchanged. The prototype uses owned normalized strings in a temporary strict-duplicate membership set above a threshold; ordinary ordered storage and public object layout remain unchanged. No string views into vector/SSO storage are retained.

[275 profiles](jsonic-thresholds.json) cover widths 8/16/32/64/128/256/512/1k/2k/4k/8k, unique keys and early/middle/end/escaped-equivalent duplicates at thresholds 16/32/64/128. [220 additional semantic/Memcheck comparisons](jsonic-validation.json) verify zero errors/leaks and exact offsets/messages, nested duplicates, malformed colon/value precedence, malformed Unicode, nesting, trailing comma and Preserve mode. [70 ordinary-allocator instruction comparisons](jsonic-plain.json) validate threshold32 without allocation-hook cost.

Provisional threshold **32**: threshold16 regresses width32 unique parsing by about 29% in the allocation-profile build. Threshold32 leaves width32 at about +2.1% ordinary-allocator instructions, improves width64 by 8.3%, width128 by 33.0%, and width8k from 1,040,157,781 to 12,887,494 instructions (−98.76%). Width8 has ~1.6% fixed instruction overhead and no extra heap allocation. At width8k the instrumented requested-payload counts increase from 16,011 to 24,019 allocations and peak live payload from 1,671,168 to 1,911,848 bytes (+14.4%). This peak excludes allocator headers, libc allocation and stack usage; it is not RSS. Early duplicates terminate before building an index. Duplicate checking remains before colon/value parse, preserving diagnostic precedence.

Recommendation: review the canonical-library strict-parser threshold32 design separately; do not sync or implement it here. Additional stack/depth and allocator/collision/platform checks belong to its implementation checkpoint. No persistent object index, ABI or duplicate-policy change is proposed.

## Decision

Review snapshot memo plus mutable frame overlay as the next **sort-first architecture** tranche. Accept neither production speedups nor a final miss-promotion policy from these models. Keep the full semantics and registry retention; do not substitute capture selection, COW, stable-parent reuse without versions, or reclamation. The separate filesystem candidates and their local certificate are documented in [the shell review](../cp410-shell/report.md). New shell changes remain local; the ten green hosted walls certify the accepted baseline only.
