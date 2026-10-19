# Runtime 2.0 delivery forecast (living)

_Last updated: 2026-10-19 18:35 AEDT. Updated when milestones land or blockers
emerge, not continuously. Authoritative per-checkpoint evidence stays in
`progress.md`; this file tracks state, dependencies, estimates and the forecast._

## States

Each checkpoint moves through: **implemented → reviewed → locally certified →
integrated (`runtime-2`) → cross-platform certified** (macOS + Windows CI at a
milestone). A checkpoint split into phases is complete only when every phase is.
Linux certification moves to a dedicated Linode once validated (D-033).

## Headline

- Critical path: **CP12 phase B → CP16/CP17 → CP24 → CP25**. CP25 (retire the
  legacy evaluator) needs near-complete VM coverage, so coverage — not built-ins —
  sets the pace.
- VM coverage (shared corpora): **101 / 786 (12.8 %)**, measured at CP12 phase A.
  Refusal-reason inventory with sole-blocker and multi-reason counts: in progress
  (`checkpoints/cp12b-coverage-inventory.md`, pending).
- Forecast (2026-10-19): CP13–CP18 plus a large share of CP12 phase B within the
  next weekly allowance is plausible; the whole roadmap (CP00–CP27) within it is
  unlikely (~10–15 %); realistic for the whole roadmap: **3–5 weeks**.
  Implementation complete ≠ cross-platform-certified release.

## Checkpoints

| Checkpoint | Implemented | Reviewed | Locally certified | Integrated | Cross-platform | Depends on | Remaining effort | Bottleneck risk | Notes |
|---|---|---|---|---|---|---|---|---|---|
| CP00–CP03, CP05, CP06, CP08 | ✓ | ✓ | ✓ | ✓ | ✓ | — | — | — | see `progress.md` |
| CP04 | — | — | — | — | — | CP00 | — | — | deferred (D-006) |
| CP07 | ✓ | ✓ | ✓ | ✓ | ✓ | CP00 | launch-cost gate | low | launch-cost gate deferred, not passed (D-011) |
| CP09–CP11 | ✓ | ✓ | ✓ | ✓ | ✓ (CI @c6fac06) | — | — | — | |
| **CP12 phase A** (engine selection, replay removed) | ✓ | ✓ (2 rounds) | ✓ @ebc2656 | ✓ bd1c8a9 | pending | CP11 | — | — | planted-defect gate permanent |
| **CP12 phase B** (globals, `GETDYN`, modules, structs, enums) | started 2026-10-19 (inventory) | | | | | CP12A | large | **high** | the coverage engine; agent assigned |
| CP13 diagnostics | ✓ (on `spec-integration`) | ✓ (design) | | | | CP12A | small | low | some gates rerun on new base pending |
| CP14 built-ins A | ✓ | ✓ | certifying 6c264045 (local + remote) | | | CP12A | certification | low | coverage unchanged by itself (104/788) |
| CP15 built-ins B | natives ✓, wiring written | ✓ (natives) | | | | CP14 | small–medium | low | time natives moved to CP16 |
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

## Schedule (fastest credible; no gates weakened)

| When | Target |
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
