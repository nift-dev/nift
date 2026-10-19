# Runtime 2.0 delivery forecast (living)

_Last updated: 2026-10-20 06:10 AEDT. Updated when milestones land or blockers
emerge, not continuously. Authoritative per-checkpoint evidence stays in
`progress.md`; this file tracks state, dependencies, estimates and the forecast._

## States

Each checkpoint moves through: **implemented → reviewed → locally certified →
integrated (`runtime-2`) → cross-platform certified** (macOS + Windows CI at a
milestone). A checkpoint split into phases is complete only when every phase is.
Linux certification runs on a dedicated Linode (D-033; validated on CP14, see `certification.md`).

## Headline

- Critical path: **CP12 phase B → CP16/CP17 → CP24 → CP25**. CP25 (retire the
  legacy evaluator) needs near-complete VM coverage, so coverage — not built-ins —
  sets the pace.
- VM coverage (shared corpora): **101 / 786 (12.8 %)**, measured at CP12 phase A;
  104 / 789 with B1 (declarations after statements) on `runtime-2-cp12b-dev`.
- Inventory complete (2026-10-20, `checkpoints/cp12b-coverage-inventory.md` on the
  CP12B branch). Curated corpus (589): all of CP12B scope → 30.4 %; CP12B +
  CP13–CP16 → +142 scripts (only +40 without CP12B). Modules block 124, structs 37,
  declaration order 48; enums occur in no corpus. Chained calls on call results
  block 153 (VM subset gap). **About 180 curated scripts depend on features no
  checkpoint owns** (`for` 21, lambdas/functions as values 88, unscheduled
  built-ins 60, methods outside CP14/15 57, lambda-taking methods 38): as
  scheduled, CP25's zero-fallback gate was unreachable. **Decided (D-036): roadmap expanded (CP12C, CP14B, CP15B, natural owners for `cmd`/`mutex`/`ffi_*`); CP25 stays strict.** Modules approved (D-037); lexical scoping adopted (D-038).
- **Target (owner, 2026-10-19): complete the Runtime 2.0 implementation within
  seven days, with the following week as contingency.** A stretch target, not
  permission to weaken correctness or certification. Workstreams overlap (agents
  in parallel, Linode certification), so checkpoint estimates are not additive.
  My earlier 3–5 week figure assumed largely sequential checkpoints and is
  superseded; the forecast is revised from observed throughput (checkpoints
  integrated per day, certification queue time, refusal-reason reductions).
  Implementation complete ≠ certified legacy retirement ≠ cross-platform-certified
  release.
- Phase B prioritisation (owner): rank features by scripts unlocked (alone and in
  combination) **and** estimated effort, respecting dependencies; weight real
  package and website usage above synthetic corpora.

## Integration queue (2026-10-20 06:30 AEDT)

Whichever candidate is ready first certifies and integrates first; the other
rebases (owner, 2026-10-20). A ready checkpoint never waits for an earlier-
scheduled one.

| Candidate | State | Blocks |
|---|---|---|
| CP15 phase A (+ records20k after) | final gate run; then Linode | — |
| feature-host-deps | rebase + targeted re-verification (waited ~18 h) | CP12B legacy module side; CP16 legacy side |
| CP17 | adversarial review | CP16 swap |
| CP12B (B1, chains, globals below compiler, VM module loader) | gates on rebased branch; compiler hookup after CP15 | CP12C |
| CP16 | implementation; CP17 swap pending | CP20, CP24 |

Temporary scaffolding to remove: VM `import-reinitialised` refusal — removal
condition: legacy and VM both implement and verify initialise-once (D-041).

## Checkpoints

| Checkpoint | Implemented | Reviewed | Locally certified | Integrated | Cross-platform | Depends on | Remaining effort | Bottleneck risk | Notes |
|---|---|---|---|---|---|---|---|---|---|
| CP00–CP03, CP05, CP06, CP08 | ✓ | ✓ | ✓ | ✓ | ✓ | — | — | — | see `progress.md` |
| CP04 | — | — | — | — | — | CP00 | — | — | deferred (D-006) |
| CP07 | ✓ | ✓ | ✓ | ✓ | ✓ | CP00 | launch-cost gate | low | launch-cost gate deferred, not passed (D-011) |
| CP09–CP11 | ✓ | ✓ | ✓ | ✓ | ✓ (CI @c6fac06) | — | — | — | |
| **CP12 phase A** (engine selection, replay removed) | ✓ | ✓ (2 rounds) | ✓ @ebc2656 | ✓ bd1c8a9 | pending | CP11 | — | — | planted-defect gate permanent |
| **CP12 phase B** (globals, `GETDYN`, modules, structs, enums) | started 2026-10-19 (inventory) | | | | | CP12A | large | **high** | the coverage engine; agent assigned |
| **CP12 phase B chained calls** (`a.b().c()` on call results) | analysis in progress | | | | | CP12A | TBD (breakdown pending) | medium | 153 curated scripts; D-036 |
| **CP12 phase C** (functions as values, closures/lambdas, `for`) | | | | | | CP12B | large | **high** | new (D-036); 88 + 21 curated scripts |
| **CP13 phase A** (prevalidation, multi-line return, `runtime.*` codes, site-DX) | ✓ | ✓ | ✓ Linode @53bc509a (FAIL 0, PASS 170, 3 root-only) | ✓ 2026-10-20 | pending | CP12A | — | — | two failed runs fixed (4 targets; `?[` regression + harness masking) |
| CP13 phase B (VM `throw`/`try`, structured error records, JSON diagnostics, `eval` prevalidation) | | | | | | CP13A | medium | **medium–high** | corpus scripts waiting on `try`; affects unwinding/cleanup |
| **CP14 phase A** (F1/F2/F3/F6, 73 natives) | ✓ | ✓ | ✓ @6c26404 | ✓ 2026-10-19 | pending | CP12A | certification | low | coverage unchanged by itself (104/788) |
| **CP14 phase B** (F4 mutating ×9, F5 callbacks ×17: `map`, `sort_by`, `filter`…; all callback-taking collection methods, D-036) | design only | ✓ (design) | | | | CP12B place model (F4); native→compiled callbacks (F5) | medium | medium | `sort_by` 19, `map` 9 corpus refusals |
| CP15 built-ins B (phase A) | natives ✓, wiring written | ✓ (natives) | | | | CP14 | small–medium | low | time natives moved to CP16 |
| **CP15 phase B** (remaining pure built-ins and methods) | | | | | | CP15A, CP12C for callbacks | medium | medium | new (D-036); ~57 methods + pure built-ins |
| records20k coverage | designed | | | | | CP14 | small | low–medium | push of fresh values; loop-called fns returning objects |
| CP16 host ops | started (host layer) | design ✓ | | | | CP14, CP17, host-deps | large | **high** | D-028–D-031 decided |
| CP17 typed handles | started | | | | | (CP16 consumes it) | medium | medium | built before CP16's concrete handles |
| CP18 embedding (C ABI 1.4) | | | | | | CP13 | medium | low–medium | |
| CP19 shell front end | | | | | | CP12 | large | **high** | on hold until integration queue clears |
| CP20 spawn + pipelines | | | | | | CP19, CP07, CP16 | medium | medium | |
| CP21 interactive layer | | | | | | CP20 | medium–large | medium | |
| CP22 coroutines + scheduler | | | | | | CP12 | large | **very high** | |
| CP23 isolates, channels, cancellation | | | | | | CP22 | large | **high** | |
| CP24 template compiler | | | | | | CP16, CP02 | large | **high** | website output must stay byte-identical |
| CP25 retire legacy | | | | | | CP13–CP24 | very large | **very high** | needs fallback count zero |
| CP26 remove scaffolding | | | | | | CP25 | small–medium | low | |
| CP27 final campaign | | | | | | CP26 | medium | low | performance, cross-platform, report |

## Independent checkpoints

| Checkpoint | Implemented | Reviewed | Locally certified | Integrated | Cross-platform | Notes |
|---|---|---|---|---|---|---|
| feature-sha256 | ✓ | ✓ | ✓ | ✓ | ✓ | |
| feature-build-recovery | ✓ | ✓ | ✓ @4556896 | ✓ | pending | |
| feature-site-dx | ✓ | ✓ (2 rounds) | ✓ @1e4cd54 | ✓ | pending | |
| feature-serve | ✓ | ✓ | ✓ @c285fa4 | ✓ | pending (Windows code never compiled here) | |
| feature-host-deps | ✓ | ✓ round 1, fixes done | | | | needed before CP16's legacy-side work |
| language-cleanup | | | | | | consistency register L1–L40; after natives/values stabilise |
| string performance (D-032) | | | | | | after CP14 integrates |
| mtime checkpoint | | | | | | equal-tick rebuilds on multigrain kernels |

## Schedule (seven-day stretch; no gates weakened)

Owner's ranges: CP13–CP15 integrated 1–2 days; CP12B substantially complete 2–4;
CP16–CP18 3–5; CP19–CP21 3–6; CP22–CP23 3–7; CP24 2–4; CP25 2–5 after its
prerequisites.


| When | Target (owner's estimates, overlapping) |
|---|---|
| 2026-10-19 (before the allowance reset) | CP14 certified + integrated; remote certification validated; host-deps verified; CP13 ready to certify; CP12B inventory |
| Days 1–2 | CP13, CP15 integrated; records20k 100 % on the VM (first end-to-end benchmark); CP12B features landing; CP17 integrated |
| Days 3–4 | CP16 integrated; CP12B lifting coverage; CP18; CP19 started |
| Days 5–7 | CP12B complete; CP20, CP22, CP24 under way; cross-platform CI milestone for everything landed |

Early signals: whether CP13–CP17 land within 2–3 days without a multi-day
certification crisis, and how fast CP12 phase B moves coverage from 13 % toward
50 %+ (the real predictor for CP25).

## Actuals

| Checkpoint | Estimated | Actual | Notes |
|---|---|---|---|
| CP12 phase A | ~1 day | ~1.5 days (2026-10-18 → 2026-10-19) | two review rounds; flaky-gate and harness fixes |
| feature-site-dx | ~0.5 day | ~1 day | two review rounds, 4 certify failures resolved by evidence |
| feature-serve | small | ~0.5 day | |
