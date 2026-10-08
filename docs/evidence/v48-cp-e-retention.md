# v4.8 CP-E: FFI buffer ownership and retention decision report

Investigation only, 2026-10-08, Linux x86_64 / glibc 2.43. No runtime, public
header, C ABI, package or benchmark implementation changed. No lifetime fix is
proposed for automatic merge. CP-F and release work have not started.

**Decision: choose A for v4.8—preserve and document Parser-owned FFI buffers.**
The measured growth is reachable registry ownership, with substantial allocator
RSS retention after destruction. The probes found no unreachable leak. Automatic
scope reclamation would break demonstrated alias and native-pointer behavior.
Explicit release is a separate design decision requiring review.

## Accepted performance and hosted closeout

Accepted runtime: `ab687d07d4f987b09ac0277142e2aaf1c0602af6`. Preserve
`428a30325114f163ae240c2789e69bdc3e5fcb50` and
`89f40d990d4d560ce9bcb9d47f634202e92ffc69` with it.
Test-only follow-up: `f2f243c92e083bbab46099a07c84a6f6ec856f16`.
The follow-up makes the numeric parity subprocess result visible to the static
integrity scanner; it does not change the accepted runtime benchmark state.

- [Deep Guards](https://github.com/nift-dev/nift/actions/runs/37701338929): PASS at accepted runtime, all seven jobs, including sanitizers/lifecycle, watch endurance, incremental equivalence, isolation, GCC/Clang warnings and package contracts.
- [NRS](https://github.com/nift-dev/nift-regression-suite/actions/runs/37701343414): PASS, 93/93 at accepted runtime; suite `579793309a30188538e4cc618a61247f636a81cc`.
- PRS within Deep Guards: PASS, 12/12 at accepted runtime; suite `1da43659da269c96af21aea7234799d2ad56b2df`.
- Accepted runtime's ordinary performance, checkpoint 10, cross-platform, Gate 6A-R, Gate 6B, init, packaging build-only and diagnostics walls: PASS.
- Accepted runtime's original Test Integrity run failed its static scan; this was the test expression addressed by f2f243c, not a runtime failure. Do not label that original run green.
- [Final f2f243c Test Integrity](https://github.com/nift-dev/nift/actions/runs/37701366305): **PASS**, all five jobs at exact f2f243c, including serial non-destructive build-boundary proof, individual Go/C#/Node/Python gates, clean `make test-bindings`, clean parallel `make -j2 test-all`, static integrity and Linux/macOS aggregates. **PERFORMANCE CAMPAIGN = CLOSED; f2f243c fully certified.**

Only Test Integrity was triggered for the test-only change (workflow path filters); it is the only Actions run at f2f243c and is green. Other workflow results above are explicitly qualified to accepted runtime ab687d0.

Release artifacts were not run. No tags/version bumps/reviewed release evidence
were created. CP-E implementation was neither committed nor pushed.

## Ownership graph and lifecycle

```text
CLI run / render operation             embedded Engine::Impl
  owns local Parser                     owns unique ScriptState
                                           owns Parser
                                                |
                     ffi_buffers_: unordered_map<ID, shared_ptr<Instance>>
                                                |
                                         FfiBufferInstance
                                                |
                                    vector<unsigned char> payload

RuntimeValue string marker -------- ID lookup in that Parser only
  variables / arrays / objects / functions / captured values / result copies
  duplicate the marker; none holds a shared_ptr to the buffer

ffi_call(..., buffer) ---------- bytes.data() ---------- foreign C code
ffi_call(..., ptr return) ------ ffi_pointers_[ID]: raw void* (non-owning)
foreign retained address ------ no tracked edge back to buffer registry
```

`Parser.h:305–320` defines the independent library, pointer, buffer and callback
registries. Both the legacy expression path (`ParserExpression.cpp:1632`) and
prepared native path (`ParserTemplate.cpp:942`) allocate a shared instance,
copy input into its vector, increment a monotonic buffer ID and insert it into
`ffi_buffers_`. The returned value is `\x1fnift:ffi-buffer:<ID>` as an ordinary
runtime string with a reserved prefix. Empty buffers also have registry entries;
their payload pointer is null.

The map is the persistent strong owner. Scope exit, rebinding, overwritten
arrays, function return and result destruction do not erase entries. Alias
count does not determine payload lifetime. `copy`/`deepcopy` recurse through
containers but copy the FFI marker unchanged (`ParserExpression.cpp:2728–2737`),
so all copies address the same mutable storage. There is no buffer free/release
builtin. `ffi_close` unloads a library; it does not release buffers or pointers.

`ffi_bytes` returns a copied integer array. `ffi_snapshot_bytes` returns copied
immutable bytes. `ffi_buffer` copies string, immutable bytes or a validated
0–255 integer array. These bridges do not share payloads with their inputs or
outputs. Array construction grows vector capacity; string/bytes input copies a
contiguous range. `ffi_struct` fills a fixed native-layout vector in the same
registry. No current API resizes an existing buffer after exposing it.

`ffi_call` resolves buffer IDs and passes `bytes.data()` to libffi; native code
can mutate bytes or retain the address. Pointer return values are saved as
non-owning raw pointers in a separate monotonically growing registry. Neither
raw pointer handles nor foreign retained addresses encode buffer ownership or
bounds. Registry rehash moves map nodes, not the shared instance/vector payload.
Pointers remain stable through normal calls, aliases and new buffer allocations,
up to owning Parser destruction. Native out-of-bounds writes remain outside this
ownership guarantee.

`Parser::~Parser` finalizes execution workers, then normal member destruction
releases map nodes, shared instances and vectors. CLI/render Parser lifetimes
are local to the operation. `Engine::Impl::ScriptState` owns a persistent Parser
(`Engine.cpp:245`); `execute` and `evaluate` reuse it. `run_embedded_script` clears
variables, callables and module bindings, but deliberately does not clear the
FFI maps. The second execution probe confirms that a foreign retained buffer
address is still usable. Destroying the Engine's last implementation owner
ends this lifetime; a copied result does not extend it.

Failed import rollback is an exceptional erase path: it removes newly created
FFI registry entries and restores ID checkpoints (`ParserScript.cpp:490–550`).
If workers were launched, cleanup is deferred until worker completion. This
path is not normal scope reclamation and must be audited before any future
exported-pointer lifetime promise; it cannot account for the successful-loop
measurements. No new rollback semantics are claimed by this investigation.

Callbacks use a thread-local active Parser/tag for the supported synchronous
`i64(i64)` bridge. Nested activation is rejected, and an unrelated foreign
thread has no active owner. Callback-created buffers enter the same Parser map;
callback return does not release them. A native callback function pointer is a
trampoline, not a transferable independently owning closure. This investigation
adds no support for invoking it after the containing `ffi_call`.

## Public semantics and embedding exposure

| Question | Current behavior / evidence |
| --- | --- |
| Assignment, array/object storage, function pass/return | Copies marker identity; does not copy payload or release registry ownership. |
| Closure/callback capture | Marker resolves against owning Parser. Closure test sees native mutation; callback allocation stress retains in that Parser. |
| `deepcopy` | Preserves opaque FFI identity, including nested arrays/objects; native mutation is visible through copies. |
| Native raw pointer stability | Demonstrated after apparent handle dropping and across a later execution; valid only while storage owner remains alive. |
| Handle after Parser destruction | Result text may survive; buffer allocation does not. It is not a transferable lifetime token. |
| Host retains across executions | Native C can retain a pointer within the same Engine lifetime. Host result may retain marker text. Ordinary Engine setters/host callable seams reject reserved handles. |
| Explicit release | No buffer API; library `ffi_close` is unrelated. |
| Context destruction | Public `nift::Context` / `nift_context` is a render binding overlay, not the persistent script Parser. Freeing it does not release Engine script buffers. Local render Parser destruction does. |
| C ABI | `nift_engine_execute/evaluate`, result JSON and engine destruction reach the same implementation. A result accessor serializes the reserved marker as text. Result disposal releases result values, not the buffer map. Setter injection of the marker returns invalid argument. |
| Go | Execute/Evaluate call C ABI on persistent Engine; Close ends native Engine ownership. No separate buffer registry or release API. |
| C# | ExecuteJson/ExecuteResult use C ABI; native Engine disposal owns shutdown. ScriptResult disposal does not own FFI storage. |
| Node | Engine execute/evaluate routes through native Engine/C ABI; explicit close/finalization ends native owner. JavaScript arrays/strings do not own buffer payloads. |
| Python | Engine execute/evaluate routes through C ABI in `src/nift_module.cc`; close/deallocation ends native owner. Python result/string lifetime does not extend FFI storage. |

Binding exposure was traced in source, not certified by a new per-language
memory soak. Their hosted binding suites belong to the separate f2f243c wall.
Public ABI remains **1.3**. This report recommends no ABI/header change.

## Reproduction method and limits

Reproduction sources and raw evidence are in [v48-cp-e](v48-cp-e/).
The C ABI probe links the repository's current `libnift_c.a`, whose timestamp is
after the accepted runtime edits; make's dependency dry-run schedules only the
vendored libffi toolchain signature check, no core rebuild. HEAD is f2f243c and
its runtime sources are identical to ab687d0. The existing ASan/UBSan binary
contains that accepted runtime. GCC optimized local builds, glibc allocator.
This is CP-E investigative evidence, not a new performance comparison or
release certification.

Reported count is the loop iteration count. The probe also creates one initial buffer before the loop; allocating modes therefore create N+1 buffers, while the reuse control creates only one.

Each matrix subprocess creates one Engine, runs a loop, frees its result, then
runs `return null` to remove script roots. It samples `/proc/self/status` RSS
before, after loop/scope reset, after Engine destruction and after diagnostic
`malloc_trim(0)`. The trim is **probe-only**, never a runtime change. Most rows
use string input; additional rows use bytes and byte arrays. Wall time includes
execution/destruction/trim. Numbers are single samples with concurrent local
and hosted work, not timing medians. A 37-second local-function row is an outlier;
no speed conclusion depends on it.

Raw `peak` fields are `getrusage(RUSAGE_SELF).ru_maxrss`; small rows can inherit
the launcher process's earlier high-water mark, so their ~12 MiB floor is not
a buffer cost. Large historical rows exceed that floor. RSS at explicit phases
is the reliable comparison; isolated CLI `/usr/bin/time -v` additionally checks
actual native mutation reuse. Valgrind RSS is instrumentation overhead, not
comparable to native rows. Fresh processes reset allocator state; repeated
Engine creation within one process separately tests allocator reuse.

A: create/drop; B: retain all in array; C: allocate once and reuse (matrix control
plus native mutation reuse); D: function-local buffer returning null;
E: function returns a buffer, retain only every hundredth; F: replace containers
holding aliases and deepcopy; G: native pointer creation/retention after dropping
apparent handle; H: synchronous callback allocates; I: repeat same Engine versus
fresh Engines. Zero-size G passes null and does not dereference it. The native
fixture uses one static saved pointer; no use-after-owner-destruction is attempted.

## Measurements and classification

Representative native create/drop results:

| Buffers | Payload | Live after scope | RSS after Engine destruction | RSS after trim | Wall |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 200,000 | 0 B | 33.1 MiB | 30.4 MiB | 6.0 MiB | 458 ms |
| 200,000 | 1 B | 39.3 MiB | 36.6 MiB | 6.1 MiB | 447 ms |
| 200,000 | 23 B | 39.4 MiB | 36.7 MiB | 6.2 MiB | 593 ms |
| 200,000 | 100 B | 54.5 MiB | 51.9 MiB | 6.1 MiB | 1,078 ms |
| 200,000 | 1,024 B | 231.7 MiB | 229.0 MiB | 6.2 MiB | 1,446 ms |
| 1,000,000 | 100 B | 246.1 MiB | 235.1 MiB | 6.2 MiB | 4,560 ms |

The one-million 100-byte payload is 95.4 MiB itself. Remaining live RSS includes
registry nodes/buckets, shared-instance control blocks, allocation rounding,
runtime baseline and allocator pages. Zero-byte buffers still cost metadata;
1-byte and 23-byte sizes share much of their allocator size-class behavior.
100-byte versus 1,024-byte growth closely follows payload plus rounding.

Massif at 10,000 × 100 bytes finds a peak **2,197,850 B useful heap** plus
**438,686 B heap overhead**. Approximately 993,800 B is the prepared buffer
payload allocation stack, 397,520 B the shared-instance stack, 636,096 B map
nodes and 82,184 B bucket-related allocations, with ~88 KiB baseline/other.
The peak snapshot lands near, rather than exactly at, the final loop count.
This explains metadata separately from payload; it does not establish a
machine-independent exact per-buffer RSS constant.

Retaining every marker adds container/marker cost: 200,000 × 23 B reaches
77.5 MiB before clearing the array, then 48.5 MiB after the reset. The registry
still owns all payloads. Function-local, subset-return and alias-container rows
also retain all created buffers. Only allocating fewer buffers limits registry
growth; copying fewer handles does not reclaim payloads.

Five executions of 10,000 × 100 B on one Engine grow approximately
8.4 → 10.7 → 13.2 → 15.5 → 18.1 MiB. Five fresh Engines in the same process stay
~8.7 MiB; destruction plus trimming returns ~6.4 MiB. Thus repeated executions
share intentional ownership; fresh Engine destruction releases it even if RSS
pages are reused by the allocator.

Historical reproduction: **partial**. The ~40 MiB observation for 200,000 × 23 B
is reproduced closely. The reported ~1.1 GiB for one million × 100 B is **not**
reproduced by the direct string case (246 MiB). Bytes input reaches ~246.4 MiB; byte-array input reaches ~276.8 MiB (capacity rounding), still well below 1.1 GiB. For 200,000 × 23 B, bytes reaches ~39.6 MiB and arrays ~42.9 MiB. Additional input results are recorded alongside the full matrix. We do not have the original
historical program/allocator/build, and cannot assign its extra memory to buffers
alone. No claim that the original observation was false or fixed is justified.

The expanded 140-row matrix covers B–H at every count 1,000/10,000/100,000/200,000 and payload 0/1/23/100/1,024; all pass. At 200,000 × 100 B, callback allocations retain ~55.4 MiB; native pointer creation/retention retains ~74.2 MiB after scope reset (the additional pointer registry is also Parser-owned metadata). Reuse controls remain ~6.3 MiB. Full rows follow below.

Valgrind Memcheck on 1,000 dropped buffers: **14,327 allocations / 14,327 frees,
zero live blocks at exit, zero errors**. Callback allocation probe:
**194,558 allocations / 194,558 frees, zero live blocks and errors**. Cross-
execution native pointer/result probe: **426 allocations / 426 frees, zero live
blocks and errors**. ASan/UBSan with leak detection passes alias/deepcopy,
retained pointer and callback semantic probes; the 100,000-buffer stress also exits successfully with leak detection enabled and no sanitizer diagnostics. Native mutation of one buffer 200,000 times returns the expected byte 77 with a 6,772 KiB CLI peak RSS. Reduced probes cannot prove absence of every possible
runtime leak, but they directly contradict an unreachable-leak explanation for
these successful allocation loops.

Classification:

1. **Live retained memory:** map entries remain reachable from Parser regardless
   of dropped script references. Payload and metadata are intentionally retained
   under the existing parser-owned contract in `cp20-bytes-ffi.md`.
2. **Unreachable leak:** none demonstrated in tested creation/destruction paths.
   The observed successful-loop growth is not evidence of one.
3. **Allocator RSS retention:** material after actual destruction; Memcheck
   confirms frees while native RSS stays high, and trim releases most pages.
   Unchanged RSS alone is not proof of live buffers or a leak.

## Alternatives and v4.8 decision

| Candidate | Assessment / review requirement |
| --- | --- |
| A: preserve parser ownership, document and reuse | Recommended for v4.8. State scope/Engine lifetime, deepcopy identity, raw pointer limit and memory growth clearly. Reuse fixed-size buffers or bound Engine lifetime when safe for the host. Documentation-only, no ABI or runtime semantic impact. |
| B: reclaim when script references disappear | Unsafe as a local fix: marker strings do not own storage, deepcopy aliases, raw pointers/foreign libraries retain untracked addresses, persistent executions demonstrate use. Needs full ownership redesign and review; no implementation. |
| C: explicit release | Design only. Must define invalidation of every alias and pointer, foreign quiescence/callback constraints, double release, ID reuse/ABA, import rollback, and host disposal. A script builtin might avoid changing exported C symbols but would still change lifetime semantics; C ABI exposure would need separate compatibility decisions. Not a casual `free`. |
| D: minimal unreachable-leak fix | No demonstrated candidate on tested paths. If discovered elsewhere, report and review before applying, per requested stop point. |

Possible future implementation boundaries: a diagnostic allocation counter or
budget could make retention visible without changing ownership, but adds an API
or policy and requires review. Clearing maps on each embedded execution would
invalidate demonstrated retained pointers. Refcounts on marker copies cannot
track foreign addresses. GC of script roots cannot safely solve native retention
without an explicit foreign borrowing/owning protocol. Allocator trim in the
runtime changes process-wide behavior and is not a lifetime solution.

No core change is needed to close this investigation. The safe v4.8 response is
to make the current contract explicit and keep existing reuse/copy bridges.
**Stop for review here. CP-F is next only after that review; it has not started.**

## Full initial native matrix

| Count | Payload bytes | Mode | After scope KiB | After Engine destruction KiB | After trim KiB | Wall ms |
| ---: | ---: | --- | ---: | ---: | ---: | ---: |
| 1000 | 0 | drop | 6420 | 6420 | 6300 | 1.50 |
| 1000 | 1 | drop | 6424 | 6424 | 6272 | 2.76 |
| 1000 | 23 | drop | 6416 | 6416 | 6264 | 2.80 |
| 1000 | 100 | drop | 6492 | 6492 | 6276 | 2.76 |
| 1000 | 1024 | drop | 7408 | 7408 | 6316 | 2.52 |
| 10000 | 0 | drop | 7684 | 7684 | 6372 | 19.54 |
| 10000 | 1 | drop | 7996 | 7996 | 6372 | 20.69 |
| 10000 | 23 | drop | 7880 | 7880 | 6256 | 17.95 |
| 10000 | 100 | drop | 8708 | 8708 | 6316 | 24.85 |
| 10000 | 1024 | drop | 17848 | 17848 | 6412 | 32.08 |
| 100000 | 0 | drop | 20136 | 18784 | 6304 | 198.50 |
| 100000 | 1 | drop | 23196 | 21844 | 6232 | 177.33 |
| 100000 | 23 | drop | 23308 | 21956 | 6344 | 197.68 |
| 100000 | 100 | drop | 31036 | 29684 | 6284 | 210.66 |
| 100000 | 1024 | drop | 121608 | 120256 | 6236 | 326.58 |
| 200000 | 0 | drop | 33888 | 31144 | 6164 | 457.60 |
| 200000 | 1 | drop | 40240 | 37496 | 6260 | 446.99 |
| 200000 | 23 | drop | 40348 | 37604 | 6372 | 592.71 |
| 200000 | 100 | drop | 55844 | 53100 | 6264 | 1077.68 |
| 200000 | 1024 | drop | 237216 | 234472 | 6392 | 1445.68 |
| 1000000 | 100 | drop | 252008 | 240700 | 6364 | 4559.66 |
| 1000 | 23 | retain | 6536 | 6536 | 6344 | 7.13 |
| 1000 | 100 | retain | 6612 | 6612 | 6356 | 4.65 |
| 10000 | 23 | retain | 8444 | 8444 | 6364 | 51.63 |
| 10000 | 100 | retain | 8968 | 8968 | 6108 | 53.47 |
| 100000 | 23 | retain | 27820 | 27820 | 6196 | 570.56 |
| 100000 | 100 | retain | 35708 | 35708 | 6268 | 468.86 |
| 200000 | 23 | retain | 49700 | 49700 | 6372 | 597.86 |
| 200000 | 100 | retain | 65324 | 65324 | 6364 | 843.42 |
| 200000 | 23 | reuse | 6136 | 6136 | 6132 | 78.02 |
| 200000 | 100 | reuse | 6184 | 6184 | 6184 | 77.67 |
| 200000 | 1024 | reuse | 6188 | 6188 | 6188 | 80.22 |
| 10000 | 23 | local | 7972 | 7972 | 6244 | 260.47 |
| 10000 | 100 | local | 8928 | 8928 | 6452 | 300.15 |
| 200000 | 23 | local | 39996 | 37252 | 4536 | 37123.10 |
| 200000 | 100 | local | 57364 | 54620 | 6244 | 5796.90 |
| 10000 | 23 | subset | 7936 | 7936 | 6304 | 39.15 |
| 10000 | 100 | subset | 8692 | 8692 | 6284 | 50.80 |
| 200000 | 23 | subset | 40536 | 37792 | 6184 | 958.51 |
| 200000 | 100 | subset | 56224 | 53480 | 6248 | 797.39 |
| 10000 | 23 | alias | 7928 | 7928 | 6304 | 76.21 |
| 10000 | 100 | alias | 8712 | 8712 | 6320 | 72.46 |
| 200000 | 23 | alias | 40260 | 37516 | 6280 | 1762.65 |
| 200000 | 100 | alias | 55788 | 53044 | 6204 | 1841.79 |
| 10000 | 100 | drop | 18536 | 17868 | 6196 | 115.20 |

## Extended native matrix (all rows pass)

| Count | Payload bytes | Mode | After scope KiB | After Engine destruction KiB | After trim KiB | Wall ms |
| ---: | ---: | --- | ---: | ---: | ---: | ---: |
| 1000 | 0 | pointer | 6752 | 6732 | 6608 | 36.44 |
| 1000 | 1 | pointer | 6844 | 6824 | 6564 | 38.65 |
| 1000 | 23 | pointer | 6896 | 6876 | 6608 | 38.26 |
| 1000 | 100 | pointer | 6996 | 6976 | 6640 | 38.39 |
| 1000 | 1024 | pointer | 7800 | 7780 | 6580 | 41.11 |
| 10000 | 0 | pointer | 8008 | 7960 | 6460 | 358.09 |
| 10000 | 1 | pointer | 9184 | 8948 | 6628 | 407.22 |
| 10000 | 23 | pointer | 9168 | 8932 | 6608 | 373.88 |
| 10000 | 100 | pointer | 9904 | 9668 | 6572 | 380.36 |
| 10000 | 1024 | pointer | 18872 | 18636 | 6508 | 407.54 |
| 100000 | 0 | pointer | 21268 | 19112 | 6648 | 3697.17 |
| 100000 | 1 | pointer | 33332 | 31076 | 6524 | 3877.66 |
| 100000 | 23 | pointer | 33440 | 28480 | 6624 | 3921.84 |
| 100000 | 100 | pointer | 41224 | 36264 | 6608 | 3946.60 |
| 100000 | 1024 | pointer | 131760 | 126800 | 6544 | 4181.07 |
| 200000 | 0 | pointer | 35912 | 31584 | 6624 | 7811.57 |
| 200000 | 1 | pointer | 60396 | 55796 | 6584 | 7724.73 |
| 200000 | 23 | pointer | 60428 | 50340 | 6612 | 7987.95 |
| 200000 | 100 | pointer | 75940 | 65852 | 6508 | 8012.68 |
| 200000 | 1024 | pointer | 257308 | 247220 | 6656 | 8418.50 |
| 1000 | 0 | callback | 6772 | 6752 | 6636 | 46.16 |
| 1000 | 1 | callback | 6756 | 6736 | 6584 | 45.40 |
| 1000 | 23 | callback | 6740 | 6720 | 6592 | 45.96 |
| 1000 | 100 | callback | 6824 | 6804 | 6588 | 46.38 |
| 1000 | 1024 | callback | 7720 | 7700 | 6652 | 68.95 |
| 10000 | 0 | callback | 7900 | 7880 | 6552 | 417.71 |
| 10000 | 1 | callback | 8308 | 8288 | 6640 | 476.10 |
| 10000 | 23 | callback | 8300 | 8280 | 6680 | 667.46 |
| 10000 | 100 | callback | 9004 | 8984 | 6604 | 1066.94 |
| 10000 | 1024 | callback | 18168 | 18148 | 6764 | 1762.37 |
| 100000 | 0 | callback | 20724 | 19352 | 6608 | 9019.47 |
| 100000 | 1 | callback | 23844 | 22472 | 6596 | 9917.17 |
| 100000 | 23 | callback | 23860 | 22488 | 6644 | 10666.20 |
| 100000 | 100 | callback | 31616 | 30244 | 6616 | 10944.30 |
| 100000 | 1024 | callback | 122332 | 120960 | 6724 | 17158.20 |
| 200000 | 0 | callback | 34944 | 32180 | 6648 | 19907.50 |
| 200000 | 1 | callback | 41100 | 38336 | 6552 | 13420.30 |
| 200000 | 23 | callback | 41192 | 38428 | 6664 | 12123.00 |
| 200000 | 100 | callback | 56732 | 53968 | 6612 | 12823.30 |
| 200000 | 1024 | callback | 238028 | 235264 | 6668 | 17429.10 |
| 1000 | 0 | retain | 6556 | 6556 | 6408 | 3.18 |
| 1000 | 1 | retain | 6652 | 6652 | 6460 | 3.47 |
| 1000 | 23 | retain | 6584 | 6584 | 6392 | 3.34 |
| 1000 | 100 | retain | 6644 | 6644 | 6388 | 3.45 |
| 1000 | 1024 | retain | 7604 | 7604 | 6500 | 4.42 |
| 10000 | 0 | retain | 8100 | 8100 | 6336 | 29.04 |
| 10000 | 1 | retain | 8488 | 8488 | 6408 | 28.41 |
| 10000 | 23 | retain | 8504 | 8504 | 6424 | 30.61 |
| 10000 | 100 | retain | 9312 | 9312 | 6452 | 25.75 |
| 10000 | 1024 | retain | 18288 | 18288 | 6412 | 33.45 |
| 100000 | 0 | retain | 24916 | 24916 | 6420 | 289.98 |
| 100000 | 1 | retain | 28032 | 28032 | 6408 | 430.43 |
| 100000 | 23 | retain | 28040 | 28040 | 6416 | 340.74 |
| 100000 | 100 | retain | 35884 | 35884 | 6444 | 354.67 |
| 100000 | 1024 | retain | 126588 | 126588 | 6560 | 441.30 |
| 200000 | 0 | retain | 43460 | 43460 | 6388 | 644.60 |
| 200000 | 1 | retain | 49736 | 49736 | 6408 | 721.18 |
| 200000 | 23 | retain | 49704 | 49704 | 6376 | 695.69 |
| 200000 | 100 | retain | 65368 | 65368 | 6408 | 627.55 |
| 200000 | 1024 | retain | 246744 | 246744 | 6580 | 874.29 |
| 1000 | 0 | reuse | 6436 | 6436 | 6432 | 1.03 |
| 1000 | 1 | reuse | 6408 | 6408 | 6404 | 1.01 |
| 1000 | 23 | reuse | 6400 | 6400 | 6396 | 0.98 |
| 1000 | 100 | reuse | 6324 | 6324 | 6324 | 0.66 |
| 1000 | 1024 | reuse | 6320 | 6320 | 6320 | 0.91 |
| 10000 | 0 | reuse | 6372 | 6372 | 6368 | 5.25 |
| 10000 | 1 | reuse | 6368 | 6368 | 6364 | 5.21 |
| 10000 | 23 | reuse | 6408 | 6408 | 6404 | 5.25 |
| 10000 | 100 | reuse | 6372 | 6372 | 6372 | 5.39 |
| 10000 | 1024 | reuse | 6376 | 6376 | 6376 | 3.83 |
| 100000 | 0 | reuse | 6368 | 6368 | 6364 | 43.17 |
| 100000 | 1 | reuse | 6364 | 6364 | 6360 | 47.87 |
| 100000 | 23 | reuse | 6448 | 6448 | 6444 | 42.86 |
| 100000 | 100 | reuse | 6304 | 6304 | 6304 | 51.83 |
| 100000 | 1024 | reuse | 6372 | 6372 | 6372 | 47.32 |
| 200000 | 0 | reuse | 6392 | 6392 | 6388 | 92.04 |
| 200000 | 1 | reuse | 6256 | 6256 | 6252 | 88.14 |
| 200000 | 23 | reuse | 6328 | 6328 | 6324 | 92.99 |
| 200000 | 100 | reuse | 6408 | 6408 | 6408 | 114.16 |
| 200000 | 1024 | reuse | 6428 | 6428 | 6428 | 107.31 |
| 1000 | 0 | local | 6612 | 6612 | 6508 | 53.75 |
| 1000 | 1 | local | 6692 | 6692 | 6548 | 63.72 |
| 1000 | 23 | local | 6676 | 6676 | 6528 | 40.43 |
| 1000 | 100 | local | 6692 | 6692 | 6476 | 29.64 |
| 1000 | 1024 | local | 7548 | 7548 | 6464 | 36.16 |
| 10000 | 0 | local | 7960 | 7960 | 6564 | 297.35 |
| 10000 | 1 | local | 8288 | 8288 | 6564 | 313.95 |
| 10000 | 23 | local | 8156 | 8156 | 6428 | 307.50 |
| 10000 | 100 | local | 9072 | 9072 | 6596 | 299.85 |
| 10000 | 1024 | local | 18056 | 18056 | 6528 | 362.52 |
| 100000 | 0 | local | 20344 | 18992 | 5780 | 2868.18 |
| 100000 | 1 | local | 23856 | 22504 | 6132 | 3386.51 |
| 100000 | 23 | local | 23984 | 22632 | 6260 | 2978.23 |
| 100000 | 100 | local | 31888 | 30536 | 6380 | 2902.87 |
| 100000 | 1024 | local | 122412 | 121060 | 6296 | 2976.70 |
| 200000 | 0 | local | 35636 | 32892 | 6400 | 6147.67 |
| 200000 | 1 | local | 41784 | 39040 | 6260 | 5744.25 |
| 200000 | 23 | local | 41788 | 39044 | 6264 | 6082.60 |
| 200000 | 100 | local | 57488 | 54744 | 6368 | 5134.92 |
| 200000 | 1024 | local | 238720 | 235976 | 6372 | 5257.92 |
| 1000 | 0 | subset | 6364 | 6364 | 6264 | 3.98 |
| 1000 | 1 | subset | 6388 | 6388 | 6256 | 3.99 |
| 1000 | 23 | subset | 6344 | 6344 | 6204 | 2.49 |
| 1000 | 100 | subset | 6476 | 6476 | 6268 | 4.27 |
| 1000 | 1024 | subset | 7424 | 7424 | 6360 | 3.68 |
| 10000 | 0 | subset | 7584 | 7584 | 6280 | 19.14 |
| 10000 | 1 | subset | 7848 | 7848 | 6224 | 18.85 |
| 10000 | 23 | subset | 7880 | 7880 | 6248 | 19.22 |
| 10000 | 100 | subset | 8668 | 8668 | 6260 | 19.44 |
| 10000 | 1024 | subset | 17720 | 17720 | 6300 | 34.47 |
| 100000 | 0 | subset | 20276 | 18924 | 6284 | 230.35 |
| 100000 | 1 | subset | 23424 | 22072 | 6296 | 237.65 |
| 100000 | 23 | subset | 23432 | 22080 | 6292 | 224.21 |
| 100000 | 100 | subset | 31156 | 29804 | 6204 | 257.15 |
| 100000 | 1024 | subset | 121772 | 120420 | 6248 | 327.72 |
| 200000 | 0 | subset | 34352 | 31608 | 6272 | 466.34 |
| 200000 | 1 | subset | 40608 | 37864 | 6268 | 514.65 |
| 200000 | 23 | subset | 40612 | 37868 | 6260 | 548.73 |
| 200000 | 100 | subset | 56272 | 53528 | 6296 | 598.79 |
| 200000 | 1024 | subset | 237508 | 234764 | 6340 | 747.42 |
| 1000 | 0 | alias | 6344 | 6344 | 6236 | 5.86 |
| 1000 | 1 | alias | 6388 | 6388 | 6236 | 8.65 |
| 1000 | 23 | alias | 6372 | 6372 | 6220 | 9.91 |
| 1000 | 100 | alias | 6468 | 6468 | 6248 | 5.78 |
| 1000 | 1024 | alias | 7316 | 7316 | 6236 | 6.90 |
| 10000 | 0 | alias | 7588 | 7588 | 6284 | 61.91 |
| 10000 | 1 | alias | 7852 | 7852 | 6228 | 68.18 |
| 10000 | 23 | alias | 7820 | 7820 | 6196 | 66.98 |
| 10000 | 100 | alias | 8656 | 8656 | 6264 | 61.06 |
| 10000 | 1024 | alias | 17692 | 17692 | 6260 | 73.34 |
| 100000 | 0 | alias | 20112 | 18760 | 6284 | 659.96 |
| 100000 | 1 | alias | 23196 | 21844 | 6228 | 716.55 |
| 100000 | 23 | alias | 23200 | 21848 | 6232 | 728.67 |
| 100000 | 100 | alias | 31016 | 29664 | 6264 | 698.08 |
| 100000 | 1024 | alias | 121628 | 120276 | 6264 | 782.11 |
| 200000 | 0 | alias | 33952 | 31208 | 6232 | 1456.57 |
| 200000 | 1 | alias | 40216 | 37472 | 6236 | 1390.67 |
| 200000 | 23 | alias | 40188 | 37444 | 6208 | 1394.81 |
| 200000 | 100 | alias | 55840 | 53096 | 6256 | 1505.34 |
| 200000 | 1024 | alias | 237108 | 234364 | 6288 | 1674.21 |
