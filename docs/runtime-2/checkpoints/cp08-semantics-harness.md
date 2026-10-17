# CP08 — Semantics and differential harness

Status: see `../progress.md` (implemented, under review).

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
