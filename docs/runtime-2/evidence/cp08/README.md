# CP08 evidence (2026-10-17, Linux x86_64, 2 vCPU)

## Hardening after review (v2) — current numbers

Binary: `runtime-2-cp08` (rebased on `runtime-2` 262d3ec).

| Gate | Result |
| --- | --- |
| (a) same binary as A and B: committed corpora (rt-diff-corpus, vm-corpus, semantics, cp410 probes, conformance and bindings sites) + 262 extracted smoke scripts + 3,000 generated programs and 150 generated sites (seed 2026), all channels incl. traces, run twice | 3,665 entries (3,494 scripts, 166 sites, 4 workspaces, 1 eval) per run; 0 differing in both; 2,610 entries traced, 20,876 Host ops compared (`gate-a-v2-run{1,2}-summary.json`). The first attempt found 2–5 differing entries per run: generated programs whose two async tasks raced on the same file — a real nondeterminism the harness caught; fixed in the generator |
| (b) seeded mutations (`selftest.txt`) | all 17 caught on the expected channel: file modes, empty directory, `.nift` garble, HOME write, TMPDIR write, HOME/TMPDIR swap, stdout-only, stderr-only, exit-only, stdin-only, eval key order, number formatting, interleaving, trace drop, trace reorder, async-task reorder, `time taken` wording |
| normalisations hide only what they document | identity and `time taken` number hidden; page-info timing keys and listing fingerprint values hidden while other keys and listing keys stay visible |
| Host-op coverage | dropping each of 17 op categories from the trace is caught |
| tracing neutrality | corpus with tracing on vs off: 0 differing outside the trace; closed fd, 25 pages × 4 threads, 5 runs: identical outputs; `NIFT_TRACE_HOST=1` refused |
| policies | D-009 policies approve the emulated intended outputs (13 channels, 6 probes) and nothing else; without them the same output is 13 channel regressions; wrong substitution, wrong count and pattern policies refused |
| robustness | missing corpus, empty corpus, below `--min-entries`: exit 2 |
| reproducibility | generator byte-identical by seed; programs record seed/index; a seeded difference minimised to an 8-line reproducer with replay script |
| (c) trace-off overhead, 300-page site (callgrind) vs runtime-2 262d3ec | full 366,587,974 vs 366,566,921 (+0.006%); no-op 118,559,312 vs 118,565,802 (−0.005%); gate +0.5% |
| (d) `make -k -j4 test-all` (root sandbox) | failing: csharp-binding (no dotnet), go-binding (toolchain download blocked), test-dependency-status, test-progress-pty, test-unreadable-source, test-v43-language (root), two v48 guards (no `/usr/bin/time`), test-v44-language-foundation (wall-clock scaling guard: passes alone; runtime-2 binary fails it 1/6 alone, this binary 0/6). All `test-runtime2-*` targets pass, incl. `rt-diff` (same binary) and `rt-diff-selftest` |
| regression suite (`nift-regression-suite` runtime-2 7ba5437) | 93/94; only `unreadable_source` (root) |
| `scanner_parity.sh` vs runtime-2 | 72 sites, 222 scripts, PASS |
| false-green scan (`tests scripts tools`) | 0 findings, 347 files |

Corpus composition and quality:

| Source | Entries | Notes |
| --- | ---: | --- |
| `tests/runtime2/rt-diff-corpus` | 19 | 4 sites (multi-page/threads/pagination; incremental in 3 modes with status/info/track/cp/mv/rm), 4 workspaces (filesystem freedom, external output, missing external file, script outside), 3 async/thread probes, sidecar probes, trace coverage, eval |
| extracted smoke scripts | 262 | 146 run to completion (138 before; import failures 38 → 7); most others are the tests' intended negative cases |
| generated programs (per 3,000) | 3,000 + 150 sites | 88% of programs run to completion; deliberate final errors and recursion-limit probes account for the rest. Construct coverage: async 27.7%, threads 29.1%, modules 27.6%, break/continue 33.5%, `&&`/`||`/`!` ~39%, floats 35.3%, string methods 55.2%, interpolation 55.8%, try/catch 34.4%, structs 25.9%, closures 27.8%, file writes 46.3% |

## First version (before review) — kept for history

## (a) Same binary as A and B

`rt-diff --a nift --b nift` over the committed corpora (vm-corpus, semantics,
cp410 probes, conformance and bindings sites), 262 scripts extracted from the
smoke tests, and 2,500 generated programs (seed 2026), Host-op traces compared:

| Run | Entries | Differing |
| --- | ---: | ---: |
| 1 | 2,995 (2,983 scripts, 12 sites) | 0 |
| 2 | 2,995 | 0 |

Summaries: `gate-a-run1-summary.json`, `gate-a-run2-summary.json`. In each
run 1,359 entries produced Host-op traces (9,198 operations compared). In
`make test-runtime2-rt-diff` (200 generated): 187 entries, 887 Host ops.

All gates were re-run on the final binary (after the trace call sites in
templates were guarded by `nift::trace::enabled()`); the numbers here are from
that re-run.

## (b) Seeded differences

Variants built from a copy of this tree with one change each; A is this
tree's binary. Full DIFF listings (first 60 lines) in `gate-b-*.txt`.

| Variant | Change | Entries differing | Channels |
| --- | --- | ---: | --- |
| v1-trace | one extra `stat` trace line after every `read` | 140 | trace only |
| v2-output | newline appended to every page output file | 7 | files only |
| v3-stderr | "division by zero" → "divide by zero" | 15 | stderr only |
| v4-modulo | integer `%` result made non-negative | 7 | stdout only |
| CP04 experiment | top-level code on the prepared path (`--no-trace`: that binary predates the hook) | 274 | stdout 238, stderr 58, exit 8 |

Negative control: the first v4 build patched only the floating-point modulo
path, which integer operands never reach; rt-diff reported 0 differing, and
`-7 % 3` printed -1 on both binaries.

## (c) Trace hook cost when unset

300-page Markdown site, callgrind, both binaries in one session
(`gate-c.txt`):

| Binary | full build | no-op build |
| --- | ---: | ---: |
| runtime-2 | 800,552,368 | 94,058,887 |
| CP08 | 801,244,016 (+0.09%) | 94,060,881 (+0.002%) |

## (d) Test suite

`make -k -j4 test`: failing targets exactly test-dependency-status,
test-progress-pty, test-unreadable-source, test-v43-language,
test-v48-prepared-collection-guard, test-v48-prepared-object-literal-guard
(environmental: root user, no `/usr/bin/time`, no pty). Inside that run:
`PASS vm corpus: 101 scripts` and `rt-diff: 695 entries, 0 differing`.
All `tests/runtime2/*.sh` pass when run directly.

## Round 2 (after the re-review; rebased on runtime-2 084f09f)

Files in `round2/`.

### (a) Same binary, full corpus + 3,000 generated programs, twice

Committed corpora, extracted smoke scripts, 3,000 generated programs and 150
generated sites (seed 2026), traces compared, `--jobs 3`:

| Run | Entries | Differing | Traced entries (Host ops) | Combined order unstable |
| --- | ---: | ---: | ---: | ---: |
| 1 | 3,672 | 0 | 2,611 (20,977) | 0 |
| 2 | 3,672 | 0 | 2,611 (20,977) | 0 |

The first attempt reported one difference: `smoke/v44_executable_script_smoke/010-signal`
loops until its test sends SIGTERM, so both sides timed out. Round 1 counted
that as equal; round 2 reports every timeout. It is excluded (`channels=all`,
reason recorded) — it never finishes on its own.

### (b) Seeded mutations — `round2/selftest.txt` (83 checks, all pass)

| Mutation | Source | Caught on |
| --- | --- | --- |
| file-mode, empty-dir, nift-garble | review 1 | files |
| home-write / tmp-write | review 1 | home / tmp |
| home-tmp-swap | review 1 | stdout, combined |
| stdout-only, stdin-only, eval-key-order, number-format, time-word | review 1 | stdout |
| stderr-only / exit-only | review 1 | stderr / exit |
| interleave | review 1 | combined |
| trace-drop, trace-reorder, trace-async-reorder | review 1 | trace |
| x-info-reformat, x-info-dupkey, x-info-escape, x-info-number | re-review | files (page-info bytes) |
| x-racy-inject, x-tick-inject, x-listing-garbage | re-review | files (every site/workspace) |
| x-escape-write | re-review | files (every entry) |
| x-trace-extra-field, x-trace-relabel | re-review | trace |
| x-trace-page-reverse (single-threaded build) | round 2 | ctx-order |
| x-time-user (script prints `time taken: 1 seconds`) | re-review | stdout |
| interleave in an async script and in a script whose comment mentions async | re-review | combined |
| timeout (child `sleep 25`; busy loop) | re-review | timeout; no process left |
| policy for another program with the same entry id | re-review | not approved; UNUSED-POLICY |
| policy with empty `old` (and four other invalid shapes) | re-review | refused, exit 2 |
| policy substitution plus an unrelated change | re-review | not approved |
| each of 17 trace op categories dropped | round 1 | trace |

Hidden by design: identity; `time-number` (Nift's build summary line only);
`x-trace-cross` (cross-task order — no happens-before edges in the trace).

### (c) Trace hook cost when unset

300-page site, callgrind, runtime-2 084f09f vs CP08 round 2:

| Binary | full build | no-op build |
| --- | ---: | ---: |
| runtime-2 | 357,232,724 | 114,917,758 |
| CP08 | 357,247,633 (+0.004%) | 114,912,341 (−0.005%) |

### Determinism under load

`tests/runtime2/rt-diff-corpus` same-binary at `--jobs 8`: 10 consecutive runs,
0 differing (after the 20 ms settle that removed the tick-hash race).

