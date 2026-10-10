# NIFT NO-OP PERFORMANCE INVESTIGATION

Date: 2026-10-11, Australia/Melbourne. **Implementation authorization: NONE. STOP FOR REVIEW.**

The demonstrated bottleneck is repeated dependency file checks and lexical path/string reconstruction. At 10k TUs Nift made **730,666 `newfstatat` calls versus Ninja's 30,041**. The global header alone received **80,000 versus 1**. Nift dirty-checked every clean tracked item exactly once; this is not repeated recursive clean-action traversal. Physical containment checks were already cached: only 16 calls. Modified mode performed zero content hashes.

Local accepted-fixture no-op medians were Nift **603.085 ms**, Make **334.141 ms**, Ninja **99.183 ms**. These are diagnostic workstation results, not improvements over the frozen cloud results: no optimization occurred. A separate low-overhead probe placed about 462.6 ms in the dirty scan and 92.5 ms in project opening. Callgrind attributes 85.20% of user-mode instructions to `build_reasons`; filesystem path object methods alone account for 32.46% of total instructions as classified self cost.

Recommend one bounded future experiment: obtain one checked, following dependency status observation and use it for existence and mtime inside **one dependency check**, while retaining the separate no-follow containment/symlink check. Do not introduce an invocation-wide cache first. There are up to 320,244 redundant metadata requests in this narrow pattern, 43.83% of the observed count. That is an operation-count opportunity, not a promised wall-time saving.

## Repository and release discipline

- Core repository: `/home/nick/Repositories/nift/nift`, branch `main`.
- HEAD before/after: `924910bb8588811a06148585840dbeeea645c178`.
- Local `origin/main` ref: `c8d9c266518d115e4f289c5c6a2b94ddd7ee9968`. No fetch or push during this investigation.
- Runtime version: Nift v4.11.0. Binary SHA-256: `c0357ab201589cba256741e1964c39ede78c06b50a3f7e158c1d4a944d8bdfdd`.
- v4.11.0 commit: `c8d9c266518d115e4f289c5c6a2b94ddd7ee9968`; annotated tag object: `aca0cc8bfe5b0b906b300d6f92ad1ef9bb23b9e2`.
- HEAD's post-release evidence differs from the release base only in documentation/evidence, not runtime implementation. No maintained source, Jsonic++, historical evidence, tag, release, version, or Git history changed here.
- Tracked files and index were clean before and after. The 75 existing individual untracked files, including earlier release certification receipts, are preserved; status is recorded in `data/core-status-before-report.txt`. This report and its receipts are new untracked files.
- Experiment and frozen lab-evidence working trees remain clean. Their content was only read. All profiling source changes and fixture mutations took place under `/tmp/nift-noop-investigation`.
- The user's no-push restriction remains active. No commit or push was attempted.

## Frozen experiment reconstruction

The authoritative experiment is `/home/nick/Repositories/nift/nift-experiments/build-systems`. Its current handover HEAD is `40b79e4870f0cd8db6088044dd612e8856de65e2`; the **accepted frozen suite** is `24671011425ef8bd0b9f05adb1c87682e9ca33b7`. Evidence is at `/home/nick/Repositories/nift/nift-experiments/lab-evidence/benchmarks/build-systems/20261010-v4110-dev`, published in evidence commit `75c1588149a295702ec4006e25efc7ba4f05394f`.

| Identity | Accepted official experiment |
| --- | --- |
| Nift source | `635996b7ff5c725c81ac8ef57b1d146f7ceda642`, tree `dc590bbb28b745d9005807af0d42f8e134860209`, rebuilt v4.11 development snapshot |
| Source patch provenance | SHA-256 `138c9d228f4d7682ef763c07f4b03190a9608460a730af75c5bce932c4ea88bf`; runtime difference from then-public ancestor was the CLI version label |
| Nift binary | `e0a2b2b953e389f69e8ea4fec128773344c6217bf4d23110bcb5632d6f6c08dc` |
| GNU Make | 4.4.1; binary `f8885cb7885e983b10c44c39350cb3cdeb1205398d290fb706d491dde28927f0` |
| Ninja | 1.13.2; binary `684d6e09759a4758ceb2b6e76dce5b07e6c2253537888c10ce38` |
| Compiler / linker / ar | G++ 13.3.0-6ubuntu2~24.04.1, GNU ld/ar 2.42 |
| Host | Fresh shared 4-vCPU/8-GB Linode; Ubuntu 24.04.4; AMD EPYC 7642 virtual CPU, KVM; Linux 6.8.0-134; CPU affinity 0–3 |
| Filesystem | ext4 on `/dev/sda` |
| Native command flags | `g++ -std=c++17 -O2 -g0 -fno-ident -MMD -MP -Iinputs/include -Ibuild/gen`; explicit `-MF`; deterministic `ar rcsD`; response files |
| No-op and full command | Make `make -j4`; Ninja `ninja -j4`; Nift `nift build` with config build threads 4. Full case uses the same command with empty outputs. |

The 10k graph has **10,015 native actions/output artifacts**: 2 generators, 10,000 leaf object compiles, main and generated object compiles, 10 module archives, and 1 link. It has **20,015 completion prerequisite edges**, 60,031 declared file-input occurrences, and 30,039 distinct declared input spellings. Compiler depfiles are auxiliary outputs, not separate native actions. Each leaf consumes its own source/private header, one of ten module headers, the global header, and generated config header. Private fanout is 1, module fanout 1,000, global/config fanout 10,000. Leaf objects feed module archives, which feed the executable. Nift uses required JSON recipe content, custom `.f` build hooks, explicit `depends`, `.deps.json` file sidecars, modified incremental mode, and no minification. All three invoke identical native action commands via an action-tracing wrapper.

**Official warm no-op means a fresh process over a disposable copy of a fully built and validated baseline, with no input mutations and all outputs present.** Timing includes startup, manifest/config/tracked loading, persistent-state loading, invalidation checking, planning, and shutdown. It is not an in-memory daemon measurement. Caches are OS-warm, not a guaranteed cold or idle machine. Every no-op action trace is empty and output hashes/application stdout are checked after the timed command.

The C supervisor measures `CLOCK_MONOTONIC` around fork/exec through `wait4`. Fixture copying, input mutations, and post-run correctness oracles are outside timing. Participant/scenario order rotates, all participants share the same affinity, and child CPU time and maximum-child RSS are recorded. RSS is the largest child's value, not the sum of concurrent workers. All warmups and long tails remain; no outliers are removed.

The series has **17 graphs, 291 cells, 9,348 observations including warmups**, with the canonical independent audit passing action sets, hashes, dependency include closure, rotation and percentile checks. At 10k/light/j4: no-op 100 samples + 2 warmups; leaf 20 + 2; full/global 3 + 1 each. At 100/light/j4: full/global 10 + 1, no-op 100 + 2, leaf/private/medium 20 + 2. At 1k/light/j4: full/global/medium 5 + 1, no-op 100 + 2, leaf/private 20 + 2. At 5k: no-op 100 + 2, leaf 20 + 2, full 3 + 1. The complete plan and every raw sample remain in the frozen `identity.json` and `observations.jsonl`.

| Light/j4 TUs | Make no-op ms | Ninja no-op ms | Nift no-op ms |
| --- | ---: | ---: | ---: |
| 100 | 5.517 | 4.942 | 21.267 |
| 1,000 | 43.560 | 20.724 | 144.650 |
| 5,000 | 269.004 | 91.679 | 648.167 |
| 10,000 | 669.667 | 179.837 | 1,278.913 |

At 10k, full-build medians were Make 63.976579 s, Ninja 66.028577 s, Nift 69.612512 s; leaf medians 2,235.871 / 1,659.979 / 2,959.917 ms respectively. The target is no-op bookkeeping; full builds and scripting performance were not optimized or rerun as an official campaign.

The **accepted** separate 5k syscall diagnostic has Nift 365,666 `newfstatat`, 20,077 opens, 10,038 reads; Ninja 15,041 / 8 / 207; Make 20,050 / 5,008 / 10,457. Its rebuilt fixture passed zero-action/output-hash validation. Earlier local/provisional reports using 20,056 Ninja stats belong to a rejected encoding and must not replace this accepted receipt. Raw accepted traces are in `diagnostics/accepted-noop5000/`. There was no accepted function-level CPU profile; this investigation supplies it.

## Diagnostic baseline and reproducibility

Core SHA and binary identity are above. The fixture generator is an archived copy of accepted suite `2467101`. The local machine is Intel i7-12700H (20 logical CPUs); measurements inherit affinity **0–3**, with four configured workers. Linux 7.0.0-29-generic, x86-64; `/tmp` is **tmpfs**. G++ 15.2.0-16ubuntu1, GNU ld/ar 2.46, Make 4.4.1. Local Ninja 1.13.2 is `/tmp/build-systems-tools/ninja/usr/bin/ninja`, binary SHA-256 `91e9548850cda2799facfdaa7abe33f9e978832f85e98212255708a7bbe437f2`. Production Nift uses the repository's C++17/O2/pthread release build. Native compiler flags and light topology match the accepted generator. Full local machine/tool receipts are in `data/identity.json`.

Prepared local native baselines were copied from prior certified work directories at 100/1k/5k/10k. Larger old Make/Ninja manifests initially still used rejected extra recipe dependencies and per-action Ninja rules. That was detected before conclusions were drawn. **Only disposable copies** were re-rendered using the accepted frozen generator's shared rules/no extra Make/Ninja recipe inputs. Expanded action commands remained unchanged; no actions ran, executable stdout matched, and independent compiler include closure passed at all 12 size/system cells. Initial results are preserved as superseded `baseline-pre-encoding-correction.json` and provisional traces in `pre-encoding-traces/`. They are excluded from conclusions.

For each accepted local no-op cell, 12 fresh invocations reused a persistent disposable prepared copy: 2 designated warmups, 10 measured samples. Python monotonic timing surrounds process launch/wait; child resource CPU deltas are captured; trace/stdout/include audits are outside timing. This repeats a prepared local directory rather than making a fresh copy for each observation, unlike the official harness. It is a bounded diagnostic series, with no claim of cloud comparability or official confidence intervals.

| TUs | System | Median ms | Min–max ms | Median child CPU ms |
| --- | --- | --- | --- | --- |
| 100 | make | 1.904 | 1.737–2.574 | 1.762 |
| 100 | ninja | 1.688 | 1.518–2.007 | 1.558 |
| 100 | nift | 9.114 | 8.294–9.760 | 23.248 |
| 1000 | make | 19.225 | 18.319–22.518 | 18.802 |
| 1000 | ninja | 9.093 | 8.588–10.059 | 8.721 |
| 1000 | nift | 61.077 | 59.482–62.689 | 200.522 |
| 5000 | make | 128.344 | 124.167–135.248 | 127.916 |
| 5000 | ninja | 46.317 | 44.328–58.757 | 45.848 |
| 5000 | nift | 298.025 | 291.579–333.343 | 1012.772 |
| 10000 | make | 334.141 | 315.645–372.399 | 333.510 |
| 10000 | ninja | 99.183 | 97.984–109.391 | 98.751 |
| 10000 | nift | 603.085 | 593.131–648.849 | 2052.219 |


The raw distributions, warmups and CPU samples are in `data/baseline.json`; no samples were deleted. Nift's near-linear slope is approximately 60 microseconds/TU at 1k–10k on this host. Its local CPU/wall ratio is about 3.4 at 10k, consistent with four-worker checking; it is not evidence of extra native actions.

Changed cases use fresh disposable baseline copies, frozen `certify.mutate` and action-closure oracles, the same four-worker command, and post-timing executable stdout/compiler include closure checks. Each leaf/private/medium case has 3 measured observations at each size; globals have 3 at 100/1k and 1 at 5k/10k. They have no designated warmups and are descriptive controls, not official statistics. All **44 observations** pass. Global large runs are explicitly single observations. The initial 10k leaf/private/medium pilot overlapped compilation of a temporary probe and was replaced by separate rechecks; the pilot is preserved, not pooled. One scratch copy failed from tmpfs inode exhaustion before a timed build; completed disposable copies were removed and the copy retried. No failed copy became a sample.

| TUs | Leaf ms / actions | Private ms / actions | Medium ms / actions | Global ms / actions |
| --- | --- | --- | --- | --- |
| 100 | 92.635 / 3 | 111.103 / 3 | 154.273 / 12 | 852.062 / 111 |
| 1000 | 340.186 / 3 | 291.059 / 3 | 939.958 / 102 | 6271.860 / 1011 |
| 5000 | 1021.883 / 3 | 951.545 / 3 | 3923.792 / 502 | 31327.912 / 5011 |
| 10000 | 1687.335 / 3 | 1687.795 / 3 | 7093.116 / 1002 | 60298.069 / 10011 |


No optimization exists to compare against these controls. Future retention must repeat the same cases without profiling contention and with sufficient samples to assess a regression. Eight remaining prepared fixtures also passed a final zero-action check with SHA-256 hashes unchanged for every native output; the four removed Make/Ninja copies had already passed baseline action/stdout/include checks. See `data/final-controls.json`.

## Nift call/path map

Source references are the unchanged core HEAD:

```text
src/nift.cpp -> run_cli (src/CLI.cpp:2155–2240)
  ProjectInfo::open (ProjectInfo.cpp:82)
    find_project_root -> load_config -> load_tracking -> WatchList::load
    project_read::load_tracking (ProjectRead.cpp:229)
      stream/decode tracked records; validate names/paths/hooks/depends
      uniqueness sorts; sidecar directory discovery; cycle validation
      TrackedInfo vector + tracked-name index + output index
  ProjectInfo::build_all (ProjectInfo.cpp:1295)
    recovery ownership / .unfinished -> reconcile_watch -> reset caches
    project pre-hooks (before affected-set calculation)
    four-worker initial scan -> build_reasons once per tracked item
      output and info paths/existence; load and validate .info.json
      settings/hook signature; page-info mtime
      each saved dependency: normalize -> containment/symlink check
        caller existence -> dependency_changed existence -> mtime
      load current .deps.json: validate declared paths and existence
        relative/normalize/set; check these dependencies again
    build_many (ProjectInfo.cpp:1093)
      include clean validation-only jobs; depends closure/name lookups
      integer adjacency/pending counts -> ready queue -> clean completion
      recheck only if a prerequisite actually executed
    project post-hooks -> finish ownership epoch -> summary
```

This DAG fixture reports validated tracked jobs even with zero native actions. It does not take the independent-item empty-job shortcut. Source inspection and counters agree: on a clean no-op there is **no second `build_reasons` pass through the clean DAG**.

`metadata_path_is_safe` (527) recomputes lexical root/path/relative/parent spellings, looks up a cached parent containment result under a mutex, and queries leaf `symlink_status`. Only parent-cache misses or symlink leaves perform physical containment. `relative_of` (ProjectRead.cpp:63) normalizes both root/path and computes a lexical relative spelling. These operations do not physically resolve symlinks, but they create path/string objects repeatedly.

`load_user_dependencies` (ProjectInfo.cpp:420) must read current sidecar metadata even when its stored dependency list exists, so newly added/invalid sidecars cannot disappear behind stale state. However, clean dependencies already checked from `.info.json` are compared again: the existing deduplication skips dependencies with a *dirty reason*, not those already checked clean.

`JsonFile.cpp:4` checks existence/readability then reads/parses. `file_readable` (FileSystem.cpp:507) checks regular-file status and opens a stream; `read_file_checked` (30) checks regular-file status again, opens another stream, seeks for size and reads. This explains two opens per JSON file and the large seek count.

## Filesystem counts and repeat work

All counts below use **`strace -f -c`**, across every worker; tracer times are excluded from performance claims.

| TUs | System | newfstatat | openat | read | readlink | getdents64 | lseek | clone3 | execve |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 100 | nift | 7966 | 477 | 238 | 125 | 8 | 696 | 8 | 1 |
| 100 | ninja | 341 | 8 | 15 | 0 | 0 | 1 | 0 | 1 |
| 1000 | nift | 73666 | 4077 | 2038 | 125 | 10 | 6096 | 8 | 1 |
| 1000 | ninja | 3041 | 8 | 50 | 0 | 0 | 1 | 0 | 1 |
| 5000 | nift | 365666 | 20077 | 10038 | 125 | 18 | 30096 | 8 | 1 |
| 5000 | ninja | 15041 | 8 | 207 | 0 | 0 | 1 | 0 | 1 |
| 10000 | nift | 730666 | 40077 | 20038 | 125 | 29 | 60096 | 8 | 1 |
| 10000 | ninja | 30041 | 8 | 403 | 0 | 0 | 1 | 0 | 1 |


At 10k Make additionally made 40,050 `newfstatat`, 10,036 opens and 20,902 reads. Nift/Ninja each made one `access` query; Nift used no `clone` and eight `clone3` thread creations (four scan plus four DAG workers), with just one `execve`, the CLI itself. No compiler, archiver, link or shell subprocess ran. Nift also made 12 separate `fstat` calls, Ninja 10; no `statx` was reported.

Nift's exact metadata count at all four sizes is **73 × TUs + 666**; accepted Ninja's is **3 × TUs + 41**. At 10k, Nift averages **72.957 newfstatat / tracked action**, **4.002 opens / action**, **2.001 reads / action**, or 12.172 metadata queries per declared native file-input occurrence. The last denominator is a fixture normalization, not a count of Nift's richer stored dependencies. Nift's stat count is 24.32 times Ninja's.

A separate `strace -c` without `-f` at 1k saw just 11 `newfstatat`, 17 opens and 8 reads: it misses dependency-check worker syscalls. Its main-thread-only result is preserved to demonstrate why it is not the authoritative count.

The targeted full 10k trace records **730,666 named metadata calls over 70,122 distinct path spellings**. This is lexical spelling count, not physical-file identity. Ninja records **30,041 calls over 30,041 spellings**, each once in this fixture, including its startup/manifest paths. Shared headers reuse the Node status; repeated calls into the scanner do not imply repeated filesystem observations.

| Path | Nift stat calls | Ninja stat calls |
| --- | ---: | ---: |
| Global header | 80,000 | 1 |
| Generated config header | 80,017 | 1 |
| Each module header | 8,000 | 1 |
| A leaf source/private header | 8 each | 1 each |
| An ordinary `.deps.json` sidecar | 11 | Absent from this native Ninja graph |

For a shared header in each Nift leaf consumer, the eight queries are: stored-dependency no-follow leaf check (1), existence in caller/inner helper plus mtime (3), current sidecar declaration existence validation (1), then existence in caller/inner helper plus mtime again (3). Consumer snapshots and sidecar validation are necessary contracts; eight independent status observations are not proven necessary.

Temporary counters at 10k:

| Counter | Calls | Unique spellings/items |
| --- | ---: | ---: |
| Dirty-check visits | 10,015 | 10,015 (zero repeated clean-item visits) |
| Completion graph | 10,015 nodes / 20,015 edges | all clean nodes completed once |
| `dependency_changed` comparisons | 160,122 | 60,084 dependency spellings |
| `path_exists` requests | 430,356 | 70,103 |
| `modified_time` requests | 170,137 | 70,099 |
| Metadata leaf-safety checks | 90,076 | 60,084 |
| Parent containment calls | 16 | 16 candidates |
| `relative_of` calls | 90,076 | 50,070 |
| JSON loads | 20,031 | 20,031 |
| Checked file reads | 20,032 | 20,032 |
| Current hash lookups / actual path hashes | 0 / 0 | 0 |

430,356 existence + 170,137 mtime + 90,076 no-follow safety + 20,031 readability regular-file + 20,032 checked-read regular-file queries account for 730,632 metadata requests; only 34 remain for other startup/containment/library work. This is not a missing 600k-node graph: the same paths are queried under several roles and consumers.

Parent safety has 90,076 cache lookups, only 16 physical checks, and approximately 90,060 hits. Graph closure/name lookup and scheduler name-to-index resolution each perform 20,015 prerequisite lookups by source loop count. Nift already has a tracked-name hash index; it is not linearly scanning all tracked names for every prerequisite. Compiler file-input edges are separate from these completion edges.

Count probes use mutexes/sets to record identities, so their elapsed times are **intrusive and not baseline timings**. CSV `inclusive_ms` totals overlap and aggregate worker time; do not interpret them as wall contributions. Full small-size counters and probe source/build scripts are retained in `data/`.

## CPU, path operations and allocations

`perf record -e cpu-clock -g` was attempted, but the host rejects perf events (`perf_event_paranoid=4`, no required capability). No host security setting was changed. There is consequently no perf wall/CPU-time attribution or kernel profile. The fallback is **Valgrind Callgrind 3.26.0**, profiling the unmodified production binary at 10k: **6,692,241,557 user-mode instructions**. It serializes/instruments execution and does not measure ordinary timing or kernel syscall cost.

| Function | Inclusive instructions, % total | Self instructions, % total where resolved |
| --- | --- | --- |
| `ProjectInfo::build_reasons` | 5,701,581,853; 85.20% | 20,553,031; 0.31% |
| filesystem `path::operator/=` | 3,144,109,094; 46.98% | 490,903,316; 7.34% |
| `path::lexically_normal` | 2,429,416,915; 36.30% | 102,024,608; 1.52% |
| `load_user_dependencies` | 1,898,346,748; 28.37% | nested path/JSON work dominates |
| `metadata_path_is_safe` | 1,325,582,833; 19.81% | nested path work dominates |
| `project_read::relative_of` | 1,066,402,382; 15.93% | nested path work dominates |
| `ProjectInfo::open` / `load_tracking` | 940,404,147 / 940,300,374; 14.05% each | overlapping startup frames |
| `load_json_file` | 614,529,168; 9.18% | includes checks/read/decode |
| `path::_M_split_cmpts` | 687,502,927; 10.27% | 393,839,600; 5.89% |
| `operator new` | 641,645,100; 9.59% | 195,888,912; 2.93% |
| `malloc` / `free` | nested allocation costs | 365,038,016 / 404,056,760; 5.45% / 6.04% |
| string append / memcpy | nested costs | 277,454,156 / 277,919,952; 4.15% each |
| JSON `parse_string` | nested decoder costs | 143,375,066; 2.14% |

Inclusive rows overlap and must not be added. Recursive sorting frames can also have overlapping inclusive costs; they do not independently establish a sorting wall-time bottleneck. Some shared-library functions lack resolved names. Attribution is bounded by those limitations.

Classifying resolved **self** instructions with mutually exclusive symbol rules gives path-object operations 32.46%, string/copy/comparison work 22.32%, allocation/free work excluding path destructors 16.68%, JSON parser/document methods 4.98%, unresolved/other 22.08%, and small resolved filesystem-wrapper, associative-container and thread/lock categories. The annotation covers 99.01% of total instructions. Inlining and unresolved symbols hide some map, sort and filesystem work in other categories, so tiny resolved categories are not proof those mechanisms cost zero. See `profile-categories.json`; this classification is descriptive, not a causal wall-time partition.

Callgrind call-arc counts identify **580,548 lexical normalizations**, **190,199 lexical-relative operations**, **4,534,217 path append operations** (including nested appends inside path algorithms), **10,249,600 component splits**, and **210,197 resolved generic-string conversions**. Optimized/inlined conversions are not exhaustively counted. These counts explain the cost of reconstructing paths in dependency loops, `metadata_path_is_safe`, `relative_of`, info-path derivation and tracked validation; they are not 580k physical canonicalizations.

There are **12,243,057 `operator new` calls**, **12,283,159 public `malloc` calls**, and about 12,283,245 public `free` call arcs in this profile. New/malloc are nested and must not be summed. These are allocation events across the process, not concurrently live objects, allocated-byte totals or per-node retained heap sizes. They include transient path components, strings, JSON containers and destruction. No content-hash loop ran in modified mode.

On this compiler/ABI, `sizeof(TrackedInfo)=408`, `ItemBuildPipeline=72`, `std::string=32`, filesystem path=40; current-source Ninja `Node=112`, `Edge=160`, pointer=8. Nift's completion adjacency payload uses 8-byte indices per edge, but initially retains prerequisite names as strings; Ninja retains Node pointers. These base sizes omit dynamic capacities, strings, containers and allocator overhead. A Nift tracked action is not equivalent to a Ninja file Node, so a direct bytes/node winner cannot be inferred. Actual total heap bytes per edge were not measured.

## Startup and persisted-state cost

A second temporary probe changes only coarse stage timing in `ProjectInfo.cpp`, leaving other production objects uninstrumented. Five invocations per size, two warmups then three samples; emitted timers surround project opening, tracking, the whole dirty scan, and `build_many`. Existing `NIFT_TEST_BUILD_DAG_STATS` supplies plan construction. Timer scopes nest: tracking is included in open; plan construction is included in build_many; do not add nested medians as independent budgets. This probe has instrumentation overhead and is separate from the production baseline.

| TUs | Tracking ms | Project open ms | Dirty scan ms | build_many ms | Plan construction ms |
| --- | --- | --- | --- | --- | --- |
| 100 | 1.010 | 1.102 | 4.783 | 0.297 | 0.020 |
| 1000 | 9.147 | 9.281 | 45.600 | 1.253 | 0.183 |
| 5000 | 44.910 | 45.093 | 230.517 | 5.056 | 0.951 |
| 10000 | 92.359 | 92.536 | 462.550 | 10.261 | 2.094 |


The 10k scan accounts for roughly four fifths of the separate probe's elapsed work, opening roughly one sixth, and clean DAG handling roughly 10 ms. Exact ratios depend on the run; this is not an allocation of the official 1,279 ms. The original production Callgrind's open=14.05%, scan=85.20% instruction attribution independently agrees on ordering. Scheduler plan construction is about 2 ms, not the leading target. Ownership/startup bookkeeping is a small fixed remainder, but its recovery contract stays intact.

| Nift persisted file category at 10k | Files | Bytes |
| --- | ---: | ---: |
| tracked.json | 1 | 2,373,266 |
| config.json | 1 | 243 |
| .info.json | 10,015 | 8,070,235 |
| current user .deps.json | 10,015 | 2,121,273 |
| global derived `.hash` files | 0 | 0 |
| Total successful checked reads | 20,032 | 12,565,017 |

The 20,031 JSON loads are config plus item metadata plus sidecars; tracking is streamed separately into 10,015 per-record documents/TrackedInfo values. Metadata snapshots are decoded with `.info.json`; the modified fixture stores no meaningful content-hash snapshot work. No `.hash` loading, hash computation or metadata serialization is required on the no-op. Directory discovery makes only 29 `getdents64` calls at 10k; it is not scanning every directory separately for every item.

Every invocation rebuilds transient tracking, path, JSON and completion-graph state. Ninja also reparses its manifest and reconstructs State every invocation; its compiler dependency log uses deduplicated path IDs and arrays, rather than Nift's per-item JSON files. Ninja does not retain the entire graph between CLI runs.

Current measured Ninja 1.13.2 `-d stats` at 10k reports manifest parse **28.9 ms**, `.ninja_log` load **2.8 ms**, `.ninja_deps` load **6.3 ms**, and node-stat cumulative **36.1 ms / 30,041 calls**; State has 30,040 path entries. These are internal metrics from one diagnostic run, not a complete exclusive wall partition or a replacement for the 99.183-ms baseline.

## NINJA LESSONS: actual source and semantics

Current upstream source studied is pinned to **`c2817e5854b825b3e7d38ac7a266724ba1313e29`**, archived in scratch with its identity receipt. The measured executable is **1.13.2**, not a binary built from that current-master SHA. Current-master scanning code has newer changes; source-level techniques and measured binary counts are kept distinct.

Reviewed `src/graph.h/.cc`, `disk_interface.h/.cc`, `state.h/.cc`, `manifest_parser.h/.cc`, `build.cc`, `build_log.h/.cc`, `deps_log.h/.cc`, `util.cc`, `ninja.cc`, plus timestamp definitions. Stable primary source: [Ninja source at the inspected revision](https://github.com/ninja-build/ninja/tree/c2817e5854b825b3e7d38ac7a266724ba1313e29/src). The [official manual](https://ninja-build.org/manual.html) documents explicit/implicit/order-only edges, compiler depfiles, dependency logs, dynamic dependencies and restat. Its displayed manual version is 1.13.1. [Issue 2779](https://github.com/ninja-build/ninja/issues/2779) discusses a binary manifest cache; discussion is not evidence that the measured binary has one.

A source/header/output spelling is lexically normalized by the manifest parser, looked up by `State::GetNode`, and represented by one retained Node for that key. Edges refer directly to Node pointers. Compiler-log paths also go through GetNode. The first status observation goes through `StatIfNecessary` to Node::Stat/RealDiskInterface::Stat; subsequent consumers use known status. No-op shared nodes can be *referenced* many times, but their leaf-state resolution is done once and producing edges terminate early at VisitDone. Parser canonicalization may still occur for each manifest occurrence before deduplication: **Ninja does not canonicalize every unique path exactly once across all startup stages**.

On POSIX the disk interface generally performs direct stat; its optional directory-wide cache is Windows-specific. Ninja's low POSIX count follows fewer requests and per-Node reuse. It is not a universal directory-stat cache. Repeated resolution can occur after reset, dyndep changes, or command completion, so one-stat-per-process is not a universal rule for changed builds.

| Concern | Ninja | Nift | Equivalent / stronger contract / avoidable cost |
| --- | --- | --- | --- |
| Graph loading | Manifest parse, GetNode dedup; logs loaded; transient State | Config/tracked JSON validation, indexes, item JSON/sidecars, transient completion graph | Different responsibility; both reconstruct state. Small-file reads/path reconstruction are opportunities. |
| Representation | Owned path Node; direct producer/consumer pointers; Edge vectors/counts | Rich TrackedInfo, prerequisite names; indexed integer completion adjacency; per-consumer JSON dependencies | Not equivalent objects. File identity could be retained more cheaply without changing snapshots. |
| Dirty criterion | Input/output mtimes, missing output/deps state, command hash; producer dirtiness | Settings/hook signatures/output presence; dependency vs consumer-info time; modified/hash/hybrid and per-consumer snapshots | Nift is stronger for hash/exact-observation and equal-time contracts; not universally stronger in every unrelated feature. |
| Input/output times | Newer input than output/log state; POSIX nanoseconds where available | Modified uses dependency **>=** consumer metadata time | Threshold and reference object differ. Do not substitute Ninja's comparison. |
| Depfiles | GCC/MSVC discovered dependencies; `deps=gcc` ingests/removes depfile into log | Native exact consumed buffers plus explicit current user sidecars; fixture sidecars encode known native file inputs | Discovery contracts differ. Fixture closure is independently audited. |
| Persistent dependency state | `.ninja_deps`: binary path IDs, output timestamp and input-ID lists | Per-item `.info.json` dependencies/hash snapshots plus current `.deps.json` validation | Cheaper representation possible later; cannot treat stale global state as consumer truth. |
| Build log | Text `.ninja_log`: command hash, output time, command durations, restat state | Per-item build metadata plus recovery state | Keep settings/scripts/snapshots/recovery coverage. No direct schema substitution. |
| Filesystem strategy | Per-Node known status; direct POSIX stat | Repeated existence/time/no-follow/regular-file checks; parent containment cache | Large avoidable duplication; separate following/no-follow roles must remain. |
| Generated dependencies | Producer links, order-only constraints, depfiles and dyndep update graph/readiness | Explicit completion DAG, producer failure blocking, output/source/hash cache invalidation and post-producer consumer rechecks | Some shared purpose, different contracts; stale cached observations can be incorrect. |
| Missing outputs | Marks output dirty; producer runs | Marks tracked artifact missing; selects producer, consumers follow completion semantics | Broadly similar trigger; requirement relationships have extra Nift policy. |
| Missing inputs | Explicit missing source is an error; dep-loader-created missing inputs can force rebuild for rediscovery | Removed/invalid dependency selects rebuild; required opaque input errors; failed producer blocks dependent | Do not copy missing-dep fallback across Nift validation contracts. |
| Path identity | Lexically canonical string key; not physical symlink identity | Lexical stored dependency names, physical containment checks; canonical cache keys for DAG observed bytes/hashes and aliases | Ninja's lexical identity alone is insufficient for Nift aliases/containment. |
| Normalization | Compact in-place lexical canonicalization, slash_bits for Windows display | Repeated std::filesystem normalization, joins, relative and generic-string operations | Retain normalized representations later; validate platform/alias behavior. |
| Symlinks / aliases | Ordinary stat follows symlink; lexical spellings can be different Nodes | No-follow leaf check plus physical containment; canonicalized producer cache identities | Nift has explicit safety/alias contracts. Do not remove checks just to match counts. |
| Timestamp resolution | Platform TimeStamp, POSIX sec/nsec | filesystem file_time_type plus equal-time conservative invalidation | Preserve precision/error handling and Nift equality threshold. |
| Hashing | Hashes commands/log keys, normally not ordinary input contents | Hash/hybrid checks current content against each consumer snapshot; exact native read buffer is authoritative | Not interchangeable. Modified no-op has zero content hashes to optimize. |
| Repeated clean nodes | Leaf status-known and producing-edge VisitDone | Once per tracked item on clean initial scan; once per completion job; file dependencies repeated by consumer/role | Memoizing clean tracked actions adds little here. File status/path repetition is the target. |
| Traversal / cycles | Dependency scan with stack/marks; requested/default-target reachability | Tracked validation/cycle pass; four-worker full scan; completion queue and targeted closure | Keep ordering/failure/targeted guarantees. |
| Allocation | Node/Edge/path allocations, pointer arrays, path-ID deps log | Rich tracking and JSON plus repeated transient path/string/set allocations | Base-size comparison is insufficient; measured churn supports path-object work first. |
| Restat | Post-command unchanged output mtime can prune pending downstream work | Rechecks clean validation jobs after executed prerequisites; unchanged dependencies can remain clean | Nift already skips actions correctly in the test below, but pays revalidation. |
| Startup | Manifest/log parsing remains measurable; no default persistent binary manifest in the measured run | Tracked parse/validation and per-item state reload | Measure before proposing a persistent format/cache. |

### Unchanged generated output test

A separate **100-TU diagnostic variant**, not a frozen benchmark change, changes the copied generator to preserve output bytes/mtime when content is unchanged. After establishing valid state, a whitespace-only spec edit executes a producer with unchanged observable outputs. The Ninja variant optionally enables `restat` on its shared action rule.

Nift executes **2 generators and zero downstream native actions**; Ninja with restat also executes 2; Ninja without restat executes 114 actions (all except independent main compile). Both generated output mtimes and application stdout are unchanged. Nift counter instrumentation sees **227 dirty-check visits across 115 items: 112 deliberate post-producer rechecks**, unlike the no-op's zero repeats. This demonstrates existing correctness-preserving pruning of native actions, but possible future revalidation savings. It does not authorize removing rechecks: aliases, hashes, generated bytes, and producer writes can change during a build. See `unchanged-generated.json` and its counter receipt.

### Techniques to transfer in spirit

| Technique | Applicable / expected benefit | Semantic risk / priority |
| --- | --- | --- |
| Per-node file status memoization | Yes within a check; later possibly within a stable scan epoch | Invocation-wide reuse needs producer/hook/write invalidation; start narrow. |
| Canonical node identity | Retained logical identity could remove path recreation and lookup churn | Lexical and physical identity/containment are different; P0 follow-up. |
| Pointer/ID graph traversal | Already used in completion adjacency; file dependencies could use retained IDs | No broad graph rewrite supported by this profile. |
| Visited/clean memoization | Already effective for clean tracked items | Do not remove changed-build rechecks; low no-op upside. |
| Compact dependency representation | Possibly useful for transient parsed state | Preserve per-consumer snapshots/current sidecars; measure retained heap before redesign. |
| Persistent dependency log | May reduce small-file reload overhead | Recovery/versioning/validation complexity; P1 later, no new binary format now. |
| Persistent command/build state | Nift already persists richer item state | Keep settings, scripts, observations and ownership contracts. |
| Restat-like pruning | Existing Nift action pruning works; recheck-cost investigation possible later | Unchanged mtime is not unchanged hash/alias/project state; not first candidate. |
| Normalize/retain path once | Yes for lexical stable representations | Physical alias identity must still be validated at correct epochs. |
| Directory stat caching | Not the explanation for POSIX Ninja result; no first proposal | Windows/platform-specific behavior, stale directory/symlink status. |
| Binary manifest/state cache | Unproven as first fix | Invalidation/recovery format burden; measured duplicates offer a smaller first change. |
| Internal metrics | Useful future inexpensive opt-in counters | Existing DAG test stats help; propose later, no public CLI feature implemented. |

## Root-cause ranking

**P0 — repeated dependency file metadata queries.** Evidence: 730,666 calls, 73N+666 scaling, 80k stat calls on one global header, 160,122 comparisons, and the source-derived eight-query pattern. Cost is evident in operation counts, but untraced syscall wall contribution is not separately measured. First candidate is a single checked status observation per dependency check; a broader stable-epoch file identity/cache could follow. Risks: following versus no-follow semantics, precision, missing/error handling, aliases and changes after producers/hooks. Stronger validation does not justify querying the same following status three times within one comparison.

**P0 — lexical path/string/allocation churn.** Evidence: 32.46% resolved path-object self instructions, 46.98% inclusive path append / 36.30% lexical normalization, 580k normalizations, 4.53m path appends and 12.24m new calls. Causes: reconstructing root/relative/normalized paths in metadata safety, sidecar loading, info-path derivation and tracking validation. Possible bounded next step: retain lexical root/normalized dependency identity instead of reconstructing it. Physical containment already has 16 calls; "remove repeated physical canonicalization" is not the measured P0. Risks: platform separators, alias identity, root-relative validation, mutation epochs. The two P0s occupy different measured budgets; this profile cannot rank their exclusive wall-time savings against each other.

**P1 — persisted-state reload/decode and tracking reconstruction.** Evidence: project open ~92.5 ms in the separate 10k stage probe, 14.05% inclusive production instructions; 20,032 reads/12.565 MB, 40,077 opens; JSON loading 9.18% inclusive instructions. Readability preflight reopens each JSON file and can potentially be replaced with one checked read while preserving diagnostics/regular-file checks. Parsed transient representations could later be cheaper. Do not skip current sidecar validation or introduce a binary cache before a bounded shared read-path experiment. Sorting/uniqueness/cycle validation belong in this startup budget and must keep their correctness checks.

**P2 — clean completion graph/scheduler and diagnostic ergonomics.** Evidence: 10,015 nodes/20,015 edges; once-only clean-item visits; ~2 ms plan and ~10 ms whole build_many in a separate 10k probe. This is a small no-op budget relative to scan/open. No evidence supports a large DAG algorithm rewrite, clean-node memoization fix, or directory-scan optimization first. Thread/mutex costs are partly hidden by Callgrind serialization and inlining; trace futex percentages are not normal-run contention percentages. Cheap permanent opt-in stage/operation counters would help future work but are not a speed fix on their own.

The investigation **does not** establish “100 ms unavoidable stronger semantics plus 900 ms removable waste.” It establishes removable operation patterns and major instruction costs. Quantifying irreducible semantic time or predicting 250 ms requires a retained, correctly paired implementation experiment. Ninja's 180 ms is not a pass/fail target.

## Shared impact and first bounded proposal

| Workload | Shared path / expected applicability |
| --- | --- |
| Ordinary websites/content/templates | `build_reasons`, metadata load, path safety and dependency_changed are shared; many consumers of a template can repeat the same status/path work. |
| `@input` / native observed dependencies | Their saved dependencies are checked by the same engine; exact consumed buffers and per-consumer hashes remain authoritative at build time. |
| Generated outputs | Same scan plus completion/producer rechecks; status reuse must not span producer writes. |
| Tracked pipelines / build scripts | Same sidecars/metadata/depends; current script signatures and pre-hook order remain intact. |
| Targeted builds | Targeted entry selects a subset/prerequisite closure but uses the same dirty-check functions; no change to selection or completion guarantees. |

Expected shared applicability follows directly from source call paths; **ordinary website speedup is not measured** because no common-path optimization was authorized/retained. Phase-14 website timing and full retention certification remain future gates, not unfinished investigation implementation.

Recommended first experiment, if later authorized:

1. Introduce a small checked dependency-status result carrying following existence/error and native-precision mtime. Reuse it in the caller and `dependency_changed` during one comparison. Obtain this with one underlying platform metadata operation, not `status` plus `last_write_time` as two calls.
2. Preserve no-follow symlink/containment checks, the consumer's stored snapshot/reference timestamp, the modified **>=** rule, and conservative behavior on missing/unreadable/error states. Do not let a failed mtime query's sentinel make a dependency look clean.
3. Keep lifetime local to that dependency check. No cross-consumer, producer, hook, invocation, or persistent-cache reuse in this first experiment. Keep hash/hybrid and fingerprint branches explicit; no snapshot format or source-read changes.
4. This can remove two following probes from each ordinary modified dependency comparison: up to **320,244 of 730,666 requests** at 10k, or ~43.83%; residual would be about 410,422 if all eligible calls coalesce exactly. It does not remove duplicate sidecar comparisons, path churn, current sidecar reads, or necessary no-follow checks. Implementation may achieve fewer removals; verify counts rather than assume the bound.
5. Compare paired uninstrumented 100/1k/5k/10k no-op distributions, changed cases, stats, profile and slope. Do not promise a specific millisecond target; retain only with material speedup, unchanged correctness, acceptable changed-case cost and justified maintenance complexity.

Required future gates: deterministic incremental matrix; randomized differential smoke; relevant mutation guards; build-system correctness fixture; `make test`; NRS/PRS; relevant sanitizers. Explicitly cover per-consumer/exact-consumed-buffer snapshots, hash/hybrid/equal-mtime edits, deleted/missing files and outputs, sidecar additions/invalid JSON, unreadable inputs, symlink/alias containment, generated producers/failure blocking, project hooks, targeted closures, recovery/repair, ordinary websites, and Windows/macOS/Linux timestamp/path precision and error behavior. If scope grows into a cache, add aggressive same-epoch producer/ancestor/directory/alias invalidation mutations. Then measure reproducible small/large website no-ops and page/shared-template changes. The official frozen experiment stays frozen; a fresh Labs series comes only after a materially improved, fully certified revision is explicitly retained.

## Evidence and stop boundary

`data/` contains raw local timing distributions, changed-case receipts, state/syscall/counter tables, Callgrind profile/annotations/call counts, coarse stages, Ninja metrics, final controls, superseded pilots and reproduction scripts. Full per-syscall path traces and prepared fixtures remain in `/tmp/nift-noop-investigation`; compact path-frequency receipts are preserved here. Frozen raw official observations and all 291-cell audit material remain at their original evidence path; `official-comparison.json` is a read-only-derived subset for convenience.

No source optimization, public metric feature, version bump, official campaign, commit, push, or release change was made. Temporary probes are confined to scratch. **Implementation authorization: NONE. STOP FOR REVIEW.**
