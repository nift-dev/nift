# NIFT v4.10 — SILVERBACK IMPLEMENTATION FINAL

The bounded implementation campaign retains callback capture/frame overlays, operation-local traversal prefixes, canonical prepared filesystem recipes, bounded large-string preparation, and FileValue ownership/dispatch changes. Ordinary sort and tiny-save CPU remain below their ideal targets. A custom concat reader is rejected. Further work needs an architecture decision, rather than another series of small trims.

Starting SHA: `b1267a5b1a2224ec3a230d2640d2e0ad9314992c`.
Starting binary SHA256: `7dd0fe8b34d13486e10b180b5ddf8c51ba76658c5c5ed5cc64cb3a8b16821b89`.
Final native binary SHA256: `f3f999b247fe5375902306f983311402225ca279947dd1b16a6073830db62dd0`.
The final repository head includes the test-fixture portability correction; its exact SHA and hosted receipt are provided in the completion response. Main and the final complete certification stage have identical native binaries.

These are private controlled measurements. The frozen official v4.9 and expanded-shell series are unchanged. Paired CPU measurements compare binaries within each trial batch; timings from different batches are not mixed to invent speedups.

## Sort — fresh final-head reprofile, 16k values

Six alternating starting/final pairs for each row, with byte-exact outputs.

| Selector / unused bindings | Before CPU ms | Final CPU ms | CPU speedup | Before instructions | Final instructions | Before/final RSS KiB |
|---|---:|---:|---:|---:|---:|---:|
| Identity / 0 | 97.6815 | 75.5525 | 1.29× | 442,821,117 | 370,904,510 | 50,034 / 38,434 |
| Captured / 0 | 113.016 | 81.8755 | 1.38× | 532,355,510 | 412,794,222 | 52,258 / 38,518 |
| Identity / 100 | 539.470 | 106.552 | 5.06× | 3,008,310,286 | 720,446,524 | 289,340 / 38,558 |
| Captured / 100 | 492.261 | 104.9385 | 4.69× | 3,096,076,914 | 764,381,887 | 291,706 / 38,614 |

Identity allocation counts: ordinary 512,480 → 352,521; wide 3,809,294 → 353,438. Final identity Memcheck runs have zero errors and zero exit heap. The complete A matrix covers 2k/4k/8k/16k and 0/5/16/50/100 unused captures, with map/filter/group/closure effects: ordinary instructions improved roughly 20–22%, wide cases roughly 83–84%. A teardown measurements fell approximately 45.9M → 27.9M instructions ordinary and 419M → 28M wide. Fresh callable identities and retention remain unchanged.

KEEP meets the explicitly allowed conditional criterion: about 16–22% ordinary instruction improvement plus major callback, width, RSS and teardown improvements. It does not demonstrate the ideal 2× ordinary-sort target. Remaining costs include complete capture snapshot validation, factory/instance setup, registration and retained-instance teardown. The accepted wide profile attributes about 346M instructions to snapshot construction/validation; final wide totals remain about 720–764M. No general scope or ordinary named-frame replacement ships.

## Traversal — certified B pairs

| Shape | Before CPU s | After CPU s | Speedup | Shared-prefix native s |
|---|---:|---:|---:|---:|
| Wide | 1.710402 | 0.936677 | 1.83× | 0.918874 |
| Deep | 2.636640 | 1.143996 | 2.30× | 1.101940 |
| Mixed | 1.525986 | 0.672701 | 2.27× | 0.702656 |

Wide instructions 4,376,085,358 → 3,308,787,651; allocations 5,640,343 → 4,440,369; allocated bytes 953,069,934 → 771,886,229. Readlinks 1,200,016 → 25; metadata calls 240,012 → 120,013; getdents 173 and cwd calls 3 remain unchanged. One shared resolved prefix replaces full per-terminal display-path materialization; 120k final RuntimeValues are still required. RSS changes are about +1% wide/deep and +6.5% mixed, not a universal memory win.

KEEP. Prefixes are operation-local, with canonical symlink/ambiguous fallback, absolute sort and dedup. The old glob grammar remains exact, including its literal bracket behavior. Remaining costs are enumeration/status, sorting and complete output ownership; CPU approaches the measured native control. No atomic filesystem-snapshot claim is made.

## Filesystem — certified C pairs, 10k paths

| Operation | Before CPU s | After CPU s | CPU speedup | Native CPU s |
|---|---:|---:|---:|---:|
| Stat/metadata | 0.0726325 | 0.0679480 | 1.07× | 0.032704 |
| Move | 0.2424665 | 0.1151190 | 2.11× | 0.037435 |
| Copy | 0.3085805 | 0.1807430 | 1.71× | 0.099738 |

At 1k paths, stat instructions 43,349,876 → 36,683,772 (native 4,847,703); move 228,496,206 → 73,429,154 (native 6,247,283); copy 230,069,654 → 74,007,026 (native 6,716,576). Move/copy instruction improvement is about 3.1×, while their remaining instruction totals are still roughly 11–12× native. Allocation counts fall 58,288 → 55,301 stat, 167,305 → 122,339 move, 168,307 → 122,342 copy. RSS is essentially neutral.

KEEP. Recipes preserve original speculative/canonical evaluation counts, ordering, source spans, source/glob validation before destination arguments, and cwd-changing factories. Native copy I/O is unchanged. Stat still pays rich object/value and path/dispatch costs. Copy's native CPU is already about 100 ms; the kernel floor limits the end-to-end multiple.

## Save and concat — final D pairs

Tiny save, 1k × 128 bytes: CPU 36.480 → 30.0145 ms, 1.22×; instructions 204,530,528 → 120,457,677, 1.70×; RSS 8,202 → 8,232 KiB. The native atomic control is 11.501 ms CPU, leaving a 2.61× gap. Its omitted source/value/policy/ID/state work makes it a lower bound, not proof of an unavoidable semantic floor. The 2× save target is missed.

Conditional KEEP for the shared canonical executor/recipes and ownership result. At 32 × 1 MiB write/save, allocated bytes 139,180,619 → 72,064,543 (−48.2%) and allocations 3,298 → 2,898. Direct completed buffer-assignment counters measure clean rw open/close copies 64 MiB → 0; fresh write/save copies 32 MiB → 0; mutation/revert copies 96 MiB → 64 MiB. Those counters exclude stream reads, argument/RuntimeValue copies and mutation replacements.

Clean rw-open/close retained capacity for 32 existing 1 MiB files changes 64 MiB → 32 MiB plus 480 bytes of empty saved String capacity; the repeat capacity unit measured RSS 73,372 → 42,436 KiB. All 32 closed opaque shells remain registered and reopenable. Mutated files can still legitimately retain both capacities. Atomic save and permissions/error behavior remain intact.

The custom concat sink and prepared open-reader recipes are DROP: large-input instructions increased about 48%, despite only 7–10% pilot CPU gains. The original reader remains. A trivial two-line move of validated temporary Strings is retained as part of file ownership. Against the recipe-only candidate, large concat instructions 55,527,624 → 51,808,290 and allocated bytes 201,047,367 → 167,492,915; CPU 80.073 → 78.603 ms. Tiny concat is neutral (13.008 → 13.262 ms). This is not a streaming architecture win. Complete working output remains necessary for existing read/dirty/revert/alias semantics.

## Strings — certified E pairs

Whole large-string assignment: CPU 345.8675 → 135.5505 ms, 2.55×; wall 347.420 → 137.160 ms; instructions 8,183,733,401 → 2,888,914,775, 2.83×; allocations 59,279 → 20,332; allocated bytes 553,512,174 → 235,586,141. RSS 7,504 → 7,742 KiB (+3.2%). Native CPU is 69.9765 ms.

KEEP. Bounded literal/structure preparation and assignment dispatch reuse the canonical linear replacement builder. Ordinary AST limits remain intact; syntax cache is 16 entries/4 MiB of accounted owned capacity, with no values or bindings. Scaling is neutral at 8 KiB and about 2.1× instruction improvement at 16–128 KiB. Source counts/order/failure behavior match 37 permanent oracles. Remaining costs include source processing, comparisons, copying/appending and character conversion. The unsafe 4.6× prototype and the superseded RHS-only 1.6× variant are not production results.

## Retained, dropped and workload recommendation

Retained: callback-only immutable capture descriptors/overlays with exact weak snapshot reuse; general operation-local traversal prefixes; canonical prepared filesystem operations; bounded large-string preparation/assignment plus linear replace; FileValue lazy saved snapshots, bounded recipes, original atomic lifecycle and temporary String ownership.

Dropped: the general scope wrapper (loop/scalar regressions), unsafe evaluate-once filesystem/string prototypes, terminal-prefix lookup regression, naive shrink-capacity/registry reclamation, both custom concat sinks/open recipes. Plan/metadata sharing is deferred until its measured residual payoff justifies complexity. Persistent RuntimeValue indexing, Jsonic++ changes, VM/JIT and general scope/named-frame replacement are excluded.

F actual-workload instrumentation: official-equivalent JSON transform/traverse maximum object width is 5 (loop metadata); data records have a few keys. Transform totals include 12k lookups/8k assignments/2k source copies; traversal 48k/40k/24k. Four real scaffold workflows and the actual 173-artifact website have maximum observed width 0. Config/tracking files have width at most 16 in their separate representation. No 2k/8k repeatedly read RuntimeValue object regime was observed. Observer coverage is explicit: direct public field/vector mutation can bypass assignment tracking; range counts are starts, not inferred visits.

Recommendation: defer object indexing. The synthetic 114× lookup result does not amortize its roughly 5.3× cold-copy regression in these narrow, copy-heavy workloads. Public mutation/invalidation still requires a separate design. Jsonic++ remains untouched.

## Safety, real builds and hosted walls

Final D: 102 independent file/depth/String-slot oracles, C's 97 filesystem/pure-operand oracles, prior string/glob contracts and bounds guards; native/bindings; GCC/Clang and binding warnings; full lifetime ASan/UBSan/LSan plus deep sanitizer; six save and six concat Memcheck controls; normal/lifetime core-memory each 57 phases × four rounds; fuzz 1,219 cases (232 builds, 987 controlled errors); NRS 93, PRS 12; ABI **1.3**. Main matches the certified binary. Other retained tranches have their own complete safety receipts.

The expanded oracle reproduced an accepted lifetime-sanitizer stack overflow. Independent ASan-only dispatch frames reduce the compatibility frame 218,128 → 39,760 bytes and make the existing depth guard reachable for valid nested named/native/method cases. Stack limits, language depth limits and tests were not weakened. An initially invalid builtin probe was corrected; its genuine failure was fixed, not rerun away. All failure evidence remains in D.

Default/migration/rewrite/redesign init/status/full/incremental workflows match artifacts. The actual website matches all 173 public hashes. No material real-build speedup is claimed. Ordinary final controls versus E: loops/scalar/no-arg/BFS/JSON traversal within 0.2%, map/set slightly lower, closure/callback instructions +1.27%/+1.11% disclosed.

A, B, C and E each passed ten exact-head non-release hosted walls. D's introducing head is published only after its local certificate; the ten exact-head hosted outcomes and final SHA are verified and reported in the completion response. The earlier interrupted B deep build remains recorded without an invented cause; all retained final certificates pass.

All 81 frozen official/expanded hashes remain unchanged. No Linode, official rerun, release/publication, Labs presentation, dependency development pin or synchronization-policy change occurred.

## Three worst remaining problems

1. **Sort/callable factory and complete capture snapshot costs.** Ordinary sort remains far below the ideal 2× target. Fresh identity, complete live capture descriptors and retention constrain shortcuts; the next architecture must preserve those contracts.
2. **Filesystem userspace/value/path machinery.** Move/copy still have roughly 11–12× native instruction totals; stat roughly 7.6×. Save still has a 2.61× CPU gap to its simpler atomic control. Existing rich values, policy, source and lifecycle work require more deliberate execution architecture.
3. **Broader collection/JSON/BFS runtime/value costs.** Narrow objects and repeated copies, frames and generic source/value work remain. The evidence does not justify optimizing an already competitive dependency or adding a wide-object index.

Recommendation: **architecture decision required**. This bounded campaign is complete after the final hosted walls pass; v4.10's general competitiveness goals are not declared achieved.

Hosted correction: the initial D macOS guard failed because its absolute temporary path used the `/var` alias while project policy resolves the physical `/private/var` root. The fixture now canonicalizes its root. Native and symlinked-temp guard runs pass; no runtime policy or expectations changed. The failed job log is retained. All ten workflows are required on the corrected head.
