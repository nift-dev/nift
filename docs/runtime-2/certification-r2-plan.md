# Runtime 2.0 first certification (R2): the B21 Linux campaign and gate inventory (draft)

This is a draft written for D-077 #6 (owner 2026-11-12 04:25). It is a plan, and it authorises nothing:
- Linode provisioning, local integration into `runtime-2`, the push, CI dispatch, tagging and release each need
  their own explicit owner approval.
- Certification is not release authorisation (D-076).

The plan builds on certification.md (tiers, `make certify`, classifications, the combined-candidate harness) and
checkpoints/combined-b3-b5.md section 36 (B21: evidence outside every tree, durable per gate, identities kept apart).
It also uses the readiness report CERT-READINESS-2026-11-12 (integrator scratch).

**Current status:** see certification-readiness-r2.md (D-078). It counts 50 deferred-post-cert, 30 implement and 11 open
inventory reasons, and records host-deps as FAILED with 6 outstanding cases. Gates 9 and 11 below are therefore
FAILED today.

## 0. Preconditions: the frozen candidate

The order is D-077 #5:
1. the output-order and std-stream data-loss repair;
2. async recursion memory safety;
3. `save()` filesystem safety;
4. L6 `native::` Option B;
5. freeze.

Each step is independently reviewed and owner-accepted. The stopping rule is D-077's: once the four repairs are
accepted, the candidate is frozen. A new issue expands scope only when it is shown to block correctness, safety or
certification integrity.

**Before the launch, the integrator presents** (D-077 #6):
- the candidate commit and tree;
- the production `nift` sha256 for the GCC 15.2 build, and for the local build for comparison;
- the inventory and gate classifications (sections 2-3);
- the CI changes and the triggers they would cause;
- the outstanding release limitations;
- the expected cost (section 5).

**Standing rule** (owner): before an expensive run, the integrator personally establishes that the complete
candidate passes every locally runnable gate, and that the certification inputs are complete and consistent:
- `certify.py --list` and `--check-plan` are complete;
- the cb35 bundle is rebuilt for the frozen TIP/TREE;
- the oracle pin is recorded.

Every gate that cannot run locally is named before the launch (section 2, column "Where").

## 1. Toolchain and environment pins

| Item | Pin | Checked by |
|---|---|---|
| Compiler | Ubuntu 26.04 `g++` **15.2.0-16ubuntu1**, `__GLIBCXX__` **20260321** | `certify.py --toolchain-preflight`: the D4 profile `tests/runtime2/cp14b/toolchain-profile.json` (libstdc++.so and two header sha256s) |
| Comparator sort | runs on the production VM (`NIFT_CP14B_REQUIRE_COMPARATOR=1`); a comparator SKIP fails the campaign (C3) | preflight + `COMPARATOR_SKIP` |
| Host | g6-dedicated-4 (4 dedicated vCPU, 8 GB); MemTotal ≥ 7600 MiB (preflight); read_limit's default-limit case needs MemAvailable ≥ 4 GiB, otherwise it is NOT RUN | preflight; read_limit.py |
| v4.12.0 legacy oracle | built from tag `v4.12.0` = `ce8cd36a92ecfdc8b15b00df41a6c3bb9a31c0eb`, provenance recorded (`--record-legacy-oracle`, C41) | `--legacy-oracle`; HARNESS_BLOCKED when it is missing |
| Regression baselines | v4.12.0 (`--baseline`), weekend/r2 `0b5079fc` and B4b `17f7964c` (`--regression-baseline NAME=TREE`) | identity.json (B21 3) |
| Evidence | `--evidence-root /var/tmp/nift-certify`, `--work-root /var/tmp/nift-certify-work`, `--campaign-id cb35-<start>-<tip8>`, with `--resume` on restart; the destructive gates run in private copies | B21 1-5 |
| Unprivileged user | `tester` (`--unprivileged-user`) for root-only reruns | SKIPPED-ENVIRONMENTAL |
| Bindings | Go 1.26, Node 22, .NET 10 (cb35 setup) | `test-bindings` |

## 2. Gate inventory

**Where:**
- **Linode** is the B21 campaign.
- **Local** is the cloud development sandbox. It has GCC 13.3, 2 CPUs and less than 1 GB of free disk. Under the
  owner's rule it is used only for code changes and small focused tests.
- **CI** is GitHub Actions.

**Expected** gives the classification expected for the frozen candidate, after the D-077 #5 steps and with the
rulings in section 3 as they stand today.

| # | Gate (group) | Target and criterion | Where | Expected |
|---:|---|---|---|---|
| 1 | Toolchain preflight | GCC 15.2 profile, comparator runs, MemTotal | Linode only (exit 2 locally) | PASS |
| 2 | v4.12.0 legacy oracle | built and pinned, provenance recorded | Linode | PASS |
| 3 | `make certify` full plan | `certify-targets` = `test-all` + `CERTIFY_CI_TARGETS` + `CERTIFY_RUNTIME2_GATES` + the Runtime 2.0 inventory (`inventory()`). 404 gates last time (110461664: 281 ok, 123 FAILED). The exact list is `certify.py --list` at the freeze, recorded in the evidence | Linode | CERTIFIED only if every gate is PASSED; otherwise PARTIAL or FAILED with per-gate classes |
| 4 | MANDATORY gates (`scripts/certify.py` MANDATORY) | b3/b4/b4b/b5 suites and their mutants, b4b fixpoint/work/gen-alias, b06 (+mutants), b5 generators/evalorder (+sanitize)/snapshot-memory, guardoff (+sanitize), `test-cp19-proof-cli`, cp14b design mutants, diagnostics, scanner parity, prevalidate, capture origins, cp12c probes, refsem (+mutants), cp22, cp23, `test-rt-vm`, `test-rt-vm-sanitize`, certification guard | Linode (test-rt-vm also runs locally: about 52 min, uninterrupted PASS at C136) | PASS required for each |
| 4a | **Certification guard** (`test-runtime2-certification-guard`), required gate (D-090, owner 2026-11-14) | certify-targets reaches every required suite and every target and script the CI workflows run (including runtime2-cross-platform.yml); `scripts/certify.py` MANDATORY lists it; also in `make test` | Linode (campaign), local, and the Linux job of runtime2-cross-platform | **FAILED today**: it has failed unnoticed since C140c, because three skip phrases in tests/runtime2/suite_status.sh were never listed. **Expected: PASS once the candidate fix lands.** A FAIL blocks certification; it is never a deferral |
| 5 | Sanitizer tier (ASan+UBSan, TSan) | every `*-sanitize` and `*-tsan` gate of the plan. Today: rt-builtins, rt-value, cp19 session/proof/admission/host/expression/r1, cp23 (+tsan), cp18, f6, rt-handles (+tsan), cp17, rt-source-map (+tsan), rt-error-oom, b5-evalorder, guardoff, cp14b, cp22 (+tsan), coro-module (+tsan), collections (+tsan), refsem (+tsan), b3, b4-tsan, cp18-bytes (+tsan), cp23-shared-unit, rt-host (+tsan), rt-parser-asan, for-s2, `test-rt-vm-sanitize` | Linode only (owner rule; MinGW has no ASan/UBSan) | PASS required. Last observed state: unknown since 110461664 |
| 6 | Mutation tier | every `*-mutants` and `*-mutant` gate of the plan (about 50 in the last listing), plus `test-runtime2-read-limit-mutants` and the B07 full mutation gate (test-side oracle); D-077 #1/#3 add the inventory mutants scope/drift/entry and the read-limit contract controls | Linode (mutant builds) | PASS required: every mutant detected |
| 7 | Performance lane | cp20-launch-bench, cp21-prompt-latency, rt-builtins-guards, for-s2-bench and literals-scaling, run alone | Linode | PASS against their checkpoint budgets; a failure is recorded and decided explicitly (owner: intermediate thresholds are not absolute release blockers) |
| 8 | Website-build benchmark vs **v4.12.0** | `tests/runtime2/cp19/website_performance.py` / `test-performance-scaling`; quick wall-clock pass first; no material regression against v4.12.0 (release-blocking) | Linode, a separate instance or phase (owner: never benchmark locally) | PASS required; NOT RUN until approved |
| 9 | CP25-R inventory (`make cp25r-inventory`) | `inventory.py report --check`; complete only with no PENDING-OWNER reason seen | Linode (also local) | **FAILED today**: 19 PENDING-OWNER after the D-077 #1 reconciliation. After the owner rules on them: PASS, reported as "**Inventory complete, with named deferred capabilities**" |
| 10 | D-077 #1 refusal contracts | `post_cert_refusals.py` (+ `--negative-control`) in `test-cp25r` | Linode and local | PASS, a refusal contract: the 40 deferred-post-cert capabilities are counted as APPROVED DEFERRAL, not as passing functionality |
| 11 | host-deps (`test-runtime2-host-deps`) | read-limit unit, host_deps.py, freedom and ownership scripts, read_limit_controls, read_limit_contract_controls, read_limit.py | Linode (the device cases run there, never in the sandbox) | **FAILED today**: 6 unapproved refusals. PASS once the owner rules on host-handle-flow, statement:Command and type:length. Its 18 deferred cases count as APPROVED DEFERRAL |
| 12 | Async recursion bound (D-077 #2, new checkpoint) | no growth past the configured limit; controlled error; no exit 70, abort or kill; un-awaited tasks bounded; bounded recursion works; async() and thread(); mutants | Linode (memory) and local (small) | PASS required, with the CP23 qualification `qualified:legacy-await-chain-defect` closed. A depth cap is not reported as comprehensive accounting |
| 13 | Output order and std streams (D-077 #5, step 1) | the repair's own suite: pipe, file and 2>&1 bytes; threads and async; >64 KiB; handles; failure paths; mutants | Linode, local; **Windows identity code: CI only** | PASS required |
| 14 | `save()` special targets (D-073 #1; D-077 #5 step 3) | the repair's suite; scratch-only fixtures | Linode | PASS required |
| 15 | L6 `native::` (D-075 #2) | its suite and migration manifest | Linode, local | PASS required |
| 16 | Comparator sort | B04 comparator expectations; F6 (lambda-relative position) checked on GCC 15.2 | Linode only | PASS, or a named release blocker (F5 portability: release blocker, not a certification blocker; D-077 keeps it on the limitations list) |
| 17 | Linode-only verification items | B03 memory-c1 24 × 20; B12 valgrind; B13 PTY/SIGINT; B20 root-only; B22 cp17-unwind-mutants within the cap; B23/B24 reruns; U1 (the 190 gates after test-build-reuse now run on an untouched tree); test-build-reuse runtime under its 7,200 s cap (B21 item 6) | Linode | PASS each; any non-deterministic result is INCONSISTENT and blocks (zero-flakiness rule) |
| 18 | Handle-mutant Low (D-071 #3) | deterministic timeout, reliable unmodified run, causal attribution | Linode preflight | PASS (before *final release*; recommended now) |
| 19 | Bindings | Go, Node, Python, .NET (`test-bindings`, exclusive) | Linode | PASS |
| 20 | Root-only and unprivileged reruns | `--unprivileged-user tester` | Linode | SKIPPED-ENVIRONMENTAL resolved by the rerun, never PASS by itself |
| 21 | Regression suite (`nift-regression-suite` `runtime-2`) | `run-contract.sh <nift>` | Linode or local; CI `runtime2-regression-suite.yml` on the push | v4.12.0 environment failures are FAILED-BASELINE. **The 4 contract scripts with `cat`/`ls`/`touch` refuse after L6 Option B**: owner decision to migrate the suite or run those 4 with `NIFT_VM=0`. FAILED until decided |
| 22 | Cross-platform CI | checkpoint-10, v44, test-integrity, gate6ar-ffi, gate6b-bytes and **runtime2-cross-platform** (new, dispatch-only; platform-subsets.md) | CI only, after an approved push and dispatch | NOT RUN until approved. Windows and macOS are certified for the documented subset only. **The corpus differential (`vm_parity`), `host_deps` and read-limit enforcement (`read_limit.py`, its controls) are certified on Linux only, and on macOS only where the subset includes them** (vm_parity and host_deps yes, read_limit no): a headline limitation of the Windows and macOS claims. A subset entry that skips part of itself is PARTIAL, not PASS |
| 23 | Fuzzing | `nightly-deep.yml` (dispatch): parser fuzz with sanitizers | CI | owner decision. Recommended once on the frozen tip; NOT RUN otherwise (tier 4) |
| 24 | FreeBSD | — | — | **Out of the first certification** (D-077 #4): a separate qualification after the baseline. Reported as "not certified in this baseline" |

## 3. Classifications and how they are reported

| Report class (D-076) | From certify | Meaning |
|---|---|---|
| **PASS** | PASSED | passed in the campaign; FLAKY is never PASS (zero-flakiness) |
| **FAILED** | FAILED-NEW; FAILED-BASELINE, reported with its baseline | a failure; a baseline failure is pre-existing but still not a pass |
| **NOT RUN** | NOT-RUN; BLOCKED (missing oracle, toolchain, harness or comparator); UNVERIFIED; SKIPPED-ENVIRONMENTAL until the rerun resolves it | no result; never a pass |
| **INCONSISTENT** | a gate whose first pass and classification reruns disagree, an IDENTITY-MISMATCH, or EVIDENCE-LOST | blocks until explained (B23/B24) |
| **APPROVED DEFERRAL** | refusal-contract gates of capabilities the owner deferred (deferred-named: D-065, D-067 #2; deferred-post-cert: D-077 #1; host-deps' 18 deferred cases: D-077 #3) | the capability is refused in the approved manner. It is **not** counted as working; the report lists it under "named deferred capabilities" |

**The final certification report keeps four inventory classes apart** (D-077 #1):
- **supported and passing**;
- **approved semantic difference**;
- **named deferred capability**;
- **unresolved defect**.

It also lists, on its own line, the `implement` rows: approved and not implemented, so refused. The report never
says "all supported functionality passes". Its inventory line reads "Inventory complete, with named deferred
capabilities".

**Refusal-contract gates (proposal for review condition C5; owner to confirm).** certify has no APPROVED DEFERRAL
class. A gate whose only deferred content is refusal contracts classifies PASSED: host-deps once its 6 unapproved
refusals are resolved, and `test-cp25r`'s `post_cert_refusals.py`. The report therefore:
- keeps the gate's PASS;
- separately counts each deferred capability as APPROVED DEFERRAL, from the harness's own lines:
  - read_limit.py's `DEFERRED capabilities, checked only as VM refusal contracts …` (per reason);
  - `post_cert_refusals.py`'s count;
  - the inventory's deferred-named and deferred-post-cert lists;
- never adds a deferred capability to the supported or passing totals.

## 4. Known limitations to carry into the report

These come from known-limitations.md, the readiness report and the review of this preparation:
- **Platform scope (review L4).** The VM-vs-legacy corpus differential (`vm_parity`), `host_deps` and read-limit
  enforcement are certified on Linux only, and on macOS only where the platform subset includes them. Windows
  excludes all three.
- The async self-recursion qualification stays until gate 12 passes.
- The trace order (D-072 #2, P2).
- Heap-budget stops are shown as "NIFT_VM internal failure" (P2).
- `...name` in the name scan (latent).
- Comparator sort is GCC 15.2-only (F5, release blocker).
- Fabricated lambda-relative positions (F6, release blocker, verified on GCC 15.2).
- Session-only refusals (D4/m3).
- `--json` function-tag leakage; the `atomic.store` text; CP16 `fs_symlink` ordering; gen1 differences.
- The cross-platform export lists.

Plus every deferred-named and deferred-post-cert capability, and the 19 open inventory reasons and 6 host-deps
refusals, until the owner rules on them.

## 5. Cost and duration (planning estimate, not authorised)

| Run | Estimate |
|---|---|
| One full B21 campaign, g6-dedicated-4 | about 14.5 h (range 12-18 h); about 16-18 billed h × about US$0.108 ≈ **US$1.75-2.00**; worst case to the 24 h TTL is about US$2.60 |
| Website benchmark vs v4.12.0 | about 2-4 h ≈ US$0.25-0.45 |
| One rerun after a fix | about US$2 |

The budget remaining at the time of the readiness report was US$8.87. The cb35 controls stay: the 24 h watchdog, a
per-instance cap of US$3, PRIOR_USD, and deletion on any setup or build failure.

## 6. Order of approvals (each separate)

1. The owner rulings needed for gates 9, 11 and 21 (section 2). Also the review conditions:
   - C2: D-077 #5 covering the four judgement-call inventory rows;
   - C3: open_bytes() and read() deferred by name;
   - C5: the refusal-contract reporting rule in section 3.
2. The Linode launch for the campaign.
3. The Linode launch for the benchmark.
4. Integration into local `runtime-2` (a fast-forward from `1512579f`, unless a merge commit is wanted).
5. The push of `runtime-2`, which triggers `runtime2-regression-suite.yml`.
6. Dispatch of the cross-platform workflows on `runtime-2`. `runtime2-cross-platform.yml` exists only on that
   branch, so it is dispatched by ref; the action SHAs are pinned first.
7. The certification tag `runtime-2-certified-<name>` (immutable).
8. Release, which is a separate decision from certification.

## Record: test-v46-relative-imports fingerprint extractor (D-092 #1, owner 2026-11-14)

**Why it changed.** `scripts/certify_fingerprints.py` matched only `CERTIFY-ERR … cmd=` lines for this gate:
- The C19/C26 record was an abort: the first fixture's fn-flags refusal ended the script.
- C94, C97 and C104 changed the gate's output (`eeb7e819` fixtures use `fn(...)`, `62c198e0` gates accept only the
  approved refusals, `cf833f5a` no touched smoke aborts silently). Since then every fixture runs, and the smoke
  reports the failing case as
  `FAIL module-private async callables: refused with import-collision (inventory status implement)`, plus the
  umbrella FAIL.
- The old extractor found no case in that log, so it gave no fingerprint, and certify classed the known failure as
  FAILED-NEW (reported in the refreshed record, C141b `36329790`, SOURCES.tsv).

**What the extractor matches now** (this gate only):
- The refused-case line, matched whole. Its case, reason and status are the case ID. The log carries no diagnostic
  position, so none is matched.
- Any other FAIL line, as a case of its own.

`import-collision` is in no general list, and no other gate's Spec changed. The C19/C26 record
(tests/runtime2/certify-fingerprints/test-v46-relative-imports.txt) is unchanged and still extracts its abort case
as before.

**Controls** (in certify_fingerprints_selftest.py, on the refreshed log embedded as a fixture, sha256 `80dbfc84…`).
Each control fails under its own mutated extractor:
1. **The known failure is recognised.** It is found in the fingerprint and is stable across roots and TMPDIRs.
   Mutant: the old CERTIFY-ERR-only Spec.
2. **An unrelated reason or case is rejected as new.** Mutant: the reason and status are dropped from the case ID.
3. **A changed status or message, or an extra failure, does not match.** Mutant: no generic FAIL capture.

**Effect on classification.**
- When a regression baseline's log shows the identical failure, the gate is now FAILED-BASELINE.
- When the baseline fails as the C26 record did (the old abort), it is FAILED-NEW. That is the honest result.

## Record: scripting comparison tooling, v4.12.0 versus Runtime 2.0 (D-097, owner 2026-11-14)

This is tooling, not a release gate. The mandatory comparison is still the separate website-build benchmark against
v4.12.0 (gate 8). The scripting comparison becomes a finding only if it reveals a genuine regression in a supported
workload.

**The defect.**
- `benchmarks/v43_scripting_perf.py` (CP91) runs scripts as `nift run <script>` and the shell as `nift sh`.
- Both v4.12.0 (sha256 `7dd42dce…a6fb`) and the candidate (`e48fd3bf…18f1`) reject both commands with
  `unknown command 'run'` / `'sh'`, exit 1. So **every** row of that benchmark timed an error exit, the shell row
  included.
- It also ran the scripts from the caller's directory, so the relative-path workloads did not find their files.
- The original is kept **byte-identical**.

**The corrected copy: `benchmarks/v412_scripting_compare.py`.**
- **Invocation.** Scripts run as `nift <script>`, with the script's directory as the working directory. The shell row
  pipes the same 500 statements into plain `nift`, which is the shell; `nift run <script>` is not used or restored.
- **Workloads.** The same workloads, sources, inputs, sizes and operation counts, and 15 samples with no warmup, as in
  the original.
- **Validity oracle**, applied before timing and to every sample:
  - exit status 0;
  - the exact stdout;
  - empty stderr;
  - the exact files and sizes in a work directory recreated before every sample, with HOME kept separate;
  - the operation count in the source.

  A failing workload is FAILED with its reason and **has no timing**.
- **Binary check.** `--expect-sha256` and a before/after hash discard a run (exit 3) if the binary changes during it.
- **Categories, reported separately and never aggregated:**
  - **identical-source:** the same program text on both versions;
  - **equivalent-operation:** `fs_10_touch_open`, the one workload whose source differs, `touch(...)` under
    `--version-profile v412` and `native::touch(...)` under `rt2`. The profile is a flag, not version detection.
    `--equivalence` proves this one first: the same exit, stdout, stderr and ten 0-byte files on both binaries (PASS).
- **Selftest:** `make test-v412-scripting-compare-selftest` (tests/runtime2/v412_scripting_compare_selftest.py).
  - **Negative controls**, each with no timing: a `nift run` profile; a `nift sh` profile; an unqualified `touch(`
    under rt2; a wrong expected output; a missing side effect.
  - **Positive controls:** both profiles, and the equivalence proof.
  - **Mutant:** an oracle that accepts everything makes the selftest fail (shown).

**Correctness results** (validate-only, in the prep sandbox; no timing is evidence):

| Workload | v4.12.0 | Runtime 2.0 candidate |
|---|---|---|
| run_startup_empty, statements_2k, named_fn_100_calls, lambda_100_calls, import_once, repl_500_statements, fs_10_touch_open | PASS | PASS |
| `stream_read_5k_lines` | **FAILED**: "cannot change binding type: line" | **FAILED**: refused, `context:assignment-error` |
| `read_val_500` | **FAILED**: "cannot change binding type: val" | **FAILED**: refused, `host-handle-flow` (finding F-HD1) |

Two original workloads are invalid programs as written, on both versions: `line := i.read_line()` and later
`line = null`. They stay identical and are reported FAILED, with no timing. Correcting their sources would make them
different workloads, so it needs an explicit decision.

**Owner confirmation (D-098, 2026-11-14).** 42eda4f0 is approved as built:
- the shell row pipes the same 500 statements into plain `nift`;
- `nift sh` and `nift run` are obsolete and stay errors, and the `nift sh` negative control is kept;
- `stream_read_5k_lines` and `read_val_500` stay FAILED with no timing on both versions, and are not rewritten without separate approval;
- the seven valid workloads are comparable under the two categories.

**Historical numbers.** The v4.3 figures in docs/handover/V4.3-LANGUAGE-CAMPAIGN.md (CP91) are kept, but they are
**not comparable**: their execution was never validated, and on the evidence above their `nift run` and `nift sh`
rows measured error exits. Wall-clock measurement of the corrected tool happens on Linode only, with approval.

