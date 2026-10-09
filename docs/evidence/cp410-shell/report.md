# NIFT v4.10 — EXPANDED SHELL PERFORMANCE INVESTIGATION

**STOP FOR REVIEW.** Retain the local glob candidates for review: remove two redundant directory sorting passes, canonicalize the common relative base once, reserve the known output size, and avoid an unnecessary successful-relative path copy. Reject the native-dispatch predicate reorder after a reproducible median +2.8% identity-sort CPU result in longer pinned-core controls. Its string/save gains do not justify that tradeoff against the primary scripting target. Atomic save guarantees and public APIs are unchanged. The broad remaining save gap needs a focused wrapper/handle-lifecycle implementation checkpoint, not a weaker save contract. Sort/capture/frame remains the equally primary scripting track.

## Frozen oracle and measured identities

Frozen series: **20261009-v4100-shell-expanded**. Unchanged: **YES**. All 51 pinned retained files match [the original freeze](oracle-freeze.json) and [final verification](oracle-final-verification.json). No official rerun, benchmark/methodology/Labs edit, provisioning, removal or publication occurred.

The official measured Nift revision is `629b1f23afbb5b3be97dac66b6cea1a9d5b3d1fc`; shell benchmark measured revision is `fc7c4d93dcd525545f97e302501eb08e1b0f9f7e`. Official workload source files were verified byte-identical to the measured revision despite later publication-only repository commits. The external headline numbers remain 2.881s traversal, 15.499s small writes, 163ms metadata, 1.091s move, 1.333s copy, 127ms strings and 238ms concat. Local figures below describe this machine and current accepted core, not replacement official results or claimed cross-language rankings.

Accepted local baseline: core `8df185e2095037512d2d7315204cd5e3991a65be`, binary SHA256 `1b70a37a77cea5328a960de3145fbdef5f5b95d1a6f0e7fa85af4e69109962a8`. Candidate binary SHA256 `c6e19e3145aaf9bb979a006f5b46c6cc51e90b663f595b2e203928069debea53`. Candidate changes are **uncommitted and unpushed**, pending this review. ABI: **1.3**, unchanged.

## Traversal

Local official-equivalent fixtures preserve target/keeper names and bytes, not just the number of paths. The official 100k targets plus 20k keepers produce **120k matching .dat paths**, with 21-byte target names and 19-byte keeper names. Early 14-byte-name fixtures are independent shape controls, not exact official fixtures: the distinction affects small-string allocations. Every timed run checks the output count; permanent semantic guards check ordered path strings.

[Final isolated paired timing](final-timing.json) alternates baseline/candidate order, four repetitions per traversal case. Both production binaries run alone; instruction/allocation profiles are separate. [Summary](final-timing-summary.json):

| Targets / matches | Baseline wall | Candidate wall | CPU change | Peak RSS change |
| --- | ---: | ---: | ---: | ---: |
| 1k / 1.2k | 27.93ms | 21.80ms | −22.4% | −2.5% |
| 10k / 12k | 264.40ms | 192.65ms | −26.8% | −14.2% |
| 100k / 120k | 2.788s | 1.999s | −28.5% | −16.8% |

At 120k matches, peak RSS changes 248,194→206,426KiB. [Instruction/heap profiles](official-profiles.json) change 6,936,903,556→5,356,232,167 instructions (−22.8%), 8,520,421→7,440,409 allocations (−12.7%), and 1,540,333,764→1,334,489,193 cumulative requested bytes (−13.4%). All scale/label Memchecks have zero errors and zero exit heap.

The final broad controls also improve: all-match flat −26.5%, deep −16.8%, mixed −20.9% wall; sparse/no-match flat about −56%/−55%, deep −44%/−45%, mixed −29%/−30%. A separate short `/tmp` root with 120k matches improves wall22.6%, CPU22.8% and RSS13.9%; the gain is not confined to unusually long artifact paths. These independent fixtures differ from exact official filenames; retain both classes of evidence.

### Root causes and counts

`glob_walk` sorts every wildcard directory, sorts it again for the recursive `**` branch, then sorts/deduplicates all terminal absolute paths. For the flat official fixture this decorates 360k entries across three sorts. Directory ordering is redundant because the final absolute lexical ordering remains authoritative. Removing the two intermediate sorts leaves one final ordering/decorating pass. Directory vectors and recursive behavior are retained; **each directory is still enumerated twice** for this pattern.

[Structural baseline counts](structural-counts.json) cover 27 scale/shape/filter combinations. Flat 120k has 120,011 walk invocations, two directory scans, 240k directory entries processed, 120k component matches, 120k terminals/absolute lexical normalizations, three sort calls and 36 directory-vector capacity growths. `sort_entries` counts inputs submitted to sort, including size-one calls that return before decoration on deep chains; it must not be read as actual conversions for those cases. The permanent key guard measures actual decorated final keys and changes from 2N to N for its wildcard case. Final dedup remains required for overlapping recursive patterns and is preserved.

Relative presentation previously called `filesystem::relative(match, base)` for every match; it weakly canonicalizes both operands. Resolve the common base once, still resolve every match's symlinks, then use the same lexical-relative conversion and absolute fallback on canonicalization error. No canonicalization is added for absolute or empty glob results. Reserve the RuntimeValue result vector to the known match count and avoid a second full path copy on the successful relative path.

[Final syscall counts](final-syscalls.json), exact 120k:

| Call | Baseline | Candidate |
| --- | ---: | ---: |
| readlink | 2,160,008 | 1,200,016 |
| newfstatat | 360,012 | 240,013 |
| getdents64 | 346 | 346 |
| openat / close | 7 / 7 | 7 / 7 |
| access | 1 | 1 |

The counted readlink failures are libc canonicalization probes on ordinary path components, not missing output files. The unchanged enumeration count confirms this is not a single-pass traversal rewrite. Earlier 296-getdents measurements came from a different fixture/directory layout; do not mix their counts with these exact final paths.

Native C++ controls preserve hidden-name filtering, recursive symlink policy, existence filtering and **absolute pre-conversion sort/dedup**. The first control also repeats base canonicalization and is not an optimized lower bound. A stronger [base-cached native control](native-optimized.json) at120k uses 3,788,189,412 instructions, 4,920,053 allocations and 1,196,230,929 cumulative bytes, zero Memcheck errors/leaks. Candidate Nift remains about41% higher in instructions than that control. Remaining targets include duplicate enumeration, path/filename materialization, filesystem path allocation and result wrappers; none justifies skipping final ordering or symlink resolution.

### Semantics and guards

Final sorting/dedup occurs **before relative canonical conversion**. Symlink aliases can yield repeated displayed strings or apparently unsorted display paths. Both behaviors are preserved. Rejected prototype/control behavior included sorting/deduping displayed strings, which changed this contract.

Permanent guards cover exact ordered paths, recursive/repeated `**`, hidden names, explicit hidden directories, Unicode/spaces, escaped magic, absolute input, empty matches, symlink file/directory/dangling/external aliases on supported platforms. Windows tests use native MSYS paths and UTF-8 subprocess decoding. The platform-specific symlink cases follow the existing platform capability boundary rather than asserting Windows POSIX links. The relative-path guard proves one base resolution and N match resolutions for 32/128/512 results, and zero/zero for empty and absolute results. Test counters are behind `NIFT_TEST_GLOB_PATH_STATS`; production symbol inspection confirms they are absent.

These guards are in the normal Make test target and the focused Linux/macOS/Windows workflow. Existing native/root/readonly/overwrite/atomic/error and long-path tests remain part of the full certificate. New candidates have local Linux certification; their new exact-SHA macOS/Windows hosted certificate is still pending publication review. The green accepted-baseline certificate is not a certificate for the new filesystem source.

## Small files and save lifecycle

Exact API: `f := file(p); f.open("w"); f.write(payload); f.save(); f.close()`, 128-byte payload. [Exact final paired decomposition](final-save-timing.json) uses target/keeper fixtures, fresh targets per run and three repetitions at1/1k/10k/100k. [Summary](final-save-summary.json) preserves all results. Native direct and temp+rename controls are decomposition controls only; they are not replacements for save or full metadata/revert semantics.

At100k, final local baseline save is5.243s and candidate5.300s (+1.1%wall/CPU); direct control1.070s and temp+rename2.186s. The traversal fixes do not affect saves. No save improvement is claimed for the final glob-only candidate. The rejected dispatch experiment saved about2.2% wall in its separate exact fixture run. Save peak RSS remains about171MiB because file instances are retained, despite closing their buffers. The save gap remains substantial and unsolved.

The earlier full1/1k/10k/100k fsync decomposition is retained in [write-baseline.json](write-baseline.json): 100k direct1.646s, atomic3.106s, Nift5.571s and per-file fsync+rename384.616s. These are exploratory single runs; some earlier measurement sessions overlapped profiling and are **not authoritative acceptance timing**. They establish the qualitative cost of a stronger sync policy on this storage. The exact fresh-file paired controls above replace them for direct/atomic/current performance conclusions; no repeated six-minute fsync campaign was needed to validate an unchanged save implementation.

### Existing save contract

Dirty save writes one sibling unique temp, writes/flushes/closes an ofstream, snapshots/restores existing destination permission bits when available, then calls the existing atomic replacement helper. Linux uses rename and **does not fsync/fdatasync**. Windows uses `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)` with existing readonly handling/restoration on failure. Do not weaken either platform contract, conflate atomic replacement with durable directory synchronization, or replace save with the direct control.

Opening an existing file in `w` mode still loads old bytes for saved/revert state. Write grows the working buffer; dirty detection compares saved/working content (fresh saved-empty files short-circuit by size). Successful save copies working bytes into saved state. Close clears contents, may retain string capacity, and does not remove instances from the parser's registry. Destructor/registry retention contributes to memory, not an implicit extra write per closed handle.

[Exact1k save profiles](final-save-profile.json) use214,830,135→213,869,908 instructions,119,421 allocations in each binary, zero Memcheck errors/leaks. The final syscall log records one temp/rename per output, bounded stream operations and no fsync. Path inspection and getcwd are measurable overhead. Separate earlier sampling at1k fresh files reports1000rename,1006openat/close,1001write,3011newfstatat and1002getcwd; final exact logs are authoritative for their fixture. Stream buffers account for roughly one allocation per file and are only a small share of total allocation traffic.

Profile hotspots include repeated builtin rejection, method parsing/dispatch, memcmp/strlen/string comparison, RuntimeValue construction, path resolution and registry/handle lifecycle. The rejected generic dispatch experiment helped some string cases but did not solve file-handle retention, repeated cwd resolution or saved-buffer ownership. Recommend a separate atomic-save wrapper checkpoint, with exact current semantics and deterministic per-output syscall/ownership guards. A direct/batched writer API should only be designed if a reviewed real user requirement calls for distinct semantics.

## Secondary paths

[1k native controls](secondary-profiles.json) and [kernel counts](secondary-syscalls.json) separate wrapper costs from filesystem transfer costs; all ten baseline/native profile rows have zero Memcheck errors/exit heap. Current API shapes include path-list parsing and, for move/copy, `p.split("/").last()`; native controls intentionally decompose those wrappers, not promise exact language-level safety/error equivalence.

| Operation,1k | Nift instructions | Native instructions | Nift/native allocations | Finding |
| --- | ---: | ---: | ---: | --- |
| stat/metadata | 50,816,383 | 4,847,730 | 66,383 /7,016 | Two relevant stat calls per file plus aboutonecwd per logical call; `inspect_path` already fetches type/size, not every optional timestamp/permission. |
| move | 244,787,741 | 6,240,310 | 185,400 /11,016 | Both1000renames; Nift adds destination preflight and path/value/method work. |
| copy | 246,007,484 | 6,695,603 | 186,402 /11,016 | Both use1000sendfile calls and2006open/close; kernel copy path is already efficient here. |
| concat | 53,385,764 | 5,282,732 | 67,471 /3,018 | Each input is materialized into an intermediate string, then appended to the output working buffer; final atomic save copies saved state. Native control streams chunks with atomic final rename. |
| string | 70,795,269 | 3,236,116 | 26,301 /1,016 | Repeated source/method rejection and temporary strings/values dominate the tiny transform. |

Move currently reports `filesystem::rename` failures; it has **no cross-filesystem fallback** to preserve. A future API proposal must not invent one silently. Copy currently uses `filesystem::copy_file(overwrite_existing)` with existing error translation. Linux already selects efficient kernel transfer, so replacing it with a hand-written buffer is not supported by these measurements.

Concat Nift/native have the same input read/open/close counts for these controls. Streaming could remove intermediate strings and large buffer ownership, but needs review of ordering, errors and atomic final replacement. No streaming primitive or metadata/move/copy change was implemented.

The current uppercase operation is byte/cctype based, not Unicode case folding. String replace copies the source and repeatedly calls `string::replace`, shifting the remaining suffix for growing/shrinking replacements. The large, growing UTF-8 case exposes that structural cost; a linear scan/append builder is a justified next isolated experiment, not an implemented change in this tranche. The rejected generic dispatch experiment did not alter that contract. The rejected predicate experiment improved ASCII transform loops at5k and50k by CPU10.8%/12.7% and wall12.0%/11.6%, with essentially unchanged memory. Those gains are not retained or claimed for the final glob-only binary. Permanent dispatch cases cover Unicode, malformed/nested calls and exact diagnostics. [Additional final string profiles](string-profiles.json) cover ASCII/no replacements, many replacements, short UTF-8 and large UTF-8, with exact A/B output and Memcheck; they retain the existing byte transform contract. Do not claim a Unicode-decoder optimization or a replacement algorithm rewrite.

## Shared costs, rejected experiments and safety

[Unrelated controls](unrelated-summary.json) use ordinary production binaries and zero-error/zero-exit-heap Memchecks: 34 instruction/allocation rows across loops, calls, callbacks, sort, map/set, JSON and BFS retain the same allocation counts and near-identical instructions. Longer isolated pinned-core paired CPU medians at128k are identity sort+0.97%, JSON parse−0.17%, mutation−1.21%, serialization−0.55%, calls−0.60% and BFS−0.60%. Map/set initially measured+3.39% with samples ranging−10.2% to+8.5%; a targeted larger256k/32-pair confirmation measures+0.77% (bootstrap median sampling interval+0.12% to+1.32%, excluding systematic machine/build uncertainty). This is a small residual cost, not a material multi-percent regression. The short5k string timing is noisy (+11.8%wall), while50k is+0.7% and deterministic ASCII/UTF8 instructions remain near baseline; no final string gain is claimed. Keep these observations and the rejected dispatch data visible rather than selecting only favorable timings.

The predicate-reorder experiment is rejected: although its isolated string loops improved about12% and saves about2%, its longer identity-sort control showed a median+2.8% CPU cost. Sort priority makes that tradeoff unattractive. The final source restores the original predicate order. The fifteen exact dispatch semantic guards are retained for future changes, not as evidence of an accepted dispatch optimization.

Path/name string materialization, allocation, source/method rejection and wrapper RuntimeValues explain specific weak paths. They do not imply that startup, pipelines, arithmetic, selected deletion or the whole shell runtime is slow. No production path representation, JSON index, Linux-only save shortcut, handle reclamation or benchmark-specific .dat/N=100k path was introduced.

Discarded evidence: initial traversal timings that overlapped strace; native control that incorrectly sorted converted paths; intermediate prototype compilation/run failures (including a compact-line comment swallowing a loop), corrected before final measurements. Retain raw receipts rather than dropping bad results. Exploratory timing files are labeled and not substituted for final isolated pairs.

[Local certificate](safety/progress.log) passes focused guards, native/embed/bindings tests, GCC/Clang and binding warning walls, NRS93, PRS12, ASan/UBSan/LSan lifetime,58extra safety reproducers,86sanitized object oracles,31sanitized sort oracles,57core memory cases, deep sanitizers and1219parser-fuzz cases (232successful builds/987controlled errors). New15dispatch cases also pass separately. The first final native run hit the restricted sandbox’s ptrace denial in the strace-based pagination ordering test. An independent minimal strace probe reproduced the same denial; the failure is preserved in `sandbox-test-failure.json`. The complete certificate passed with tracing permitted; no test is skipped or flaky failure counted as a pass. Production ABI/layout remains1.3.

All ten hosted walls for the accepted baseline SHA are green ([receipt](hosted-baseline.json)); new filesystem source awaits its own cross-platform hosted run after review. Candidate results and the full certificate are local review evidence, not a release approval.

Sort/callable track: [architecture review](../cp410-capture-frame/report.md) complete with measured all-visible capture scaling, lookup/escape constraints, isolated prototypes and canonical Jsonic++ threshold design. No production architecture change was made.

Recommendation: accept the bounded glob changes, drop the rejected generic predicate experiment, then run the reviewed new exact-SHA hosted walls. The next filesystem implementation target is atomic-save wrapper/path/handle overhead, followed by streaming concat and targeted path-list wrappers. Keep sort ownership work primary on the scripting track. **STOP FOR REVIEW.**
