# NIFT NO-OP OPTIMIZATION EXPERIMENT 2

Disposition: **RETAINED / CERTIFIED** at production SHA `64d68993a981fdb6aaf6a52e37c666b035073b3e`. The candidate was accepted and pushed normally under explicit authorization; native certification passed in [run 38068903985](https://github.com/nift-dev/nift/actions/runs/38068903985). Original pre-adoption recommendation: **RETAIN**. It removes 1,471,187 allocation events and improves the paired 10k no-op median **3.57% (26.999 ms)**. This is a modest result, not another 25% win. The production change is eight added/six removed lines in two files, with no new cache or representation class. Larger website workloads show small improvements; the small website no-op is essentially unchanged. **Next candidate: NONE YET. STOP FOR REVIEW.**

## Retained Experiment 1 and hosted certification

Retained production SHA: `9ce43cac3bddf6e5076d02da5525d424dd22a04f`. Normal push authorized and completed. Evidence-only landing: `c23128dd58869af69c6d05ff97e98643b651eb9a`, also pushed normally. No tag, version or release changes. [Exact-SHA checkpoint run 38063376419](https://github.com/nift-dev/nift/actions/runs/38063376419): Linux, native macOS, native Windows and normalized comparison **PASS** (18 portable cases plus platform-specific contracts). macOS focused status cases **30 PASS**; native Windows Python **24 PASS / 6 POSIX permission skips**. Windows symlink/escape/broken-link fixtures were executed, not skipped. Native C++ helper verified `FileBasicInfo` timestamp conversion, directory support and failing handle observations. Six skipped POSIX permission fixtures do not certify Windows ACL behavior.

| Hosted wall at production SHA | Run | Current conclusion |
|---|---|---|
| Process hardening contracts | [38063376443](https://github.com/nift-dev/nift/actions/runs/38063376443) | success (attempt 1) |
| Init targets | [38063376472](https://github.com/nift-dev/nift/actions/runs/38063376472) | success (attempt 1) |
| Gate 6B bytes value semantics | [38063376495](https://github.com/nift-dev/nift/actions/runs/38063376495) | success (attempt 1) |
| Performance regression guards | [38063376398](https://github.com/nift-dev/nift/actions/runs/38063376398) | success (attempt 1) |
| Hosted certification diagnostic | [38063376416](https://github.com/nift-dev/nift/actions/runs/38063376416) | success (attempt 1) |
| v4.4/v4.5 cross-platform | [38063376429](https://github.com/nift-dev/nift/actions/runs/38063376429) | success (attempt 1) |
| Gate 6A-R vendored libffi | [38063376439](https://github.com/nift-dev/nift/actions/runs/38063376439) | success (attempt 1) |
| packaging matrix (build-only, non-publishing) | [38063376385](https://github.com/nift-dev/nift/actions/runs/38063376385) | success (attempt 1) |
| Checkpoint 10 cross-platform equivalence | [38063376419](https://github.com/nift-dev/nift/actions/runs/38063376419) | success (attempt 1) |
| Test integrity guards | [38063376434](https://github.com/nift-dev/nift/actions/runs/38063376434) | success (attempt 2) |

The first test-integrity binding aggregate failed its existing typed-content **full-build** timing guard (250 pages 0.028 s, 1,000 pages 0.232 s, ratio 8.2) during `make -j2 test-all` alongside compilation. Full `--all` selection bypasses `build_reasons`; isolated retained/original controls passed three times each (ratios 2.6–3.4). The exact-SHA failed-job rerun passed, including the aggregate and serial build-boundary preservation proof, with no code or threshold changes. All ten hosted workflows are green. The first failure and successful retry are retained. Native status/checkpoint certification is separate from this aggregate timing guard.

## Fresh retained profile

The fresh 10k ordinary original/retained pair measured **934.578 → 709.516 ms** before the candidate. Later candidate measurements use their own fresh paired retained baseline, not 771.151 ms from the earlier experiment.

Callgrind on retained production: **6,686,776,528 user-mode instructions**. `build_reasons` inclusive **5,696,524,537**, `load_user_dependencies` **1,898,622,941**, `metadata_path_is_safe` **1,326,218,172**, `relative_of` **1,066,667,571**. Inclusive costs overlap and must not be added. Mutually exclusive symbol classification assigns **30.80%** of retained self instructions to path objects; rules are recorded in `parse-profile.py` and differ slightly from the original investigation's classifier. These percentages are not ordinary wall-time partitions. Kernel work is absent from Callgrind.

The retained operation counts still match the earlier path investigation: **580,548 normalizations, 190,199 relative operations, 4,534,217 path appends, 10,249,600 component splits, 210,197 resolved generic conversions, 12,243,057 operator-new calls**. Experiment 1 removed kernel metadata work without removing this userspace reconstruction. Path churn remains a demonstrated P0.

## Selected candidate and scope

Family selected: **duplicate lexical normalization in metadata safety checks**. Both existing private-helper callers in `build_reasons` already supply `(root / spelling).lexically_normal()`. Previously the helper normalized that path again and recomputed `root.lexically_normal()` for every dependency/requirement.

The candidate computes normalized root once in the consumer check, passes it by const reference, and passes the already-normalized dependency/requirement by const reference. Lifetime is one consumer dirty-check; values are immutable stack-local objects. There is no global mutable path identity, interning, mutex or retained status added. Existing root/path joins, `relative_of`, parent derivation/key construction and build-cache keys are unchanged. The private helper's parameter names/header comment make the normalized-input contract explicit; it still has exactly two callers.

No-follow leaf status, parent physical containment/cache authority, symlink physical containment, native following status, equal-mtime conservatism, per-consumer hash snapshots, exact consumed-byte authority, producer/hook boundaries and fingerprint value handling are unchanged. No persisted-state/schema change. Modified clean checks still do zero hashes.

Modified runtime files: `src/ProjectInfo.cpp`, `src/ProjectInfo.h`. Supporting changes: permanent `tests/dependency_path_identity.py`, its `Makefile` target, and this report/evidence. Experiment 2 committed: **NO**. Pushed: **NO**. Candidate binary SHA-256: `9d7e1438531c5f476c4603fb0a2d79d614b49d98ffbea3787edeb1842c7c92e5`. Baseline runtime binary: Experiment 1 SHA, SHA-256 `2ac04a913b293775406c95352047c8729d8de7a8e41b9eefa35d7f12ba3db046`.

The [concrete path lifecycle](data/path-lifecycle.md) traces leaf source, private header, module header, global header and .deps.json through saved checks, current sidecar validation, lexical/physical containment and conditional hash-cache identity. Original spelling, normalized relative spelling, normalized absolute path, canonical physical identity, symlink leaf and canonical build-cache key remain distinct. Allocations are measured, not inferred from source-level path constructors.

## Operation reductions at 10k

| Operation (resolved call arcs) | Retained E1 | E2 candidate | Removed |
|---|---:|---:|---:|
| `lexically_normal` | 580,548 | 410,411 | 170,137 |
| `lexically_relative` | 190,199 | 190,199 | 0 |
| `path::operator/=` | 4,534,217 | 3,583,456 | 950,761 |
| `parent_path` | 90,080 | 90,080 | 0 |
| `generic_string` | 210,197 | 210,197 | 0 |
| `_M_split_cmpts` | 10,249,600 | 8,498,245 | 1,751,355 |
| `operator new(` | 12,243,057 | 10,771,870 | 1,471,187 |
| `malloc` | 12,283,159 | 10,811,972 | 1,471,187 |
| `free` | 12,283,245 | 10,812,058 | 1,471,187 |
| `basic_string` | 35,311,084 | 31,858,314 | 3,452,770 |
| `memcpy` | 20,190,745 | 18,199,147 | 1,991,598 |
| `metadata_path_is_safe` | 90,076 | 90,076 | 0 |
| `relative_of` | 90,076 | 90,076 | 0 |
| `filesystem::path_within` | 16 | 16 | 0 |
| `filesystem::dependency_status` | 170,137 | 170,137 | 0 |

Exactly **170,137** normalizations removed: the helper no longer repeats **90,076** dependency normalizations; root normalization falls from **90,076** to **10,015** per-consumer observations. This also eliminates nested path appends/splits/allocations performed inside normalization. No additional operation family was manually optimized.

Candidate total instructions: **5,974,673,670**, down **712,102,858 (10.65%)**. Candidate `build_reasons` inclusive **4,984,013,086**; safety helper **576,056,753**. `relative_of` and user-dependency costs/call counts remain essentially unchanged. Resolved basic-string self instructions fall **1,117,837,357 → 1,030,467,181**; memcpy self instructions **277,921,840 → 250,740,064**. String-pattern totals include constructors/destructors and are not pure append counts. The parser exposes exact matched symbols.

New/malloc are nested allocation events and are not summed. Removed allocation events: **1,471,187**, or **12.02%** of operator-new calls; this is not retained heap bytes. Optimized/inlined path methods are not exhaustively represented by call arcs. Callgrind serializes/instruments execution and is not an ordinary timing measurement.

Fresh native syscall counts are identical: `newfstatat` **410,422**, `openat` **40,077**, `read` **20,038**, `lseek` **60,096**. Safety calls **90,076**, physical-containment calls **16**, following status observations **170,137**, hash requests **0** in both profiles. Experiment 1's status reduction is preserved.

## Ordinary timings and scaling

| TUs | Experiment 1 median [min–max], ms | Experiment 2 median [min–max], ms | Reduction | Child CPU E1 → E2, ms |
|---:|---:|---:|---:|---:|
| 100 | 23.043 [15.350–51.083] | 21.862 [14.145–54.480] | 5.13% | 38.443 → 37.142 |
| 1,000 | 92.672 [87.950–103.111] | 86.811 [80.509–109.501] | 6.32% | 296.104 → 279.824 |
| 5,000 | 408.697 [379.830–465.033] | 395.829 [370.282–435.804] | 3.15% | 1409.560 → 1369.137 |
| 10,000 | 756.549 [726.982–794.855] | 729.551 [698.604–768.715] | 3.57% | 2652.491 → 2544.967 |

Same accepted diagnostic fixtures, i7-12700H, `/tmp` tmpfs, CPUs 0–3, four jobs, two warmups and 20 measured samples per binary/size, alternating retained/candidate order. At 10k, the original binary was included before/after alternating pairs to provide an honest current cumulative comparison. All runs were zero-action, native output bytes stayed unchanged, application stdout passed, and no samples were removed. Raw samples/distributions/child CPU are in `data/paired.json`.

10k retained **756.549 ms**, candidate **729.551 ms**; improvement **26.999 ms / 3.57%**. Child CPU **2,652.491 → 2,544.967 ms**, down **4.05%**. Candidate wins **18/20 paired rounds**, with median within-pair difference **26.292 ms** (range -3.554 to 74.197 ms). This descriptive sample is not a universal guaranteed improvement.

OLS on four size medians: retained **74.306 µs/TU**, candidate **71.831 µs/TU**, down **3.33%**; intercepts **21.159 → 19.394 ms**. Host conditions shifted across series; the fresh retained/candidate pair carries Experiment 2 attribution.

Separate coarse stage-only probes, alternating five invocations each/two warmups, median of three: `open` **95.192 → 94.438 ms**, dirty scan **633.800 → 583.541 ms**, `build_many` **9.746 → 9.234 ms**. Instrumentation and fewer samples make these supporting diagnostics; they are not substituted for production medians or added to inclusive function budgets. No stage instrumentation is in the production candidate.

One attempted retained Callgrind invocation overlapped tracing and was refused by the ownership lock. It is excluded; its failure logs are preserved under `rejected-concurrent-*`. The accepted profile was run sequentially after tracing ended and completed a valid clean build.

## Website fixtures

| Pages | Workload | Samples per binary | E1 median, ms | E2 median, ms | Reduction | Expected rebuilt pages |
|---:|---|---:|---:|---:|---:|---:|
| 100 | noop | 20 | 4.440 | 4.390 | 1.14% | 0 |
| 5,000 | noop | 20 | 124.723 | 120.663 | 3.26% | 0 |
| 5,000 | one-page | 3 | 130.238 | 126.958 | 2.52% | 1 |
| 5,000 | shared-template | 3 | 375.326 | 351.094 | 6.46% | 5000 |
| 5,000 | shared-dependency | 3 | 356.697 | 350.881 | 1.63% | 5000 |

Reproducible authored sites: common HTML template, per-page HTML content, and a common injected header; four workers, modified mode, minification disabled. No-op sites use two warmups and 20 samples per binary. Changed workloads use three fresh disposable copies per binary; copies/mutations are outside timing. All **106 observations** passed exact output-byte and rebuilt-consumer-count oracles, with paired baseline/candidate output maps identical. Raw wall/child-CPU samples and fixture generator are preserved.

The small site no-op is essentially unchanged. Larger no-op/one-page/shared workloads show modest gains, particularly shared-template edits. This fixture set does not establish gains for every real website or workload. Compilation in the native build fixture is not used to claim website performance.

## Correctness and platform limits

- All Linux `make test` targets PASS: `make -j4 -o test-pagination-ordering test`, plus unchanged pagination ordering target with native ptrace. No skipped target is silently counted as executed.
- New path identity differential **15 cases in modified/hash/hybrid**, each against retained and candidate binaries: ordinary, dot, repeated separators, within-root parent alias, targeted edit/remaining consumers, and authored-sidecar parent rejection. Native Windows variant adds Windows separators when run there.
- Existing focused status **30 PASS**, including deletion/recreation, equality, older/newer, errors/unreadable content, symlink leaf, escape/broken link and project pre-hook mutation.
- Deterministic snapshot matrix **27 cases, zero contractual failures**; snapshot contracts PASS, including generated aliases, producer barriers, fingerprints, conflicting reads, hook observations, output validation and migration.
- Randomized differential **9 seed/mode combinations, 288 operations, 645 builds, 288 clean oracles PASS**. Relevant mutation guards **9/9 caught**. Static integrity scan **319 files / zero findings**.
- Disposable build-system correctness **45 records / 14 scenarios PASS**, including generated, targeted, rename and deletion. Ordinary/nested/output/metadata/pagination paths, symlinked parents, escaping links and missing paths also exercise existing filesystem/path boundary suites.
- NRS **94/94 PASS** using candidate CLI and freshly rebuilt candidate embedding libraries; PRS **12/12 PASS**.
- ASan+UBSan focused path/status suites **15 + 30 PASS**. Only modified `ProjectInfo.cpp` is instrumented and linked with normal remaining objects; this is focused coverage, not a fully sanitized core certification. LeakSanitizer disabled under the sandbox.
- Experiment 2 exact-SHA native certification: **PASS** Linux/macOS/Windows and normalized comparison in run 38068903985. Focused path identity: 15 Linux, 15 macOS, 18 Windows cases (Windows separator spelling included), across modified/hash/hybrid. Status contracts: Linux/macOS30 each, Windows24 plus six explicit POSIX permission skips. Native broader corpus covers aliases/symlinks, containment, generated and targeted dependencies, hooks and incremental behavior; 18 portable observations compare identically and platform-specific contracts pass. No unsupported Windows ACL/case/drive claim is inferred from Linux. Receipts: `data/hosted-e2/`.

## Cumulative diagnostic scoreboard

Historical accepted reference: original **1,024.193 ms / 730,666 metadata calls / 102.141 µs/TU**; Experiment 1 **771.151 ms / 410,422 / 76.880 µs/TU**. Those values retain Experiment 1 attribution (**24.71%**).

Fresh same-series 10k triplet: original **989.366 ms**, retained E1 **756.549 ms**, E2 candidate **729.551 ms**. Fresh E1 reduction **23.53%**, incremental E2 reduction **3.57%**, cumulative **259.816 ms / 26.26%**. Current metadata count **410,422**; current slope **71.831 µs/TU**.

Arithmetic from historical 1,024.193 to current 729.551 is 28.77%, but crosses series/host conditions and is not the causal cumulative claim. The fresh triplet provides the comparable cumulative evidence. No Ninja/Make official result was rerun or modified.

## Recommendation and review state

**RETAIN for review**, because the operation reduction is clear, the ~27 ms/3.57% 10k gain is consistent across most paired rounds, website controls improve modestly, Linux correctness remains intact, and the runtime patch is small. This is deliberately a modest recommendation. Do not stack another lexical family or introduce an abstraction/cache to enlarge it. The original review stop was followed by explicit acceptance and normal main-push authorization before hosted certification. Native certification now satisfies adoption; Experiment 2 is landed and certified. Experiment 1 remains independently landed and certified. **Next candidate: NONE YET. STOP FOR REVIEW.**

## Receipts and reproduction

`data/SHA256SUMS` binds every receipt, including full Callgrind profiles, annotations, parsed counters, raw timings, website generator, stage probes, correctness logs and source snapshots. Accepted fixtures remain in `/tmp/nift-noop-investigation`; baseline binary `/tmp/nift-path-experiment/nift-experiment1` comes from the recorded production SHA. Recreate those from accepted investigation evidence for another machine. No official Labs campaign or frozen evidence was modified.

Drivers: `paired.py` (ordinary series), `parse-profile.py` (documented symbol accounting), `websites.py`, stage/sanitizer build scripts. Native tracing: `taskset -c 0-3 strace -f -c ... nift build`; Callgrind: `taskset -c 0-3 valgrind --tool=callgrind --callgrind-out-file=... nift build`. Permanent test: `make test-dependency-path-identity`; optional `--baseline <retained-binary>` compares outputs with the retained revision. Scratch data is outside timing and production instrumentation remains absent.
