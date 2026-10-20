# Progress

_Last updated: 2026-10-18_

## Where things stand

| CP | Status | Branch / commit(s) | Notes |
| --- | --- | --- | --- |
| CP00 | complete | `runtime-2` | Baseline in `benchmarks.md` |
| CP01 | complete — Linux, macOS, Windows (CI @262d3ec) | `runtime-2` | Defect D1 fixed; review F1–F7 resolved or documented; Windows coarse-clock path compiled and tested in CI (`checkpoints/cp01-modified-race.md`) |
| CP02 | complete — Linux, macOS, Windows (CI @262d3ec) | `runtime-2` | Defect D2 fixed; review findings 1–7 fixed, 8/10a/10b open for CP16, 9 documented; @input shadow probe fixed after CI (`checkpoints/cp02-dynamic-reads.md`) |
| CP03 | complete — Linux, macOS, Windows (CI @262d3ec) | `runtime-2` | D-002 filesystem freedom; contract tests changed (incl. dependency_status escaping symlink); regression suite `runtime-2` updated (`checkpoints/cp03-filesystem-freedom.md`) |
| CP04 | deferred (owner, D-006) | not committed | 12.4× met but behaviour diverges; probes in `tests/runtime2/vm-corpus/cp04/`; patch kept outside the branch |
| CP05 | complete — Linux, macOS, Windows (CI @c5db14d) | `runtime-2` | Flat call in loop −10.5% (top level −4.6%); review: unbounded cache memory and latched flags fixed (`checkpoints/cp05-call-path.md`) |
| CP06 | complete — Linux, macOS, Windows (CI @262d3ec) | `runtime-2` | Template scanner dispatch: −57.8% full-build instructions, byte-identical; review found no output difference in ~37k cases (`checkpoints/cp06-scanner-dispatch.md`) |
| CP07 | complete — Linux, macOS, Windows (CI @aba3a50) as leak/resource fix; launch-cost gate **deferred, not passed** (D-011) | `runtime-2` | Anonymous capture files; pipe attempt rejected and preserved (`checkpoints/cp07-process-capture.md`) |
| CP08 | complete — Linux, macOS, Windows (evidence split across two commits, see below) | `runtime-2` | Host-op trace, `tools/rt-diff` harness (policies, minimiser, 83 self-tests), corpus, semantics v1.1; two adversarial review rounds addressed (`checkpoints/cp08-semantics-harness.md`) |
| CP09 | complete — Linux, locally certified (`make certify` @0dc60c6: 0 FAIL); cross-platform certified at the CP09–CP11 milestone (Linux, macOS, Windows; see below) | `runtime-2` | `src/rt` value model; review round 1: 3 defects + 4 risks fixed (reference-invalidation contract documented only), verified by the reviewer's repros ported as regression tests; no second review round (`checkpoints/cp09-value-model.md`) |
| CP10 | complete — Linux, locally certified (`make certify` @0dc60c6: 0 FAIL); cross-platform certified at the CP09–CP11 milestone (Linux, macOS, Windows; see below) | `runtime-2` | Script lexer/parser; review round 1: 10 defects + 2 risks fixed by Opaque fallback, new negative gate against legacy (0 failures, 28,464 programs); no second review round (`checkpoints/cp10-parser.md`) |
| feature-sha256 | complete — Linux, locally certified (`make certify` @ae0b7ac: 1 FAIL in test-test-integrity fixed at ced1f98 and re-certified; 159 PASS, 7 classified); cross-platform certified at the CP09–CP11 milestone (Linux, macOS, Windows @c6fac06 after two CI-only fixes; see below) | `runtime-2` | Native `sha256()` / `sha256_file()` (vendored Bitcoin Core v29.0 SHA-256, SHA-NI with portable fallback); one review round, 2 defects fixed (incl. a stale-page bug in the first `@try` dependency fix) (`checkpoints/feature-sha256.md`) |
| CP11 | complete — Linux, locally certified (`make certify` @bf32e4f: 0 FAIL, 161 PASS, 7 classified; test-rt-vm re-run at the final commit); cross-platform certified at the CP09–CP11 milestone (Linux, macOS, Windows; see below) | `runtime-2-cp11i` (local, not pushed; rebased on `runtime-2` @`c4c5894`) | VM proof of concept, **gate GO** (re-measured on the final build, idle host): register bytecode VM over CP09 values, compiler from the CP10 AST, heap frame stack (100k-frame budget, exact memory budget, no C++ recursion), whole-unit fallback plus enumerated value-dependent bail-and-replay, opt-in `NIFT_VM=1`/`require`. Round 1: 7 miscompiles + 2 risks fixed. Round 2: legacy's prepared evaluator inventoried (11 differences), M1–M4 refused/Opaque, R5/R6 (CP09 follow-ups). Round 3 (last, D-015): F1 member-path element targets refused (`index-target`), F2 comparison chains refused (`comparison-chain`), F3 tree depth bounded before analysis and prepared walks iterative (100k-term inputs no longer crash compilation), F4 aggregate equality O(depth) memory (CP09 follow-up), DOC-1/2 corrected; D-016: supported-subset generator (100 % on the VM required and met; the deliberate replay cases moved to a separate runtime-replay suite, the CP12 backlog), general generator coverage reported (22.7 % VM, no threshold), unsupported generator (500/500 refused before execution). `make test-rt-vm`: 37,421 adversarial programs in 21 families, 0 non-approved differences; sanitizer clean; reviewer material rounds 1–3: 0 non-approved differences. Flat call 461 instr (v4.12 14,994: 32.5× fewer; CPython 953: 0.48×), fib call 471; 75 of 757 shared-corpus scripts run on the VM (`checkpoints/cp11-vm-poc.md`) |
| feature-build-recovery | complete — Linux, locally certified (`make certify` @4556896: FAIL 0, PASS 163; UNVERIFIED 4: Go toolchain download blocked, `dotnet` absent, `/usr/bin/time` missing for two prepared-guard targets; SKIPPED-ENVIRONMENTAL 3: root-only, pass as unprivileged user); rebased to `eda8103` with no source/test change; cross-platform pending (next CI milestone) | `runtime-2` | D-020: ordinary failures never need `--repair` (failed pages keep last-good output, listed in `.nift/failed.json`); `.unfinished` only for uncertain state, recovered automatically; one review round (F1–F8 fixed: bounded corrupt-state handling, pagination cleanup, symlink-preserving restore); randomized soak 2,250 steps, 0 problems; equal-timestamp extra rebuild allowed only when confirmed on disk (`checkpoints/feature-build-recovery.md`); regression-suite patch prepared, not pushed |
| feature-site-dx | complete — Linux, locally certified (`make certify` @1e4cd54: FAIL 0, PASS 164; UNVERIFIED 4: Go toolchain download blocked, `dotnet` absent, `/usr/bin/time` missing for two prepared-guard targets; SKIPPED-ENVIRONMENTAL 2: root-only; BASELINE-FAILURE 1: `test-v43-language` fails as root on v4.12 too, and its unprivileged rerun failed only the wall-clock `typed_content_scaling` ratio (8.4x) — instruction counts show identical linear scaling on `runtime-2` and this branch (3.93x for 4x pages, +0.6% instructions), so it is wall-clock noise, not counted as a pass); first certify @2b5b179 had 4 FAILs, each resolved with case evidence (`checkpoints/feature-site-dx.md`, Certify follow-up); cross-platform pending (next CI milestone) | `runtime-2` | `build --auto` keeps watching after errors (no lost edits, no loops; clean SIGINT/SIGTERM stop), `--auto`→`--repair` workaround removed on top of build recovery; unterminated-literal errors; real error positions in try/catch; `run()` launch failures and per-stage pipeline status; conservative warnings by context (34→14 FPs, 6/6 typos kept); `--strict` follows actual substitution, verified per page via build records (missing record = unverified); `@path` warning costs nothing when off (1k-page site: +0.8% on, −2.2% off) |
| feature-serve | complete — Linux, locally certified (`make certify` @c285fa4: FAIL 0, PASS 165; UNVERIFIED 4 and SKIPPED-ENVIRONMENTAL 3, the known environmental set); first certify @5022d35 failed `test-test-integrity` (pipefail, skip-as-pass in `tests/serve_smoke.sh`), fixed; Windows code and `tests/serve_windows_lifecycle.ps1` never compiled or run here, macOS and IPv6 untested — all at the next cross-platform milestone | `runtime-2` | D-026: `nift serve` wraps Python 3.8+ `http.server` (argv, isolated mode `-I -B`), POSIX exec after checks, Windows launch-and-wait with kill-on-close job object (warns if unavailable); one review round (3 defects, 2 Windows risks fixed) (`checkpoints/feature-serve.md`) |
| CP12 | phase A complete — Linux, locally certified (`make certify` @ebc2656: FAIL 0, FLAKY 0, PASS 168; UNVERIFIED 2: `dotnet` absent, Go toolchain download blocked; SKIPPED-ENVIRONMENTAL 3: root-only); rebased onto `runtime-2` @ead3f8b with no source/test change; cross-platform pending (next CI milestone) | `runtime-2` | Engine selected before execution; bail-and-replay removed (D-013/D-018). Review round 1 (D1–D4, policies) and round 2 (VM self-attestation): D-018a policy and deleted-line rule removed, 108 pinned golden programs with independent oracles (Python ints, CP10 AST spans), generators within ±2^53, int-bounds suite (2,682 programs), D-018c checked against the AST; permanent planted-defect gate `test-rt-vm-mutants` (8/8 rejected, unmutated passes; shared stats-file race fixed). Hot paths: flat call 425, fib 285/call, loop 109 instructions. Coverage: shared corpora 101 of 786 on the VM (103 before round 2; ~1,700 adversarial programs now refused instead of approved); supported-subset generator 100 %. Integrated with two separately attributable commits: CP09 `inline_text` null-memcpy fix (UB found by CP15 review; direct regression test, fails under UBSan without the fix) and the certification test-harness fixes (`test-build-recovery` confirmed equal-timestamp allowance; `test-site-dx` D1 waits for watcher idle) (`checkpoints/cp12-engine-selection.md`) |
| CP13 | phase A complete (syntax prevalidation, multi-line `return`, `runtime.*` error codes, site-DX reconciliation, import hook); phase B open (VM `throw`/`try`, structured error records, opt-in JSON diagnostics, `eval` prevalidation) — Linux, **remote-certified on Linode** (`make certify` @53bc509a: FAIL 0, BASELINE-FAILURE 0, UNVERIFIED 0, FLAKY 0, PASS 170; SKIPPED-ENVIRONMENTAL 3: root-only, each passing as unprivileged `tester`); first remote run @0b879df not certified (`null?["x"]` false rejection, hidden by a harness masking bug — both fixed); cross-platform pending | `runtime-2` | prevalidator R1–R5 with defer rule (a finding stands only if a raw scan agrees); positive syntax corpus (17 programs, legacy-exact) + operator differential (346); rules 119/119; zero-false-rejection gate in tool and check mode; 4 certify failures at fb9112d fixed (trailing-comma diagnostics preserved, shared `src/rt/parser-sources.txt`, test-integrity ×6). Integrated with the certify-harness fix (same-user, same-step BASELINE-FAILURE; pure `decide()` with a 14-case self-test) (`checkpoints/cp13-diagnostics-design.md`) |
| CP14 | phase A complete (F1/F2/F3/F6: 73 natives wired); phase B open (F4 mutating array methods ×9 — waits for the CP12B place model; F5 callback methods ×17 such as `map`, `sort_by` — wait for native-to-compiled callbacks) — Linux, locally certified (`make certify` @6c26404: FAIL 0, FLAKY 0, PASS 168; UNVERIFIED 2: `dotnet`, Go toolchain; SKIPPED-ENVIRONMENTAL 3: root-only); remote Linode certification of the same commit run in parallel for D-033 validation; rebased onto `runtime-2` with no source/test change; cross-platform pending | `runtime-2` | 73 native built-ins (strings, arrays, objects, conversions/formatting) over `rt::Value`, free of `RuntimeValue`, wired into compiler/VM (W1–W7) with legacy-identical errors; refusals for arity, lazy args, receivers, object `length` (quirks Q1–Q14 recorded, D-023); two review rounds on the natives; unit 635, differential ~5k/seed 0 differences, sanitizer clean, 25 callgrind guards; planted-defect gate 8/8; hot paths unchanged (425/285/109). Supported-subset spec narrowed by one named pattern (`double-via-binding-assign`, kept as must-stay-refused fixtures). Coverage unchanged by itself: shared corpora 104/788 (`checkpoints/cp14-builtins-design.md`, CP14 wiring record) |
| CP15 | phase A complete — Linux, certified on Linode (`make certify` @9cf5cd0 on base 5e4e6144: FAIL 0, BASELINE-FAILURE 0, UNVERIFIED 0, FLAKY 0, PASS 170; SKIPPED-ENVIRONMENTAL 3: root-only, pass as unprivileged `tester`); rebased onto `runtime-2` with no source/test change; cross-platform pending (next CI milestone) | `runtime-2` | Built-ins B phase A: natives wired; W9 option A (JSON-valid `{...}` literals read through the JSON path, as legacy), sign of zero preserved (closes the `-0` defect), generator JSON-only forms without narrowing, digit-led text typing in `text_type.cpp`. Open: `object_member` 383 → 681 instructions (integral JSON numbers held as doubles, as legacy; mixed int/double `AddAssign` path) — DeepSeek fast-path candidate (`checkpoints/cp15-builtins-design.md`) |
| CP17 | core phase complete — typed handles (`src/rt/handle.*`, `src/rt/handles/`): generation-checked handles owned by execution contexts, authoritative lifecycle with completion API, bounded slot reuse, close/detach semantics (D-039); not yet linked into `nift` (VM parts supplied as a patch; CP16 swaps onto it) — Linux, certified on Linode (`make certify` @90252d5 on base a8efbd76: FAIL 0, BASELINE-FAILURE 0, UNVERIFIED 0, FLAKY 0, PASS 170; SKIPPED-ENVIRONMENTAL 3: root-only, pass as unprivileged `tester`); earlier run @6fea85e (pre-CP15 base) also clean; rebased onto `runtime-2` with no source/test change; cross-platform pending | `runtime-2` | Adversarial review: handles suite, ASan/UBSan, ThreadSanitizer, guards, box-patch all pass; mutation testing 46/46 detected (two surviving mutants in the first round — detached-supervised, close-ignores-in-flight — fixed by strengthened tests) (`checkpoints/cp17-typed-handles.md`) |
| feature-host-deps | integrated — Linux, certified on Linode (`make certify` @847e65d on base a8efbd76 with parent CP17 @90252d5: FAIL 0, BASELINE-FAILURE 0, UNVERIFIED 0, FLAKY 0, PASS 171; SKIPPED-ENVIRONMENTAL 3: root-only, pass as unprivileged `tester`); first run @8b37d3f not certified (`test-consumer-snapshots` regression, fixed); rebased with no source/test change; cross-platform pending (Windows capture-bound code not compiled here) | `runtime-2` | D-028 legacy fixes before CP16: `open_bytes`/stream/`rw`/`page(x).content`/`minify` reads recorded, failed reads recorded as absent/unreadable; `file().copy/move` freedom; review round 1: async/thread reads merged, copy sources recorded, captures bounded while the child runs, build writes refused into `.nift/` and other pages' outputs (owner D-006 decision), cross-device moves; bounded whole reads with fatal `limit.exceeded` (`checkpoints/feature-host-deps.md`) |

### CP08 certification evidence (owner-accepted 2026-10-18)

| Check | Commit | Result |
| --- | --- | --- |
| Checkpoint-10 cross-platform equivalence (Linux, macOS, Windows) | `bce5913` | pass |
| v4.4/v4.5 cross-platform (Linux, macOS, Windows) | `bce5913` | pass |
| Regression suite `runtime-2` vs core `runtime-2` | `bce5913` | pass |
| Test integrity guards (binding gates, static scan, aggregate evidence on ubuntu and macOS, fast contract suites) | `600fe99` | pass |

`bce5913..600fe99` changes no product source (`git diff -- src` is empty): the
rt-diff `trace-async-reorder` mutant fix, the certify runner and its guard, CI
annotations, private temp files in three tests, and docs (D-012). The integrity
job at `bce5913` failed only on the mutant self-test fixed in `aed991b`. No full
matrix was re-run to consolidate (D-012).

### CP09–CP11 + feature-sha256 cross-platform milestone (2026-10-18)

| Workflow (run) | Commit | Linux | macOS | Windows | Other jobs |
| --- | --- | --- | --- | --- | --- |
| Checkpoint 10 cross-platform equivalence (39100542589) | `f3208f4` | pass | **fail** (Native SHA-256 step) | **fail** (Native SHA-256 step) | compare: skipped |
| Test integrity guards (39100545658) | `f3208f4` | binding gates **fail** (`make -j2 test-all`: load-sensitive `v44_perf_scaling_smoke.sh`, map 40k=52 ms 80k=232 ms, ratio 4.46 > 3.5); static scan, fast contract suites, aggregate evidence ubuntu: pass | aggregate evidence: pass | — | |
| v4.4/v4.5 cross-platform (39100544195) | `f3208f4` | pass | pass | pass | |
| Regression suite `runtime-2` vs core (39100540920) | `f3208f4` | pass | — | — | |
| Checkpoint 10 cross-platform equivalence (39104574518) | `c6fac06` | pass | pass | pass | compare normalized semantics: pass |
| Test integrity guards (39104576304) | `c6fac06` | all jobs pass: static scan, fast contract suites, aggregate evidence ubuntu, binding gates (`NIFT_DEFER_LOAD_SENSITIVE=1 make -j2 test-all` pass; `make test-load-sensitive` alone pass; build-boundary serial pass) | aggregate evidence: pass | — | |
| Regression suite `runtime-2` vs core (39104571762, push) | `c6fac06` | pass | — | — | |

Root causes of the `f3208f4` failures, all test/CI infrastructure (no product
source changed in `f3208f4..c6fac06`):
- Windows SHA-256: the checkout converted `third_party/bitcoin-sha256/COPYING`
  to CRLF (core.autocrlf), so `check_vendored_sha256.py` found its hash
  unrecorded. Fixed in `b963f62` (`.gitattributes`: `third_party/bitcoin-sha256/** -text`;
  the checker also names CRLF as the cause).
- macOS SHA-256: the Host-op trace check expected `$P/data/x.bin`, but the
  trace records the canonical path (`/private/var/...`; `$TMPDIR` is under the
  `/var` symlink). Fixed in `b963f62` (accepts the `pwd -P` form too). The unit
  tests (incl. ASan/UBSan) and all other builtins checks passed on macOS.
- Linux binding gates: a load-sensitive wall-clock guard ran inside
  `make -j2 test-all`. Fixed structurally in `c6fac06` (guards deferred there and
  run alone by `make test-load-sensitive`; thresholds unchanged). Recorded in
  `certification.md` as a load-sensitive observation (with `974dcb3`), **not
  resolved**.

v4.4/v4.5 cross-platform was not re-run at `c6fac06` (narrowest workflow, D-012;
nothing it exercises changed). Windows aggregate evidence is not part of Test
integrity guards and is not claimed.

### Certification levels (D-012)

- **locally certified** — `make certify` on Linux with every result PASS or classified;
- **cross-platform certified** — the CI workflows above pass on Linux, macOS and Windows;
- **release certified** — full matrix on the exact commit to be promoted, before merge to `main`.

CP09–CP11 and feature-sha256 are certified locally and, at the CP09–CP11
milestone above, cross-platform (`f3208f4` + CI-only fixes to `c6fac06`).

`runtime-2` holds checkpoints that passed every gate. `runtime-2-dev` holds work
whose review is still open; it is rebased onto `runtime-2` before each delivery.

## Environment

- Work happens in a cloud clone of public `main` at `ce8cd36` (D-003); local
  certification on Linux, cross-platform via GitHub Actions on `runtime-2` (D-007, D-012).
- Known environmental failures on this host (also on the baseline) are
  classified by `make certify` (`certification.md`): root-only failures,
  missing `/usr/bin/time`, no usable pty.
- Load-sensitive wall-clock guards run exclusively and keep their thresholds
  (`certification.md` § Load-sensitive measurements).

## Local checkout divergence

Checked with git through the device bridge: `/home/nick/Repositories/nift/nift`
HEAD is `00dda50` = `ce8cd36` plus a version bump touching 7 files
(HANDOVER.md, README.md, docs/guarantees/registry.json,
docs/handover/RELEASES.md, snap/snapcraft.yaml, src/CLI.cpp,
tests/version_consistency_test.py); no modified tracked files; untracked
evidence directories only. `ce8cd36` is its parent, so the bundle's
prerequisite is present, and no `runtime-2` commit touches those 7 files, so
the patches apply without overlap (not yet applied there: nothing is written
to the owner's git repository without approval).

## Next action

1. CP09 (value model) and CP10 (parser): finish local validation, adversarial
   review and docs; promote as locally certified.
2. CP11 (VM proof of concept, GO/NO-GO): compiled Nift programs through the VM,
   compared with v4.12 and CPython on equivalent workloads.
3. Full cross-platform matrix at the CP09–CP11 integration milestone.
4. CP04 resumes after the VM decision (D-006). Carried to CP16: reads inside
   `async` functions, OS resolution of `link/..`, globs in `cp()`/`rm()`, `@path`
   targets, pipe-based execution (D-011).

Notes on history:
- Commit `1ecb2ae` is titled "WIP CP03"; its content is the CP03
  implementation and the review fixes follow in `a02072e`.
- `runtime-2-dev` was rebased once (CP02/CP03 split onto the final CP01) after
  the first delivery bundle. Neither branch has been pushed; fetch the new
  bundle with forced refspecs (see `APPLY.md`). `runtime-2` itself has only
  moved forward.

## Open escalations

None. CP07 launch cost decided by the owner (D-011, option a).

## Regression suite

CI: `.github/workflows/runtime2-regression-suite.yml` runs the suite's
`runtime-2` branch against the core `runtime-2` branch on every push to it
(D-008: neither `main` changes until both are promoted together).

`nift-dev/nift-regression-suite` is a certification gate for every checkpoint
(`./run-contract.sh <nift>`). On this host v4.12.0 fails 4 modules
(unreadable_source: runs as root; v44_executable_script: needs `nift` on PATH;
pagination, v45_target: timing flakes) and `incremental_new_features` is flaky
(2/20 on v4.12.0). The suite's `runtime-2` branch encodes D-002 (D-008).

## CI status (D-007, D-012)

`runtime-2` is pushed to `origin` (certified commits only). Workflows run by
`workflow_dispatch` on the branch, narrowest workflow first; the full matrix only
at integration boundaries, for platform-sensitive changes, or before promotion.
