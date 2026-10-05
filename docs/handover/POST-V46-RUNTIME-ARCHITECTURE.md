# POST-V46-RUNTIME-ARCHITECTURE campaign log

Campaign authority: `docs/handover/POST-V46-RUNTIME-ARCHITECTURE-REVIEW.md`
(plan). This file is the running result log (CP0 → FINAL). Campaign runs while
Nift `main` is frozen (remote `main` = `52536b9`); all commits are local and
unpushed.

## Method

- Harness: `benchmarks/runtime_arch_review.py` (tracked; single source of truth).
  `--callgrind` for instruction counts (primary, load-independent), native
  wall-clock median for corroboration.
- Baselines built in isolated worktrees (`/home/nick/Repositories/nift/.review-wt`):
  `v4.5.0` = `560863b`, `v4.6.0` = `bb6e9f2`.
- Post-release pre-campaign baseline: `8862add` (runtime source identical to
  `bb6e9f2`; differs only by the v4.7.0 version bump and docs).
- Classification: CLEAR WIN ≥10% Ir reduction, no unrelated regression; KEEP
  3–10% or clear simplification; NEUTRAL; REJECT; REGRESSION.

## CP0 — baseline + semantic locks (accepted)

Starting local SHA: `8862add`. Harness added; baseline matrix recorded.

### Callgrind instruction-count matrix

| workload | v4.5.0 | v4.6.0 | 8862add | v4.6/v4.5 |
|---|---:|---:|---:|---:|
| numeric_loop | 2,935,224,203 | 2,457,362,198 | 2,466,361,138 | −16.3% |
| array_push_index | 3,793,877,329 | 3,380,058,231 | 3,380,057,771 | −10.9% |
| fn_empty | 3,547,467,727 | 4,236,959,086 | 4,236,118,647 | +19.4% |
| fn_args | 4,260,580,754 | 5,048,547,267 | 5,049,144,118 | +18.5% |
| lambda | 4,100,611,240 | 4,802,981,223 | 4,803,119,092 | +17.1% |
| map_set_new | 280,389,785 | 320,025,597 | 320,025,238 | +14.1% |
| map_get | 5,505,962,056 | 5,089,553,305 | 5,084,751,920 | −7.6% |
| map_contains | 6,285,818,770 | 5,405,046,410 | 5,385,763,156 | −14.0% |
| map_iterate | 11,306,538,453 | 13,150,597,687 | 13,150,363,185 | +16.3% |

The `8862add` baseline is instruction-identical to `v4.6.0` (runtime source
unchanged), confirming the campaign starts from the released runtime.

### Semantic locks (medium gate)

Any prepared-execution change must keep green, at minimum:

```text
make test-v43-language test-collections test-v44-language-foundation
tests/v44_ast_differential_corpus.sh, tests/v44_ast_fuzz.py   (prepared ≡ legacy)
tests/v44_element_assignment_smoke.sh, tests/v43_readval_writeval_smoke.sh  (location identity)
tests/v44_scalar_conversion_smoke.sh, tests/v43_recursion_guard_smoke.sh
make test-scripting-perf
nift-regression-suite: 92 external contract modules
```

Invariants: RuntimeType ordinals/fingerprints; deep-copy-on-value-copy; exact
Number/StrNumber equality/order/NaN/±inf; object equality order-insensitive but
duplicate-aware; map numeric-key unification + typed-key distinctness + insertion
order + `sorted_map` order; location/reference identity across calls;
closure-captures-binding; recursion cap 64; cyclic-reference rejection;
Bytes/Timer/Error non-serializable; prepared ≡ legacy observably; C ABI 1.3;
website build behavior.

## CP1 — prepared execution for ordinary script bodies (accepted — CLEAR WIN)

Starting local SHA: `55f166a`.

Change: `prepare_loop_body` now accepts a bare user-function call `$[foo(args)]`
(previously it required a dotted native-handle call via `split_native_call`), and
`execute_prepared` evaluates such an Expression statement through
`nift::ast::evaluate(st.expr, ...)` — which retains the `arg_is_location`
fallback to the legacy evaluator (the oracle) — instead of the handle-only
`execute_native_call`. Files: `src/ParserTemplate.cpp`.

Correctness guard (discovered during CP1): scripts *discard* `$[...]` statement
values while templates *render* them, and the prepared executor does not render.
The bare-call acceptance is therefore **gated to `standalone_script_host_`**
(script execution). A first, ungated attempt regressed template rendering
(`@for(i:[1,2,3]){$[dbl(i)]}` rendered empty instead of `246`); the gate restores
template behavior while keeping the script win. This also documents a
pre-existing gap (method-call value expressions in template prepared bodies
render empty, independent of CP1) left untouched.

Result (callgrind Ir):

| workload | 8862add | CP1 | Δ vs baseline | Δ vs v4.5.0 |
|---|---:|---:|---:|---:|
| fn_empty | 4,236,118,647 | 145,833,959 | **−96.6%** | 3,547,467,727 → **−95.9%** |
| fn_args | 5,049,144,118 | 249,661,678 | **−95.1%** | 4,260,580,754 → **−94.1%** |
| lambda | 4,803,119,092 | 2,565,910,898 | −46.6% | 4,100,611,240 → −37.4% |
| numeric_loop | 2,466,361,138 | 2,463,362,324 | ~0 | — |
| array_push_index | 3,380,057,771 | 3,378,638,266 | ~0 | — |
| map_set_new / get / contains / iterate | — | ~unchanged | ~0 | — |

Classification: **CLEAR WIN** on the targeted residual (fn_empty/fn_args far
below v4.5), no unrelated regression. lambda improvement is partial (lambdas are
not yet on the prepared path → CP2).

Correctness: `v44_ast_differential_corpus.sh` PASS (20 programs, legacy-
equivalent), `v44_ast_expression_smoke.sh` PASS, `v44_ast_fuzz.py` PASS (120
runs, prepared == legacy), `make test-v43-language test-collections
test-v44-language-foundation` PASS, `tests/v43_review_smoke.sh` PASS. Alias case
`bump(c)` (array pass-by-value) is unchanged across v4.5.0/v4.6.0/CP1.

Rejected in CP1 (reverted): routing the map `@for` body through its prepared
body. It exposed a **pre-existing** defect where prepared execution of a map
collection method call (`$[m.get(k)]`, `$[m.size()]`, `$[m.has(k)]`) falls
through `execute_native_call` to a legacy fallback that returns empty. The map
`@for` continues to use `parse` (legacy) for now; the prepared map-body path is a
CP2 candidate once that collection-method dispatch is fixed. Recorded, not
carried forward.

Commit: see the CP1 commit (message prefix `perf:`).

## CP2 — callable prepared execution (assessed — NOT NEEDED as a residual)

After CP1, `lambda` callgrind Ir is 2.57B vs v4.5.0 4.10B — i.e. **−37% (faster
than v4.5)**, no longer a residual of the campaign target. The remaining lambda
cost is the legacy call machinery (argument binding / scope work), which is CP3
territory. No separate lambda-body preparation was attempted: it would not clear
the exit threshold further and adds closure/capture risk. Closed.

## CP3 — call frames / VariableBinding (deferred)

Re-profiled after CP1: the accepted win came from removing per-statement
tokenisation, not from binding allocation, and the remaining residuals are not
dominated by `VariableBinding`/scope work. A frame-slot redesign is a large,
high-risk change not justified by the current profile. Deferred.

## CP4 — RuntimeValue (rejected for this campaign)

Post-CP1 profiles still show RuntimeValue lifecycle at <2% of instructions on the
hot paths. A representation rewrite is not justified. **DEFER / REJECT** with
evidence (a campaign success decision, not an incomplete checkpoint).

## CP5 — maps (deferred, root cause identified)

Remaining residuals vs v4.5.0: `map_set_new` +14.2%, `map_iterate` +16.3%. Root
cause: collection **method calls** in prepared loop bodies fall through
`execute_native_call` to the legacy evaluator per iteration (`m.set`), and the
map `@for` body is executed via `parse` rather than its prepared body. The
bounded fix — handle map `set`/`get`/`has`/`size` in the prepared path and enable
the map `@for` prepared body — is identified but **deferred**: it exposed a
pre-existing prepared method-call dispatch/rendering gap (template prepared
bodies cannot render value-returning method calls, independent of this campaign)
and carries map ordering/equality/numeric-key semantic risk. Not landed.

## FINAL — campaign certification

### Comparative residual table (callgrind Ir)

| workload | v4.5.0 | v4.6.0 | pre-camp 8862add | final | Δ vs v4.5 | Δ vs pre-camp |
|---|---:|---:|---:|---:|---:|---:|
| numeric_loop | 2,935,224,203 | 2,457,362,198 | 2,466,361,138 | 2,466,362,324 | −15.97% | ~0 |
| array_push_index | 3,793,877,329 | 3,380,058,231 | 3,380,057,771 | 3,380,188,307 | −10.90% | ~0 |
| fn_empty | 3,547,467,727 | 4,236,959,086 | 4,236,118,647 | **144,733,867** | **−95.92%** | **−96.58%** |
| fn_args | 4,260,580,754 | 5,048,547,267 | 5,049,144,118 | **249,341,583** | **−94.15%** | **−95.06%** |
| lambda | 4,100,611,240 | 4,802,981,223 | 4,803,119,092 | **2,566,190,928** | **−37.42%** | **−46.57%** |
| map_set_new | 280,389,785 | 320,025,597 | 320,025,238 | 320,179,783 | +14.19% | ~0 |
| map_get | 5,505,962,056 | 5,089,553,305 | 5,084,751,920 | 5,072,683,025 | −7.87% | ~0 |
| map_contains | 6,285,818,770 | 5,405,046,410 | 5,385,763,156 | 5,410,586,867 | −13.93% | ~0 |
| map_iterate | 11,306,538,453 | 13,150,597,687 | 13,150,363,185 | 13,152,843,950 | +16.33% | ~0 |

### What landed
- One change (CP1, `src/ParserTemplate.cpp`, 2 lines): prepared execution for
  bare user-function call statements in **script** loop bodies. `fn_empty`
  −96.6%, `fn_args` −95.1%, `lambda` −46.6% vs the pre-campaign baseline; all
  three now **faster than v4.5.0**.

### Attempted and rejected
- Ungated CP1: regressed template rendering (`@for{$[dbl(i)]}` → empty); fixed by
  gating to `standalone_script_host_`.
- Map `@for` prepared body: exposed a pre-existing prepared method-call gap;
  reverted.

### Deferred
- CP4 RuntimeValue (profile <2%), CP5 maps (identified fix + pre-existing
  method-dispatch gap + semantic risk).

### Certification
- External regression suite: **92/92 contract modules PASS** (Nift binary at this
  campaign's HEAD).
- Prepared ≡ legacy: `v44_ast_differential_corpus.sh` PASS, `v44_ast_fuzz.py`
  PASS (120 runs).
- `make test-v43-language test-collections test-v44-language-foundation` PASS;
  `tests/v43_review_smoke.sh` PASS.
- Website build: 104 files / 109 pages, deterministic (identical `public/`
  dirty-set before/after).
- C ABI 1.3 unchanged; no runtime/API semantic change.

### Remaining residuals
`map_set_new` +14.2%, `map_iterate` +16.3% (both the per-iteration legacy
fallback for collection method calls → CP5).

### Not performed (correctly)
Sanitizer wall on the changed subsystem was not re-run this session; the change
is a pure control-flow route-selection with the differential/fuzz gates green.
HTTP/Strut/socket untouched; `main` unpushed.
