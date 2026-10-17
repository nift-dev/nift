# CP08 — Semantics and differential harness

Status: see `../progress.md` (implemented; review findings and owner CP08 requirements addressed).

## Objective
Before any new engine runs user code, make "same behaviour" measurable: a
written specification of current semantics, and a harness that runs thousands
of programs through two binaries and reports every difference in stdout,
stderr, exit status, diagnostics, output files and Host operations.

## What was built

| Piece | Files | Notes |
| --- | --- | --- |
| Host-op trace | `src/HostTrace.h`; trace points in `src/ParserObservation.cpp`, `src/ParserExpression.cpp`, `src/ParserTemplate.cpp`, `src/ParserScript.cpp`, `src/Parser.cpp`, `src/Process.cpp`, `src/JobControl.cpp`, `src/ParserAsyncPool.cpp`, `src/ProjectInfo.cpp` | `NIFT_TRACE_HOST=<fd>`; JSON lines; per-page context; format in `../trace-format.md` |
| Harness | `tools/rt-diff/rt-diff` | Python 3 stdlib; scripts, eval files, sites; sidecars; ignore lists; JSON report; parallel; per-entry timeout |
| Generator | `tools/rt-diff/gen.py` | Seeded; ~80% of programs run to completion, the rest end in deterministic runtime errors (overflow, recursion limit, missing files, index errors) |
| Smoke extraction | `tools/rt-diff/extract_smoke.py` | 269 scripts from quoted heredocs in `tests/*.sh` |
| Exclusions | `tools/rt-diff/ignore.txt` | 7 extracted scripts that read the clock or interleave threads (nondeterministic on one binary) |
| Semantics probes | `tests/runtime2/semantics/*.f` (57) + golden `.stdout/.stderr/.exit` | One per spec row/edge case |
| Golden runner | `tests/runtime2/vm_corpus.sh` | Now covers `vm-corpus/*/` and `semantics/`; runs in a temp copy and writes `$ROOT` for the path so goldens are checkout-independent (the CP04 goldens recorded an absolute path before) |
| Spec | `docs/runtime-2/semantics.md` v1 | Probe and source columns; decisions per row |
| Make targets | `test-runtime2-rt-diff` (A = B = this binary; committed corpora, extracted smoke scripts, 200 generated programs), `test-runtime2-vm-corpus` | Both in `make test` |

## Corpus (committed or extracted on the fly)

| Source | Entries |
| --- | ---: |
| `tests/runtime2/vm-corpus/cp04` | 44 scripts |
| `tests/runtime2/semantics` | 57 scripts |
| `docs/evidence/cp410-competitiveness/probes` (the 120-script probe corpus) | 120 scripts |
| `tests/conformance/cases/*/project` | 9 sites |
| `bindings/{python,node,csharp}` example sites | 3 sites |
| scripts extracted from `tests/*.sh` heredocs (minus 7 excluded) | 262 scripts |
| total committed/extracted | 495 entries |
| generated (`gen.py`) per run | any number; gate used 2,500 |

## Gates

| Gate | Result |
| --- | --- |
| (a) Same binary as A and B over all 495 entries plus 2,500 generated programs (seed 2026), traces compared, run twice | 2,995 entries, 0 differing, both runs; 1,359 entries traced (9,198 Host ops) (`evidence/cp08/gate-a-run{1,2}-summary.json`) |
| (b) Seeded differences detected | trace-only (extra `stat`): 140 entries, trace channel only; output-file-only (newline appended to page outputs): 7 sites, files channel only; diagnostic wording ("divide by zero"): 15 entries, stderr only; integer modulo sign: 7 entries, stdout only; CP04 experimental binary vs runtime-2: 274 entries (stdout 238, stderr 58, exit 8). A first modulo variant that patched only the float path changed no observable behaviour and was correctly reported as 0 differing |
| (c) Trace hook cost when unset, 300-page site (callgrind) | full build 801,244,016 vs 800,552,368 (+0.09%); no-op 94,060,881 vs 94,058,887 (+0.002%); gate +0.5% |
| (d) Full `make -k test` | failing set exactly the six environmental targets; `test-runtime2-vm-corpus` (101 scripts) and `test-runtime2-rt-diff` (695 entries, 0 differing) pass; all `tests/runtime2/*.sh` pass |

## Findings recorded in semantics v1 (behaviour unchanged, D-006)
- Five divergences between the loop path and the canonical path (type-error
  wording, `const` enforcement, native-name shadowing, error location,
  interpolation including in functions called from loops).
- Runtime errors (undefined callable, index out of range) are not catchable by
  `try`/`catch`.
- Native-vs-user name precedence is per name (`error`, `len` user wins; `min`,
  `type` native wins at top level).
- No `**` operator; `"abc"[1]` unsupported; unknown escapes drop the backslash
  (v0 row corrected).
Eight rows are marked "Owner decision needed" for the new engine's target.

## Residual risks
- Trace scope is parser-level Host operations (see `../trace-format.md`, "Not
  traced"); build-engine reads and `which()` are not traced. CP16 moves all Host
  access behind one layer.
- Scripts that read clocks or interleave threads are excluded by name; new such
  scripts will show up as A = B differences in `test-runtime2-rt-diff`, which is
  the intended alarm.
- The generator covers the constructs listed in `gen.py`; template-directive
  generation is limited to the committed sites.
- Linux only; macOS/Windows pending CI (D-004/D-007).

## Review and hardening (same-model adversarial pass; not independent)

The review attacked the harness with 15 mutation variants and targeted probes.
Each finding and the owner's CP08 requirements (D-009, D-010, owner note
2026-10-17 §2 and §4) were addressed as follows. Every resolution is exercised
by `tests/runtime2/rt_diff_selftest.sh` (`make test-runtime2-rt-diff-selftest`,
part of `make test-all`).

| # | Finding / requirement | Resolution |
| --- | --- | --- |
| R1 | Thin template/site coverage: 12 sites, ~9 pages, one `build --all`, no incremental, status, info, rm, mv; mostly 1 build thread | `tests/runtime2/rt-diff-corpus/`: a 25-page, 4-thread site with partials, `@json`, `@pathto` and pagination; incremental step sequences (edit → status → build → status) in modified, hash and hybrid modes; `track`, `cp`, `mv`, `rm`, `info`; generated template sites (`gen.py`: 150 per 3,000 programs, 1–4 threads) |
| R2 | Async/thread ops untested; sorting hid reordering within a task | ops are attributed to `<owner>/async#N` / `<owner>/thread#N` (creation order on the owning thread) and compared in order per task; three async/thread probes with traced ops; `trace-async-reorder` mutation caught. The gate run found a real race (two generated async tasks writing the same file) — fixed in the generator |
| R3 | Tracing changed behaviour: closed fd reused by page outputs; variable visible to scripts and children; fd 1/2 mixed into output | descriptor validated at startup (`fstat`/`F_GETFL`), fds 0–2 refused unless `NIFT_TRACE_HOST_ALLOW_STDIO=1`, duplicated to a private close-on-exec fd, original closed, variables removed from the environment; `--trace-neutrality` compares every channel except the trace over the corpus; closed-fd 25-page/4-thread reproducer run 5× with identical outputs; `scripts/trace-env` probe |
| R4 | Eval output normalised (sorted keys, re-serialised numbers); no eval corpus | eval output compared byte for byte; `eval/values.eval`; `eval-key-order` and `number-format` mutations caught |
| R5 | File modes, empty directories, `.nift/`, HOME writes, HOME/TMPDIR aliasing, stream interleaving not compared | snapshot records modes, directories and symlinks; `.nift/` compared with two documented page-info normalisations; HOME and TMPDIR are distinct and snapshotted; a combined stdout+stderr run compares write order for scripts without async/threads; all corresponding mutations caught |
| R6 | argv/env/stdin sidecars unused; sites could not take env | sidecars for scripts, `rt-diff.env`/`.steps` for sites and workspaces (also as non-invasive sibling files); corpus uses args, env, stdin |
| R7 | Missing or empty corpus passed | exit 2 for missing/empty corpus, duplicate ids, exclusions without reasons; `--min-entries` (make target: 600) |
| R8 | Much of the corpus failed early: extracted scripts lacked imports; generator bugs (empty-file write, accidental int64 overflow) | extraction replays every quoted heredoc of a test into a file tree (imports and data resolve): 146 of 262 run to completion (138 before; import failures 38 → 7); the remaining failures are mostly the tests' intentional negative cases. Generator bugs fixed: 88% of generated programs run to completion, the rest end in a deliberate final error or the recursion limit |
| R9 | Generator narrower than documented | floats, booleans, `&&`/`||`/`!`, `break`/`continue`, string methods, modules (`import`/`export`), async functions, threads, template sites; coverage per 3,000 programs in evidence |
| R10 | `semantics.md` source references off by 1–12 lines; recursion row ambiguous | every cited line re-verified; recursion row states 64 frames (`down(63)` ok, `down(64)` fails) |
| P | Duplicate entry id `app/site` | labels extended by parent names on collision (`python/app`, `node/app`) |
| O1 | Approved-difference policies (owner §2) | `--policy`: decision, exact entry, channel, exact old→new substitution with count, justification, test; approved differences reported separately; patterns refused; self-tests show a policy approves only its exact difference. `tools/rt-diff/policies/d009.json` + `<probe>.r2.*` intended goldens for the D-009 rows: an engine emitting them is fully approved, and without the policies the same output is 13 channel regressions |
| O2 | Narrow, documented nondeterminism handling | `docs/runtime-2/rt-diff.md`: five rules, each with a self-test showing it hides nothing adjacent; pids, enumeration order and scheduling deliberately not normalised |
| O3 | Host-op coverage | `scripts/trace-coverage` exercises every op; dropping each of the 17 op categories from the candidate's trace is caught |
| O4 | Reproducibility | programs record seed and index; `--repro-dir` delta-minimises differing generated programs (hanging candidates rejected) and writes a replay script; reports record the command line |
| O5 | Semantics categories | `semantics.md` v1.1: legacy behaviour / Runtime 2.0 / approved change / deferred per row; D-009#3 compatibility scan (no existing program outside the two pinning probes relies on a built-in winning) |

## Residual risks after hardening
- Trace scope is still parser-level Host operations; build-engine reads, PATH
  lookups and FFI are not traced (listed in `../trace-format.md`). CP16 moves
  all Host access behind one layer.
- The combined (interleaving) channel is skipped for scripts that use async or
  threads, whose stream order is legitimately nondeterministic.
- `.nift/` is compared as contract (owner direction: page-info is observable),
  with two documented normalisations; a future metadata format change will
  show up as a difference and needs a policy or a recorded decision.
- Policies match exact text; when the new engine's diagnostics positions are
  settled, D-009 intended goldens may need deliberate regeneration (each such
  change is a recorded decision, never a broad suppression).
- Linux only; macOS/Windows pending CI (D-004/D-007).
