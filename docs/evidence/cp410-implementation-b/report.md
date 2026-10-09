# CP410-B: operation-local shared glob prefixes

Starting accepted head: `69ad5252946d8181cdd6ab17571aad8df93e1fda`. All ten callback hosted walls are successful at that exact head (CP410-A `hosted.json`). ABI remains 1.3.

KEEP the general shared-prefix walker and relative presentation. Matching uses the existing grammar and recursion, not a suffix or directory-shape specialization. Absolute paths are normalized, sorted, and deduplicated before relative conversion. Final RuntimeValues are created only for terminal results; consumed path owners are released during presentation. Literal bracket behavior remains unchanged. Symlink leaves and lexical ambiguities retain full-leaf resolution.

## Controlled local result

Six balanced triples per shape, one CPU affinity, no competing campaign compilation/profile during timing. Each process includes startup, matching, ordering, output and teardown. Comparators are the accepted callback binary and the earlier stable-fixture native prefix control; that control is not a general semantic oracle or a true whole-runtime floor.

| Shape | Accepted CPU seconds | Shared CPU seconds | Speedup | Native prefix CPU seconds | Accepted/shared RSS KiB |
|---|---:|---:|---:|---:|---:|
| Wide | 1.710402 | 0.936677 | 1.83x | 0.918874 | 205796 / 208590 |
| Deep | 2.636640 | 1.143996 | 2.30x | 1.101940 | 296466 / 298680 |
| Mixed | 1.525986 | 0.672701 | 2.27x | 0.702656 | 124158 / 132190 |

Wide whole-process instructions: **4,376,085,358 -> 3,308,787,651 (-24.4%)**. Allocations: **5,640,343 -> 4,440,369 (-21.3%)**; cumulative allocated bytes **953,069,934 -> 771,886,229 (-19.0%)**. Both Memcheck runs have zero errors and zero exit heap.

Wide readlink **1,200,016 -> 25**; newfstatat **240,012 -> 120,013**; getdents64 **173 -> 173**; getcwd **3 -> 3**. Kernel tracing timings are diagnostic, not the CPU timing result.

RSS increases about 1% on wide/deep and 6.5% on mixed. The shared descriptors trade some peak storage for much less cumulative allocation and repeated parent traversal. Remaining instruction costs are largely terminal path splitting/copying, allocation/free, and comparison/order work (raw self-phase profile retained). This change approaches the measured native prefix control, rather than claiming the entire glob subsystem is at a native floor.

## Exact contracts and ownership

Forty stable-filesystem/between-call path contracts cover literals, star/question, recursive/repeated recursive components, brackets, hidden/Unicode names, aliases, relative/absolute paths, lexical dot components, missing matches and external/dangling aliases. Portable existing path contracts remain in the cross-platform wall; the expanded alias/removed-cwd corpus is explicitly POSIX. The retarget test asserts the external command launched and succeeded, and observes the changed alias within the same Parser. The removed-cwd probe asserts removal actually happened and preserves the absolute-glob failure boundary.

The dedicated 32/128/512 scaling guard records terminal paths, prefix nodes/resolutions, full display paths, and final values. Ordinary sibling matches resolve one prefix; empty/absolute patterns create no shared prefix; leaf symlinks use individual full-path resolution; repeated recursive components keep absolute deduplication. The 120,000-match wide process records exactly 120,000 terminal paths, one prefix node, one prefix resolution, zero full display paths, and 120,000 final values. Counters exist only in dedicated test binaries.

The matching grammar, absolute normalization, absolute ordering/deduplication, and alias-output duplication remain unchanged. Brackets retain their current literal interpretation; this change introduces no bracket character-class feature.

Each relative `ls` glob owns its prefix descriptors. Matching completes before presentation. The first serialized ordinary terminal under a normalized directory resolves that directory prefix and stores its success or error for the rest of this call. Symlink leaves, lexical dot-component ambiguities, and empty terminal filenames use full-leaf canonicalization. Cached directory-entry type is observed during matching. Ancestor-only relative paths use the existing lexical-relative calculation rather than naive concatenation. The operation does not promise an atomic filesystem snapshot during concurrent mutation. Prefix resolution is a defined per-directory observation within this operation, rather than a fresh parent-chain observation for every ordinary leaf. No descriptor persists across calls; the same-parser retarget contract proves the next call observes the changed target.

Absolute patterns preserve their post-matching current-directory observation, including the removed-current-directory exception. Relative-base and prefix errors retain absolute presentation fallback. Final values are emitted only after absolute sorting/deduplication; consumed terminal path owners are released as they are serialized.


## Discarded private variants

The first per-terminal prefix lookup/duplicated suffix ownership version increased instructions to 4,684,718,138 and was dropped. Carrying the directory prefix reduced them to 4,038,438,253, but still constructed full relative paths per leaf. The retained version carries the prefix, directly builds ordinary display strings, consumes terminal owners, and interns prefixes lazily. Earlier timings with compilation overlap are diagnostic only; the final isolated timing file is authoritative.

## Certification

Local native/bindings, GCC/Clang and binding warnings, NRS (93 modules plus both embedding consumers), PRS (12 modules), lifetime ASan/UBSan/LSan, focused sanitized glob contracts and Memcheck pass. Deep sanitizer and parser/resource fuzz certification pass (1,219 cases; 232 builds, 987 controlled errors); core lifecycle memory checks pass (57 phases, four rounds). A long build process ended with SIGTERM without a reported test failure; its raw log is retained, and the remaining check sequence was completed separately. Default/migration/rewrite/redesign initialization, status, full/incremental builds have exact output-artifact hashes. The isolated full build and promoted binary are byte-identical to the measured candidate. Publication and hosted results will be recorded after commit.

No Jsonic++, dependency pin/synchronization, frozen benchmark, Labs or Linode changes.
