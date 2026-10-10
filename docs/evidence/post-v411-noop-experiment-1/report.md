# NIFT NO-OP OPTIMIZATION EXPERIMENT 1

Experiment 1: **RETAINED / CERTIFIED**. Production commit `9ce43cac3bddf6e5076d02da5525d424dd22a04f` was pushed normally to main under the subsequent explicit authorization. Native Linux, macOS and Windows checkpoint certification and normalized comparison passed in [run 38063376419](https://github.com/nift-dev/nift/actions/runs/38063376419). The original experiment findings below remain diagnostic evidence; the later brief authorizes the separate Experiment 2 investigation.

## Candidate and boundaries

Base SHA: `924910bb8588811a06148585840dbeeea645c178`. Committed: **YES**, production SHA `9ce43cac3bddf6e5076d02da5525d424dd22a04f`. Pushed: **YES**, normally to main under subsequent explicit authorization. Version remains 4.11.0. Baseline binary SHA-256: `c0357ab201589cba256741e1964c39ede78c06b50a3f7e158c1d4a944d8bdfdd`. Candidate binary SHA-256: `2ac04a913b293775406c95352047c8729d8de7a8e41b9eefa35d7f12ba3db046`.

Production changes: `src/FileSystem.cpp`, `src/FileSystem.h`, `src/ProjectInfo.cpp`, `src/ProjectInfo.h`. Supporting changes: `Makefile`, the signature anchor in `scripts/consumer_snapshot_mutations.py`, new `tests/dependency_status.cpp`, new `tests/dependency_status.py`, and this evidence directory. Historical evidence is preserved. Jsonic++, the release tag, frozen experiment repository, frozen Labs evidence and official benchmark series were not modified.

`DependencyStatus` carries existence, explicit error, and seconds plus nanoseconds. A dependency observation is local to one comparison. Saved dependency and current sidecar comparisons obtain separate observations; consumers, producers, hooks, phases, targeted jobs and invocations share none. The page-info reference is observed at the former reference-mtime location once within that consumer check. There is no status cache.

POSIX obtains the following status and native timestamp with **one `stat`**. Linux uses `st_mtim`; macOS uses `st_mtimespec`. Windows opens a following handle with `CreateFileW` (without `OPEN_REPARSE_POINT`, with directory support and read/write/delete sharing), obtains **one metadata query** through `GetFileInformationByHandleEx(FileBasicInfo)`, and closes the handle. This is not one Windows API call or a demonstrated one-syscall Windows implementation. Its 100 ns timestamp is converted exactly to Unix seconds plus nanoseconds. Both comparison operands use this representation; no C++ `file_clock` epoch conversion occurs. Failures preserve the native error and cannot become clean through a default time.

No-follow metadata safety and containment remain separate and unchanged. Modified and hybrid retain `dependency.mtime >= info.mtime`, including equality. Missing/error observations conservatively invalidate. Per-consumer snapshots, checked consumed-buffer authority, `current_hash_cached`, metadata schemas and explicit value-based fingerprint handling are unchanged. No path identity/allocation optimization is included.

## Metadata calls

Fresh paired `strace -f -c` observations, same fixtures and CPUs 0–3:

| TUs | Baseline newfstatat | Candidate newfstatat |
|---:|---:|---:|
| 100 | 7,966 | 4,522 |
| 1,000 | 73,666 | 41,422 |
| 5,000 | 365,666 | 205,422 |
| 10,000 | 730,666 | 410,422 |

Exact slopes: baseline `73 × N + 666`; candidate `41 × N + 422`. At 10k: **320,244 calls removed (43.83%)**, matching the investigation's theoretical bound. Other 10k I/O is identical: `openat` **40,077**, `read` **20,038**, `lseek` **60,096** for both binaries. Traced timings are not performance measurements.

Diagnostic counters corroborate attribution: dirty visits **10,015 / 10,015 unique**; dependency comparisons **160,122 / 60,084 unique**; no-follow safety **90,076**; physical containment **16**; JSON reads **20,031**; checked reads **20,032**, bytes **12,565,017**; hashes **0**. Candidate following-status observations: **170,137**; old mtime requests: **0**; existence requests fall **430,356 → 110,112**. Counters are temporary in a scratch copy and are not present in the production binary. Their inclusive timings overlap and are not performance evidence.

## No-op timings and scaling

| TUs | Baseline median [min–max], ms | Candidate median [min–max], ms | Reduction | Child CPU baseline → candidate, ms |
|---:|---:|---:|---:|---:|
| 100 | 12.311 [11.595–12.911] | 9.935 [9.133–10.419] | 19.30% | 36.237 → 27.148 |
| 1,000 | 102.741 [99.773–130.649] | 78.654 [76.032–85.762] | 23.44% | 358.875 → 265.457 |
| 5,000 | 503.936 [494.629–528.480] | 384.574 [371.487–413.533] | 23.69% | 1813.526 → 1342.574 |
| 10,000 | 1024.193 [998.718–1122.521] | 771.151 [756.273–843.815] | 24.71% | 3691.572 → 2716.914 |

The 100-TU series was repeated after all other work ended to remove a possible overlap with the early brief focused-test run; its original samples remain in `data/paired-initial.json`. The final 100-TU samples and unchanged larger series form `data/paired.json`. Two warmup rounds followed by 20 measured observations per binary and size; alternating order; same prepared fixture, uninstrumented binaries, CPUs 0–3, four jobs, `/tmp` tmpfs on an i7-12700H. Every run had zero action trace; native outputs remained byte-identical and application stdout passed. Raw distribution and child CPU are in `data/paired.json`; no outliers discarded. Child CPU includes the complete CLI process and descendants, excluding parent driver work.

OLS fitted to the four size medians: baseline **102.141 µs/TU**, candidate **76.880 µs/TU**; intercepts **-0.321 ms** and **1.635 ms**. The slope improves **24.73%**. At 10k wall time improves **24.71%**, child CPU **26.40%**. These are descriptive same-node results, not confidence intervals or an official Labs campaign.

The earlier investigation's ~603 ms and ~60 µs/TU were under different host conditions. Today's fresh baseline is ~1,024 ms and ~102 µs/TU. Only the fresh alternating baseline/candidate series supports this optimization's improvement; the earlier median is not used as the comparator.

## Changed-input controls

**44/44 observations per binary PASS**, **88 total**. Every observation checked the exact nonduplicated action set, application stdout and transitive include dependency audit. Every paired baseline/candidate native output SHA-256 map matched. Copies and mutations are outside timed CLI execution; raw timings and child CPU are retained. Compilation dominates global edits; small differences there are not evidence of a global-build speedup.

| TUs | Change | Samples per binary | Expected actions | Baseline median, ms | Candidate median, ms |
|---:|---|---:|---:|---:|---:|
| 100 | leaf | 3 | 3 | 68.642 | 67.022 |
| 100 | private | 3 | 3 | 69.872 | 66.111 |
| 100 | medium | 3 | 12 | 112.556 | 106.999 |
| 100 | global | 3 | 111 | 639.495 | 642.745 |
| 1,000 | leaf | 3 | 3 | 199.516 | 179.070 |
| 1,000 | private | 3 | 3 | 203.703 | 187.524 |
| 1,000 | medium | 3 | 102 | 703.213 | 688.063 |
| 1,000 | global | 3 | 1011 | 5492.863 | 5528.671 |
| 5,000 | leaf | 3 | 3 | 876.274 | 762.803 |
| 5,000 | private | 3 | 3 | 864.782 | 750.552 |
| 5,000 | medium | 3 | 502 | 3629.085 | 3407.698 |
| 5,000 | global | 1 | 5011 | 31393.153 | 28845.826 |
| 10,000 | leaf | 3 | 3 | 1787.469 | 1553.255 |
| 10,000 | private | 3 | 3 | 1783.127 | 1539.935 |
| 10,000 | medium | 3 | 1002 | 7366.778 | 7271.504 |
| 10,000 | global | 1 | 10011 | 58683.804 | 58695.598 |


## Correctness

- `make test`: all targets passed, assembled from `make -j4 -o test-pagination-ordering test` plus the unchanged pagination target run natively with tracing. The original sandbox run could not ptrace; full-suite escalation timed out, so the narrow tracing check was run separately and passed. Logs retain the precise coverage rather than claiming a single successful full invocation.
- Focused native helper and 30 CLI adversarial cases PASS in modified/hash/hybrid: older/equal/newer, missing/deleted/recreated, denied-parent stat error, unreadable changed content, followed alias, escaping leaf, broken link and post-pre-hook dependency mutation. Helper also covers directory, symlink cycle, native subsecond field precision and adjacent-nanosecond ordering. POSIX stat of an unreadable file can succeed; changed content forces the separate checked read to fail conservatively.
- Deterministic snapshot matrix **27 cases, zero contractual failures**; snapshot contracts PASS, including fingerprints, generated variants, declared hooks, conflicting reads, output validation, native/external/interrupt barriers and migration.
- Randomized differential smoke **9 seed/mode combinations, 288 operations, 645 builds, 288 clean oracles PASS**, seeds 0/1/2 with 32 steps in each incremental mode.
- Relevant mutation guards **9/9 caught**, none missed.
- Disposable build-system correctness fixture: **45 records PASS**, 14 scenarios including generated, targeted, unrelated, rename and deletion across Make/Ninja/Nift. No official campaign was rerun.
- Unchanged generated dependency boundary: output mtimes unchanged; candidate Nift runs **2** generators, Ninja with restat **2**, Ninja without restat **114**. Candidate dirty checks repeat after producer execution; no status survives that boundary. Generated changed/alias freshness and hook boundaries also pass existing contracts.
- NRS **94/94 PASS** against candidate CLI and freshly rebuilt candidate embed libraries. PRS **12/12 PASS**. Package sandbox run had local-server socket failures; native rerun passed.
- Focused helper ASan+UBSan PASS with `ASAN_OPTIONS=detect_leaks=0`. LeakSanitizer was disabled because it could not operate under this sandbox; no leak-sanitizer certification is claimed.

## Cross-platform and retention

Linux: **PASS**, including native nanosecond precision, error behavior, measurements and the above contracts. macOS: **PASS** natively on production SHA in run 38063376419. Windows: **PASS** natively in the same run, including native `FileBasicInfo` reference comparison. Six POSIX permission fixtures are explicitly skipped on Windows; the other 24 CLI cases pass. Platform branches were reviewed against the [Apple stat contract](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/lstat.2.html), [Microsoft FILE_BASIC_INFO contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_basic_info) and [following-handle rules](https://learn.microsoft.com/en-us/windows/win32/fileio/symbolic-link-effects-on-file-systems-functions). Documentation review does not substitute for native tests. Windows permission and symlink capabilities require native verification; its tests record unsupported fixtures as skips.

Attribution is the isolated replacement of duplicate following existence/mtime queries with a checked local observation. Safety, I/O volume, dependency visitation and hash counts are unchanged. The measured N-dependent metadata and wall-time slopes fall materially. Optional Callgrind was not rerun; no new allocation/path-churn result is claimed.

The original recommendation was **RETAIN**. The subsequent authorized landing and exact-SHA native certification completed adoption. Windows POSIX permission skips remain explicitly documented. The separate Experiment 2 report records the next bounded candidate and review stop.

## Receipts and reproduction

Raw receipts and drivers are under `data/`; `SHA256SUMS` binds each receipt. The drivers reference the diagnostic fixture archive at `/tmp/nift-noop-investigation` and the preserved baseline at `/tmp/nift-status-experiment/nift-baseline`; recreate those from the accepted investigation if rerunning elsewhere. Instrumentation is confined to a diagnostic scratch core. The production helper/CLI tests are integrated through `make test-dependency-status`.

Commands: `python3 paired.py`, native `python3 traces.py`, `python3 changed-paired.py`; randomized `tests/randomized_incremental_differential.py` with seeds 0/1/2, steps 32 and all three modes; `scripts/consumer_snapshot_mutations.py`; `make test`; NRS `run-contract.sh` with candidate `NIFT_BIN` and `NIFT_EMBED_PREFIX`; PRS `run.sh` with candidate `NIFT_BIN` and `NIFT_EXPECT_VERSION=4.11.0`; archived `certify.py --kind native --size 100 --profile light --jobs 4 --results <scratch>` with candidate `NIFT` and diagnostic Ninja. Consult the receipts/source scripts for exact fixture setup and assertions.

## Hosted adoption checkpoint

Exact production SHA: `9ce43cac3bddf6e5076d02da5525d424dd22a04f`. [Checkpoint 10 run 38063376419](https://github.com/nift-dev/nift/actions/runs/38063376419): Linux/macOS/Windows corpora and normalized comparison PASS. Linux/macOS focused CLI status cases: 30 PASS each. Native Windows Python: 24 PASS, six POSIX permission fixtures explicitly skipped; symlink/escaping/broken-link fixtures PASS, not skipped. The native Windows C++ helper checks `FileBasicInfo` seconds/nanoseconds against the native timestamp; directory and native handle-error observations are exercised. Targeted, generated and hook contracts also pass the checkpoint wall. A Windows hook fixture uses the established native interpreter path conversion. Hosted skips are not permission-ACL certification.

The original report above documents the pre-adoption review state; this hosted checkpoint closes that outstanding native execution gap. All ten relevant hosted workflows completed successfully at the exact production SHA; run IDs and conclusions are preserved in `data/hosted/hosted-api.json`. Test-integrity run [38063376434](https://github.com/nift-dev/nift/actions/runs/38063376434) passed on unchanged attempt 2, including `make -j2 test-all` and the serial build-boundary preservation proof. Attempt 1 failed the typed-content full-build scaling timing guard (8.2× against an 8.0× threshold); three isolated controls on each original/retained binary passed at 2.6–3.4×. No source, test or threshold changed for the retry. Both attempts and isolated controls are preserved in `data/hosted/`. The official Labs campaign remains frozen.
