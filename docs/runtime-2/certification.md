# Checkpoint certification (D-010)

## Tiers

| Tier | When | Command |
| --- | --- | --- |
| 1. Implementation | while editing | the affected suites: `python3 scripts/certify.py --targets <t>...`, or `--quick` (runtime and Runtime 2.0 smoke set), plus focused rt-diff runs |
| 2. Checkpoint | before committing a checkpoint | `make certify` (full, isolated, classified), regression suite (`nift-regression-suite` `runtime-2`), rt-diff gate; sanitizers and performance gates where the checkpoint requires them |
| 3. Post-push | after pushing to `runtime-2` | CI: `checkpoint-10-cross-platform`, `v44-cross-platform`, `test-integrity`, `runtime2-regression-suite` (Linux, macOS, Windows; toolchains unavailable locally) |
| 4. Milestone | major architectural milestones | broad differential campaigns, fuzzing, full benchmark suites |

The full matrix is not re-run after every small edit; tier 1 is enough until a
checkpoint is ready.

## `make certify`

`make certify` runs `scripts/certify.py` over `certify-targets` (= `test-all`
plus every suite the CI certification workflows run). It:

1. builds every non-test prerequisite once (`make -k`), so concurrent targets do
   not race to build shared objects;
2. runs parallel-safe targets concurrently (`CERTIFY_JOBS`, default `nproc`)
   and **exclusive** targets serially afterwards;
3. re-runs every failure alone, then classifies it;
4. writes `.build/certify/<target>.log` and `.build/certify/summary.{txt,json}`.

`tests/runtime2/certification_guard.sh` (part of `make test`) keeps
`certify-targets` complete: it fails if any `make` target, or any test/script a
workflow launches directly (`bash tests/…`, `python scripts/…`, `./tests/…`),
is not reached by a `certify-targets` recipe. The only CI-only script is
`scripts/compare_checkpoint10.py` (compares three runners' outputs).

## Classifications

| Status | Meaning | Counts as pass? |
| --- | --- | --- |
| PASS | passed in the certification run | yes |
| FLAKY | failed in the concurrent run, passed alone (load-sensitive; usually a wall-clock guard) | no — reported |
| SKIPPED-ENVIRONMENTAL | failed here only because of the environment, shown by re-running: e.g. fails as root but passes as the unprivileged user (`--unprivileged-user`, default `tester`; without one the failure stays FAIL) | no |
| UNVERIFIED | a dependency or platform is unavailable, detected from the environment and the log: tool not installed (e.g. `dotnet`), Go toolchain download blocked, `/usr/bin/time` missing, sibling `nift-packages` checkout missing | no — CI covers it |
| BASELINE-FAILURE | fails the same way on the v4.12.0 baseline tree (`../baseline-src`, built from `ce8cd36`) under the same conditions | no — pre-existing |
| FAIL | anything else | no; `make certify` exits 1 |

Classification never relies on a target's name alone. `--strict` also fails on
anything that is not PASS or FLAKY.

## Exclusive targets

From the isolation audit (`scripts/certify.py --audit report.json`, run in a
disposable copy: every target run serially, recording every file it changed in
the checkout and any new TMPDIR entry):

| Target | Why it runs alone |
| --- | --- |
| `test-build-boundary-nondestructive` | snapshots the whole checkout before/after; a concurrent writer is reported as a modification |
| `test-build-boundary` | builds from a clean copy of the checkout |
| `test-cp18-bytes` | rebuilds `src/embed/*.o` in the source tree |
| `test-cp21-bytes` | rebuilds `libnift_c.a` |
| `test-v45-embed-staged-consumer` | restages `dist/embed-prefix` |
| `test-go-binding`, `test-csharp-binding` | restage `dist/embed-prefix` and build binding products |
| `test-node-binding`, `test-python-binding` | build the binding native modules |
| `test-minify` | sub-make writing `minifypp/.build` |

Any target whose recipe runs a sub-make is also treated as exclusive
automatically.

### Load-sensitive measurements

Three kinds of failure are kept apart: **correctness regressions** (always
block), **performance regressions** (established by controlled measurement,
e.g. Callgrind instruction counts) and **load-sensitive measurements** —
wall-clock guards whose result depends on what else the machine is doing.
These targets keep their thresholds unchanged and run exclusively, with no
competing target:

| Target | Wall-clock guard |
| --- | --- |
| `test-v44-language-foundation` | `v44_perf_scaling_smoke.sh`: doubling n must not quadruple time (array push, map, set) |
| `test-v48-map-set-scaling` | `v48_map_set_scaling_guard.sh` |
| `test-scripting-perf` | `scripting_perf_guard.sh` budgets |
| `test-runtime2-process-capture` | background descendant must not block (< 280 ms) |

A failure under exclusive execution is classified **FAIL** even if the re-run
passes (the re-run result is recorded in the reason). A failure seen only in a
concurrent context — `make -j test-all`, or a CI runner shared with other jobs
— is recorded as a load-sensitive observation, not as resolved by a later pass.

**CI runs them exclusively too.** `NIFT_DEFER_LOAD_SENSITIVE=1` makes the
recipes of `test-v44-language-foundation` (its `v44_perf_scaling_smoke.sh` line
only), `test-v48-map-set-scaling` and `test-runtime2-process-capture` print
`deferred (load-sensitive, run exclusively by make test-load-sensitive): …`
instead of running the wall-clock script. Test integrity guards runs
`NIFT_DEFER_LOAD_SENSITIVE=1 make -j2 test-all` and then, as a separate step
with nothing else running in the job, `make test-load-sensitive`, which runs
exactly `v44_perf_scaling_smoke.sh`, `v48_map_set_scaling_guard.sh`,
`runtime2/process_capture.sh` and `scripting_perf_guard.sh` serially. Without
the variable (local `make test` / `make test-all`) nothing changes: the guards
run in their usual targets. `make certify` removes the variable from the
environment, keeps all of these targets exclusive, and also reaches
`test-load-sensitive` (via `certify-targets`, so the CI step stays covered by
`certification_guard.sh`). Thresholds are unchanged.

Open observation: `test-v44-language-foundation` failed in CI at `974dcb3`
(`make -j2 test-all`; map n=40k 85 ms, n=80k 450 ms, ratio 5.3 > 3.5) and in one
concurrent local certify run (0.1 s, log not retained), and passed alone and in
CI at `600fe99`. No threshold changed. Controlled measurement (2026-10-18,
this host, `nift` at `600fe99`): the guard's map programs execute 392.6M and
783.0M instructions (Callgrind; ratio 1.994) with peak RSS 30.7 MB and 55.1 MB,
so map insertion is linear in work; eight runs alone gave wall-clock ratios
1.8–3.4 against the 3.5 bound. The excess over 2× is wall-clock variance
(allocation, page faults, scheduling), and the margin is thin even on an idle
machine. Still classified load-sensitive, not resolved: the guard keeps its
bound, runs exclusively under `make certify`, and a Callgrind instruction-ratio
companion is the candidate controlled measurement if it fails again.

Second CI observation: at `f3208f4` (run 39100545658, job "Production binding
gates (Go, C#, Node, Python) on Linux") the same guard failed inside
`make -j2 test-all`: map n=40k 52 ms, n=80k 232 ms, ratio 4.46 > 3.5. Both CI
failures (`974dcb3`, `f3208f4`) happened in a concurrent context and are
recorded as load-sensitive observations, **not resolved** — neither by later
passing runs nor by the CI restructuring above, which only removes the
competing workload from the measurement.

Every target runs with a **private TMPDIR** (removed afterwards). Many C++
tests create fixed names under the temp directory
(`temp_directory_path() / "nift-host-seam-pg"`, `nift-c-abi-pagination`,
`nift-engine-*`, …); without a private TMPDIR they collide with concurrent
targets, with other checkouts running the same tests, and with a different
user's leftovers (a non-root run failed `test-c-abi` and `test-host-seam` with
"Permission denied" on root-owned `/tmp/nift-*` directories).

Audit results (2026-10-17, 159 targets): only the targets above wrote outside
`.build/`; the only output written by two targets was `dist/embed-prefix`
(`test-go-binding`, `test-v45-embed-staged-consumer`); no target left a
fixed-name file in TMPDIR. Three tests that used fixed `/tmp` names
(`v43_cp137_cp140_frontmatter.sh`, `v43_cp141_cp163_content_model.sh`,
`v44_ast_constant_fold_smoke.sh`) now use private `mktemp` directories; they
broke whenever a root-owned file with the same name was left behind.

## What CI covers that local runs cannot

UNVERIFIED items are validated by the post-push tier: the binding gates (Go,
C#, Node, Python) in `test-integrity`; macOS and Windows in
`checkpoint-10-cross-platform` and `v44-cross-platform`; package dogfood with
the pinned `nift-packages` checkouts in the regression-suite workflow. A
platform is claimed only after its CI job passes on the pushed commit.

## Open timing observations (for the next controlled benchmark window)

- `benchmarks/typed_content_scaling.py` (inside `test-v43-language`) is a single
  wall-clock ratio of ~0.1 s builds. On the shared 2-vCPU host the unchanged
  `runtime-2` binary measured 3.6×–12.8× across five runs (2026-10-19), so a
  single run cannot judge regressions there. Instruction counts for site-DX
  (@1e4cd54) and `runtime-2` (@e9198eb) were both 3.93× for 4× pages (+0.6 %
  instructions), i.e. linear. Instruction counts cannot exclude filesystem or
  scheduling regressions, so rerun the wall-clock test in the next preflight-
  passing benchmark window and record the result here. Until then the target
  is not counted as a pass in local certification.

## Where certification time goes (measured 2026-10-19, `feature-serve` @c285fa4)

172 targets, 64 min of summed target time (34 min of it exclusive), run at
`-j2`; wall time about 50 min for targets plus the prebuild (a full rebuild,
since rebases touch every source mtime), roughly 80 min end to end on an idle
host. Longer runs were lock waits, not certify itself.

| Target | Minutes | Share |
|---|---:|---:|
| test-build-boundary-nondestructive (exclusive) | 21.3 | 33.5 % |
| test-runtime2-rt-diff-selftest | 11.1 | 17.5 % |
| test-rt-vm | 8.4 | 13.3 % |
| test-build-boundary (exclusive) | 4.2 | 6.6 % |
| test-python-binding (exclusive) | 3.6 | 5.6 % |
| test-node-binding (exclusive) | 3.4 | 5.4 % |
| everything else (166 targets) | 12.2 | 18.6 % |

Candidates (not applied; none skips or weakens a gate): cache object files
across rebases (ccache is not installed here), check whether
`test-build-boundary-nondestructive` and `test-build-boundary` repeat the same
builds, and whether `test-rt-vm` and `test-rt-vm-mutants` share their unmutated
runs.

## Follow-up: site-DX checks under extreme CPU starvation (recorded 2026-10-19)

With `test-site-dx` at `nice 19` against four CPU burners, three checks outside
D1 failed: `N3 not linear` (wall-clock), `watch 6 (3 → 4)` and
`recovery: marker left`. Not investigated yet. For the marker, establish whether
it is temporary (cleaned up once processing finishes) or persists after the
watcher becomes idle or exits — the latter is a correctness defect even if it
only appears under extreme contention.

## Remote Linux certification validated (CP14 @6c26404, 2026-10-19)

The same commit was certified locally (shared 2-vCPU host) and on a dedicated
`g6-dedicated-4` Linode (4 vCPU AMD EPYC 7642, Ubuntu 24.04, fresh setup from
`runtime-2` + a commit bundle; evidence in the scratch `remote-cert/` folder and
the user's `.claude-linode-runs/cert-cp14/`).

| | Local | Remote |
|---|---|---|
| Targets | 173 | 173 |
| Raw per-target outcomes | — | identical on all 173 |
| Classification | FAIL 0, FLAKY 0, PASS 168; UNVERIFIED 2 (Go, .NET); SKIPPED-ENVIRONMENTAL 3 (root-only) | FAIL 0, FLAKY 0, PASS 168; BASELINE-FAILURE 2 (Go, .NET); SKIPPED-ENVIRONMENTAL 3 (same) |
| Queue wait for the heavy lock | ~70 min | none |
| `make certify` wall time | ~82 min | ~70 min (+3 min setup) |
| Summed target time | 95 min | 90 min |
| Cost | — | ~US$0.22 |

The Go and C# bindings fail in both environments for harness reasons, not Nift
(they also fail on the v4.12 baseline remotely): the remote job's job-wide
`ulimit -f` killed `dotnet build`, and Ubuntu's Go 1.22 cannot fetch the Go 1.26
toolchain the module requires. Fix for the next remote run: no job-wide file-size
limit around `make certify` (its targets carry their own limits), Go from the
official tarball, .NET from Microsoft's feed. Neither environment counts them as
passes; CI covers them.

Conclusion (D-033): remote Linux certification is equivalent; it becomes the
default for subsequent checkpoints. Wall time is dominated by serial exclusive
targets (`test-build-boundary-nondestructive` alone ~21 min), so more cores help
little; the gain is no queue and no contention with development work.

## Closed defect: `-0` in JSON-valid object literals (found 2026-10-20, closed by CP15 @9cf5cd0)

Found by the CP15 integration owner while building the W9 differential set; not
caught by any certification of CP12 phase A or CP14 phase A.

| Program | Legacy | VM (`NIFT_VM=require`, since 4b2fc909) |
|---|---|---|
| `o := {"b": -0}` then `print(o.stringify())` | `{"b":-0}` | `{"b":0}` |
| `[{"b": -0}]`, `{"a": {"b": -0}, "c": .5}` | keeps `-0` | `0` |

Both exit 0, so the difference is silent. Legacy parses a JSON-valid `{...}` as
JSON (keeping `-0`); the VM evaluated it as a Nift literal. `0.0` and `-0.0` were
already refused (`number-literal`). The shared differential generators never
produced `-0` inside an object literal, which is why rt-diff and the adversarial
suite stayed clean.

Disposition: fixed by CP15's W9 JSON path (option A); no backport to the CP13
phase A candidate, which is mid-certification. CP15's evidence must show the
4b2fc909 failure and the fix on the same cases, extend the probes to other `-0`
shapes, and add `-0` and similar value-preservation edges to the supported
generator. Closed only when CP15 integrates.

**Closed 2026-10-20:** CP15 integrated on its certified commit; its evidence
shows the 4b2fc909 failure and the fix on the same cases, with `-0` probes and
generator edges added (`checkpoints/cp15-builtins-design.md`).

## Remote environment: Go and .NET were never actually installed (corrects the CP14 record)

The CP14 section above says the remote script was fixed to install Go from the
official tarball and .NET from Microsoft's feed. That fix was never exercised to
success: on the CP13 phase A run (2026-10-20) `dl.google.com` returned 404 for
every Go tarball from ap-southeast, and the script installed the .NET 8 SDK while
the bindings target `net10.0`. Both binding targets were therefore unverified
remotely for CP14 as well. `cert.sh` now installs Go from the
`proxy.golang.org` toolchain zip and .NET from channel 10.0, with a loud warning
if either is missing. The issue counts as resolved only after a fresh instance
passes both binding targets with no manual steps.

## CP13 phase A remote run @0b879df (2026-10-20): not certified

Linode g6-dedicated-2, ap-southeast; instance deleted after evidence retrieval
(confirmed by API); spend to date about US$0.52. Summary line: FAIL 0,
BASELINE-FAILURE 1, SKIPPED-ENVIRONMENTAL 2, FLAKY 2, PASS 168. **That summary is
wrong for one target**:

- `test-v43-language` was classified BASELINE-FAILURE. As root it fails at a
  root-only step (`v43_cp112_filevalue_wall_smoke.sh`), and so does the v4.12.0
  baseline. The unprivileged run got further and failed on a real regression:
  `print(null?["x"])` → "unknown value or malformed expression" (expected
  `null`); the runtime-2 binary prints `null`. The CP13 prevalidator does not
  know optional indexing. The local certify of fb9112d had the same masking.
- Harness fix (branch `certify-baseline-fix`, 2dcce918, to be certified with the
  next CP13 run): the baseline runs as the same user as the candidate log it is
  compared with, and BASELINE-FAILURE requires the same failing step; otherwise
  FAIL. Regression check added to `certification_guard.sh`.
- `test-go-binding` and `test-csharp-binding` show FLAKY only because the
  toolchains were installed by hand during the run; their re-runs alone passed
  (Go: `ok nift.dev/embed/v4/cmd/embed-harness`; C#: 29 passed, 0 failed) on the
  unchanged candidate. Not counted as a fresh-instance provisioning fix.
- Remote provisioning now has a mandatory preflight (exact toolchain versions;
  certify does not start if any is missing; `GOTOOLCHAIN=local`) and a
  per-instance SSH host key installed by cloud-init and pinned before first
  contact. Both are verified only when a fresh instance passes with no manual
  steps.

## CP13 phase A certified on Linode @53bc509a (2026-10-20); fresh-instance provisioning verified

Candidate: `runtime-2` 25bee4bb + certify-harness fix (2 commits) + CP13 phase A
through 1d533533, rebased with no source change (the only non-docs difference
from 1d533533 is the harness). Fresh g6-dedicated-2 instance, ap-southeast;
deleted after evidence retrieval (confirmed by API); campaign spend about
US$0.68.

- **Result:** FAIL 0, BASELINE-FAILURE 0, UNVERIFIED 0, FLAKY 0, PASS 170;
  SKIPPED-ENVIRONMENTAL 3 (`test-v43-language`, `test-unreadable-source`,
  `test-dependency-status`: fail only as root, each passes as `tester`).
- **Harness fix exercised:** `test-v43-language` now reaches the unprivileged run
  and passes there (the masking case from @0b879df); its certification guard
  self-test (14 cases) ran as part of certify.
- **Provisioning verified with no manual steps:** SSH host key pinned at creation
  matched (StrictHostKeyChecking=yes); preflight passed all 16 requirements
  (Go 1.27.2 from the proxy toolchain zip, .NET SDK 10.0.402, Bun 1.4.2, Node 22,
  …); `test-go-binding` passed in 27.5 s and `test-csharp-binding` in 7.8 s in the
  main run. The Go/.NET environment issue recorded above is closed.
- Evidence: `.claude-linode-runs/cp13a2/out.tgz` on the owner's machine
  (certify log, per-target logs, preflight, environment).

## Open defects from the independent memory review (2026-10-20)

Raised by DeepSeek's code review; reproduced by a bounded investigation on a
Linode worker (`memcheck` job, tip 1105409d; follow-up `memcheck2` running).

- **C1 — abort on memory exhaustion (VM).** `destroy_heap_object` grows a
  `std::vector` worklist inside noexcept destruction; under `ulimit -v` the VM
  aborts (SIGABRT, "terminate called after throwing … std::bad_alloc") at 7 of 11
  limits (10 of 12 with the heap budget raised), including during teardown after
  a correct "memory budget exceeded". Legacy fails cleanly (exit 1) at every
  point. **Blocks integration of value-layer changes until fixed and verified**
  (allocation-free cleanup, original error preserved, cause-and-effect evidence,
  deep nesting, ASan/UBSan, `ulimit -v` matrix).
- **C3 — heap-budget bypass in `join()`/`replace()` (VM natives).** Results are
  built without a budget check: under a 64 MiB budget ~1 GB was allocated, ending
  in bad_alloc (exit 70) instead of `limit.exceeded`. **Resource-budget blocker**:
  check and reserve before allocating, overflow-checked size arithmetic.
- **C2 — atom table outside the heap budget (confirmed).** Runtime interning
  (`from_entries` keys) is never counted or trimmed: 3M distinct keys under a
  16 MiB budget reached 511 MB RSS without the budget firing. Medium now (one-shot
  CLI runs), high once VM contexts are long-lived. Count interned bytes (L7 2.1);
  table lifetime/trimming → CP23 or the first long-lived-VM checkpoint.

Final verification (memcheck2, 7fab21a6): **C1 cause and effect** — 18 of 24
`ulimit -v`/budget points SIGABRT on the unchanged binary, 0 of 24 with only
`value.cpp` swapped for the hybrid worklist (all exit 70 cleanly); value suites
and vm_unit pass with the fix; push −1.5 %. **C3** also covers latent
`stringify`/`flatten` amplification once aliasing is allowed; `print` peaks at
1.54× (one output-buffer copy). The L7 side table (cd56fc1f) closes per-object
saturation under-counting but not C3; it adds one allocation inside `noexcept`
(`heap_retally`), to be fixed with C1.
- Related: L7 step 2.1 proposes shrinking the per-object accounting counter from
  1 GiB to 512 MiB; integration blocked until no silent saturation at either limit
  is shown (original range preferred).

Owner: the L7 agent (single owner of the VM value layer), with C1/C3 commits and
evidence kept separately attributable from L7's own changes.

## CP15 phase A certified on Linode @9cf5cd0 (2026-10-20)

Instance g6-dedicated-2, fresh Ubuntu 24.04, mandatory toolchain preflight
passed (Go from the proxy toolchain zip, .NET 10, `GOTOOLCHAIN=local`). Base
`5e4e6144`, tip `9cf5cd0f`; `make certify` exit 0 after 6,333 s:

    certify @ 9cf5cd0: FAIL 0, BASELINE-FAILURE 0, UNVERIFIED 0, SKIPPED-ENVIRONMENTAL 3, FLAKY 0, PASS 170

SKIPPED-ENVIRONMENTAL (fail only as root, pass as unprivileged `tester`, same
three as CP13 @53bc509a): `test-dependency-status`, `test-unreadable-source`,
`test-v43-language`. Linux only; cross-platform verification is the next CI
milestone. Integration: rebased onto `runtime-2` (docs-only changes since
the base); the non-docs diff against `9cf5cd0f` is empty, so the
certification applies to the integrated tree. Evidence:
`.claude-linode-runs/cp15/out.tgz` (certify log and per-target logs).
Instance destroyed after the evidence was copied (confirmed by the API).

## CP17 certified on Linode @90252d5 (2026-10-20)

Base `a8efbd76` (CP15 integrated), tip `90252d5e`; `make certify` exit 0:

    certify @ 90252d5: FAIL 0, BASELINE-FAILURE 0, UNVERIFIED 0, SKIPPED-ENVIRONMENTAL 3, FLAKY 0, PASS 170

SKIPPED-ENVIRONMENTAL: the same three root-only targets as CP13/CP15, each
passing as unprivileged `tester`. An earlier run of the pre-rebase commit
`6fea85ec` (base `975dbb1a`, without CP15) gave the same summary; it is not the
integrated tree and is kept only as supporting evidence. Integration: rebased
onto `runtime-2` (docs-only commits since `a8efbd76`); the non-docs tree equals
`90252d5e`. Linux only. Evidence: `.claude-linode-runs/cp17r3/out.tgz`,
`.claude-linode-runs/cp17cert/out.tgz` (pre-rebase run). Instances destroyed.

## feature-host-deps: first run not certified @8b37d3f; certified @847e65d (2026-10-20)

First run (base `975dbb1a`, tip `8b37d3f7`): `FAIL 1, PASS 170` (plus the three
root-only targets). `test-consumer-snapshots` failed deterministically (also on
rerun and as `tester`; baseline passes): `declared_filevalue_hook` expects a
post-build hook that reads a declared dependency changed during the build to
fail with "observed at multiple versions", and the build succeeded. Cause: the
D-028 rewrite of `file().open` replaced the unconditional
`host_.observe_dependency(path, data)` with `observe_read()`, which returns
early when the parser is not observing (standalone hook scripts), so hook reads
stopped being reported. Fix: `e2e86749` (restore the host observation when not
observing; integrated as `c7971365`). Verified on the failing instance before
re-certification (27/27 contractual cases, `declared_filevalue_hook: true`).
The guard stays permanent: `test-consumer-snapshots` is part of `make test`.

Certified run: base `a8efbd76`, parent CP17 `90252d5e` (the certified CP17
commit), tip `847e65d9`:

    certify @ 847e65d: FAIL 0, BASELINE-FAILURE 0, UNVERIFIED 0, SKIPPED-ENVIRONMENTAL 3, FLAKY 0, PASS 171

Integration: rebased onto `runtime-2` @`d7161643`, whose non-docs tree equals
CP17's certified tree; the integrated non-docs tree equals `847e65d9`
(source, tests, fixtures and build files; the certify package snapshot
`nift-packages.tgz` b978a5a2… is unchanged). Linux only. Evidence:
`.claude-linode-runs/hostdeps/out.tgz` (failed run), `.claude-linode-runs/hostdeps4/out.tgz`.
Instances destroyed.

## Open VM defects: error wording on paths (found 2026-10-20, records20k stage 4)

Present on `runtime-2` since CP12/CP15 (reproduced on the CP15 binary); both
engines exit non-zero, but the VM's message differs from legacy's, so these are
real divergences, not approved differences. Found by the records20k owner while
probing the scalar path-argument lift; not caught by any certification because
no differential corpus produces a failing path inside these shapes.

| Program | Legacy | VM |
|---|---|---|
| `v := a[5].y` / `print(a[5].y)` (index out of range, then member; also `a[0][5].y`, `if (a[5].y.z == 1)`) | quotes `'a[5].y'` (whole chain) | quotes `'a[5]'` |
| `s.contains(o.zz)` (missing member in a method argument) | `contains: argument must be a string` | `value has no member: zz` |
| `print(a[j].k)`, `k` missing | `JSON value 'a[j]' has no member 'k'` | `value has no member: k` |

Reproducers: `scratchpad/integ/pathq/q2/`, `scratchpad/integ/pathp/method_arg_missing.f`,
`scratchpad/integ/pathr/pe_unchanged_method.f`. Disposition: fixed by the
records20k owner as separate, attributable commits with regression fixtures,
and the differential generators extended with failing-path shapes; closed only
when integrated and certified.

## feature-case-fastpath (DeepSeek) certified on Linode @33c5415 (2026-10-20)

Base `2d191e6b`, tip `33c5415a` (DeepSeek's two commits plus Claude's
integration commit):

    certify @ 33c5415: FAIL 0, BASELINE-FAILURE 0, UNVERIFIED 0, SKIPPED-ENVIRONMENTAL 3, FLAKY 0, PASS 173

Focused gates before certification (Linode worker job `case-gates1` @244cc396,
pre-rebase, identical code): built-ins, guards (35 workloads, 0 failures),
built-ins sanitizer, `test-rt-vm`, mutants, rt-lint, test-integrity, and the
negative control run from outside the repository (candidate passes; planted
wrong mapping fails 120 of 1,679 checks). Integration: rebased onto `runtime-2`
@`041e8efc` (docs-only since the base); non-docs tree equals `33c5415a`
(package snapshot b978a5a2… unchanged). Linux only. Evidence:
`.claude-linode-runs/casecert/out.tgz`, `.claude-linode-runs/results/case-gates1/`.

## Stack CP12B + L7 + loop-literal fix certified on Linode @01f18d2 (2026-10-20)

One stacked candidate, certified as a whole (the pass shows the three changes
coexist; it is not separate per-checkpoint certification). Base `6413f7a4`,
tip `01f18d29`:

    certify @ 01f18d2: FAIL 0, BASELINE-FAILURE 0, UNVERIFIED 0, SKIPPED-ENVIRONMENTAL 3, FLAKY 0, PASS 174

Contents, bottom up: CP12B phase B through `217ac21b` (B1 tests, missing
goldens for `cp11/fallback_fn_called_early`, golden-corpus completeness check
`test-runtime2-vm-corpus-complete`); L7 value layer (12 commits); loop-literal
fix (splitter escape fix for 55cb323c; literal and output completion restored
for 5fe38bd1, output completion only where an operand carries a literal
needing it; `tests/runtime2/loop-literals`, v4.6.0 authoritative); the two
legacy loop-interpolation goldens re-recorded from the fixed legacy; the
D-009#6 approvals removed (no longer a difference). g6-dedicated-2, `-j2`,
4361 s certify; package snapshot b978a5a2… unchanged. Local preflight
(`precert.sh`) on the same commit: PASS.

Not certified, in order: CP12B @0bea8f1a (missing goldens), L7 @0d62b316
(same), stack @f3ba494f (two legacy goldens pinned the v4.12 loop regression;
restarted), stack @ba657a0b (rt-diff self-test: D-009#6 approvals unused). All
three were detectable by cheap local checks before provisioning; see the
standing rule below. Integration: rebased onto `runtime-2` @`b37a223b`
(docs-only since the base); non-docs tree equals `01f18d29`. Linux only.
Evidence: `.claude-linode-runs/results/stack3/`.

## Standing rule: local preflight before certification (owner, 2026-10-21)

Before launching an expensive certification run, the certifier personally
establishes that the complete candidate, at its exact commit, passes every
applicable local gate and that the certification inputs (source, tests,
fixtures, build files, approval rules, package snapshot) are complete and
consistent. A subagent's report that its checkpoint passed is not enough. A
gate that cannot run locally is named before launch. The candidate is frozen
while certification runs; a defect found mid-run is collected with the rest
of the results and fixed once. `precert.sh` (fixture completeness, corpus
against the candidate's own binary, loop literals, rt-diff, rt-diff
self-test including unused approvals, `test-rt-vm`) is the minimum. Full
certification stays mandatory for executable changes; notes-only changes keep
the equivalence exception.

Timing correction to "Where certification time goes": in the @01f18d2 run the
longest targets were `test-rt-vm-mutants` 1840 s, then
`test-build-boundary-nondestructive` (exclusive, about 22 min) and
`test-runtime2-rt-diff-selftest` (about 11 min).

## records20k (stage 4) certified on Linode @527b692 (2026-10-20)

Base `5394557d`, tip `527b6924`:

    certify @ 527b692: FAIL 0, BASELINE-FAILURE 0, UNVERIFIED 0, SKIPPED-ENVIRONMENTAL 3, FLAKY 0, PASS 174

g6-dedicated-4, `-j4`, 4678 s certify (the 2-vCPU stack run took 4361 s:
four vCPUs did not shorten it, because the sequential `test-rt-vm-mutants`
(2068 s) and the exclusive `test-build-boundary-nondestructive` (1552 s) set
the critical path). Package snapshot b978a5a2… unchanged.

Preflight: all six `precert.sh` targets passed individually on 527b6924, but
the script did not exit successfully: it was edited in place mid-run (by the
certifier) and stopped with a syntax error after `test-rt-vm`, before its
unchanged-candidate check and closing line; HEAD and tree were verified
unchanged by hand. (precert.sh now runs from a private copy.)

records20k-specific evidence not covered by `make test`/certify: the agent's
local runs on 527b6924 passed `vm_records20k` (n = 5k/10k/20k on the VM,
byte-identical to legacy), `vm_records_coverage` (0 failures) and
`vm_path_wording` (44 on the VM, 16 refused). The worker job `stage4h` on the
pre-rebase tip fb6e265a did NOT run `vm_records20k` or the benchmark (job-script
path error); those two are not claimed from it. Benchmark figures remain the
stage4g ones (@fe20a9b0). Linux only. Evidence: `.claude-linode-runs/results/r20kcert/`.

