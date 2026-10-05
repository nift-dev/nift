# POST-V46-RUNTIME-ARCHITECTURE-REVIEW

Investigation and planning checkpoint. **No runtime source was changed.** This
document recommends a campaign; it does not begin implementation.

## Baseline and method

- Released: `v4.6.0` / tag target `bb6e9f2`.
- Local development HEAD: `f4c595f` (v4.7.0 bump + methodology note; runtime
  source identical to `bb6e9f2` — `git diff 85960d1..f4c595f -- src include
  Makefile snap packaging` is empty apart from the version string).
- Remote `main`: `52536b9` (frozen; nothing pushed).
- Deterministic evidence: **callgrind instruction counts** in isolated git
  worktrees (`v4.5.0` = `560863b`; `v4.6.0` = `bb6e9f2`), same toolchain.
  Wall-clock corroborates only.
- Raw instruction runs retained outside the repo under
  `.review-wt/` (disposable worktrees); numbers below are the `summary:` Ir.

### Method caveat (important)

The v4.6 handover's benchmark harness and raw counters are **not retained in the
repository**; its `fn_empty`/`fn_args`/`map_set_new`/`map_iterate` workload
definitions exist only as prose. The review therefore **re-derived** the
workloads (see "Reproduced workloads") and re-measured. Directions and most
magnitudes reproduce; absolute instruction counts are not comparable to the
handover because the iteration counts differ.

## Reproduced workloads (isolated worktrees)

| Workload | Shape | N |
|---|---|---|
| numeric_loop | `fn(main(n)){s:=0;i:=0;while(i<n){s+=i;i+=1}}` | 500k |
| array_push_index | build 10k array, index `arr[i%10000]` | 500k |
| fn_empty | `fn(noop()){return 0}` called in a `while` inside `fn(main)` | 20k |
| fn_args | `fn(add1(x)){return x}` called in a `while` inside `fn(main)` | 20k |
| lambda | `f := (x)=>x` called in a `while` inside `fn(main)` | 20k |
| map_set_new | `map().set(i,i)` distinct keys | 2k |
| map_get | map of 500, `m.get(key)` | 20k |
| map_contains | map of 500, `m.contains(key)` | 20k |
| map_iterate | map of 1000, `for((k,v):m){...}` | 100 |

## Reproduced residuals (callgrind Ir)

| Workload | v4.5.0 | v4.6.0 | Δ |
|---|---:|---:|---:|
| numeric_loop | 2,935,257,998 | 2,457,402,241 | **−16.3%** (faster) |
| array_push_index | 3,793,911,047 | 3,380,100,744 | **−10.9%** (faster) |
| map_get | 5,504,872,326 | 5,078,079,494 | **−7.8%** (faster) |
| map_contains | 6,284,373,410 | 5,394,147,934 | **−14.2%** (faster) |
| fn_empty | 3,547,740,679 | 4,258,578,190 | **+20.0%** |
| fn_args | 4,259,653,287 | 5,071,863,009 | **+19.1%** |
| lambda | 4,100,283,361 | 4,819,297,028 | **+17.5%** |
| map_set_new | 280,437,129 | 320,067,326 | **+14.1%** |
| map_iterate | 11,313,483,494 | 13,203,993,099 | **+16.7%** |

Directions match the handover exactly (numeric/array/get/contains improved;
empty/args/lambda/set-new/iterate regressed); wall-clock corroborates. The
regressed ratios (+14–20%) are, if anything, larger in this shape than the
handover's (+11.7%/+15.1%), likely because the reference shape has a larger
fixed startup share.

## Current profile — the decisive finding

Profiling the current `bb6e9f2`/`f4c595f` binary on the regressed workloads
does **not** reproduce the handover's RuntimeValue-dominated profile. It shows
the cost is the **legacy string expression evaluator plus string tokenization**:

`fn_empty` (v4.6, top functions by Ir):
```text
std::string ctor/assign/memmove/strlen/memcmp/copy   ~ 60%
Parser::evaluate_expression_impl                     ~ 15-20%
Parser::parse                                        ~ 1%
RuntimeValue move-assign / destructor / copy-assign  ~ <1% combined
```

`fn_args` and `lambda` show the same shape (~55% string ops, ~10–22%
`evaluate_expression_impl`, RuntimeValue <1%). `map_set_new` shows
`runtime_numeric_fingerprint` ≈ 0.12% and `numeric_form` ≈ 0.05%; `map_iterate`
shows no `structural_equal`/fingerprint in its hot set. The map *container* is
not the cost.

Two script loop forms were compared directly (`while` inside `fn(main)` vs a
top-level `for(k : range(...))` calling `fn(noop())`): both cost ≈4.26B Ir for
20k calls, i.e. **both execute the loop body through the legacy evaluator** and
re-tokenize the statement text per iteration. The prepared AST call path is not
reached by ordinary script shapes.

Consequence: the dominant residual is **per-statement/per-call re-tokenization
and string evaluation in the legacy executor**, not value lifecycle and not the
map data structure. This reprioritises the campaign.

### Reconciliation with the handover

The handover states the benchmarked function-call path "already uses prepared
AST execution" and profiles RuntimeValue lifecycle at ~20%. That picture holds
for a *prepared* body; the ordinary script shape (a `while`/`for` body calling a
function) is legacy. The two are not contradictory but they are different
bottlenecks, and the handover's `fn_empty` absolute count (≈7.4k Ir/call) is
~24× cheaper per call than the reproduced script shape (≈177k Ir/call) — strong
evidence the handover measured the prepared path. The review treats the
legacy-executor cost as the primary, higher-value target.

## Commit attribution (verified against source/history)

| SHA | Subject | Standing |
|---|---|---|
| `560863b` | v4.5.0 release notes + rehearsal guard | baseline identity **proven**; the 14.81B is asserted (no counter retained) |
| `7b97ae1` | separate runtime values from json | **likely**: introduces `RuntimeValue.{h,cpp}`; parity number unreproduced. Note the 152-byte size postdates it (`aa71108` adds Error) |
| `64d57cb` | harden runtime numeric boundaries | **proven mechanism** (wires `numeric_form` into per-op paths); magnitude asserted. Repaired by `dc37ea3`/`5b2395c` → numeric now **faster** (reproduced −16.3%) |
| `2bb28fa` | split parser template implementation | **correlated only** — pure code move, no mechanism |
| `aa71108` | add recoverable error handling | **proven introducing commit** (independent CP9 cert bisects the tight-loop regression here); the dominant mechanism is per-statement `ast::Context` reconstruction, wider than "ExecOutcome payload" |
| `258dc74` | remove ExecOutcome payloads from hot path | **proven repair**; its diff also builds `ast::Context` once per parse — the larger recovery, under-described by the title |
| `dc37ea3`,`b818cb3`,`5b2395c` | Number/scalar fast paths | **proven + commit-measured** |
| `ae13adc` | harden fast paths | correctness-only |
| `85960d1` | skip builtin dispatch compares | **proven + commit-measured**; its own recorded final (fn_empty 15.95B/+7.7%, fn_args 19.19B/+13.3%) **contradicts** the handover's accepted 16.54B/+11.7%, 19.48B/+15.1% |

The intermediate absolute instruction counts in the handover (14.78/18.19/19.49/
27.80/21.66B) have no retained counter; treat them as narrative, not certified.

## Rejected diagnoses (reconfirmed, do not retry without new evidence)

1. **RuntimeValue size alone** — disproven as the *sufficient* cause: the
   reproduced profile puts RuntimeValue lifecycle at <1–2% on the regressed
   workloads; the size is a contributor to constants, not the driver. The
   152-byte size also postdates `7b97ae1`.
2. **Reparsing every call** — disproven as stated: `Parser::parse` is ~1% here;
   the cost is `evaluate_expression_impl` re-tokenizing statement *text*, a
   different mechanism. The legacy executor path (not the parser) is the target.
3. **Blind `shared_ptr` removal** — unsafe: `VariableBinding` encodes location
   references (root+path slots) and caller-local ownership; removing shared
   ownership breaks aliasing/shadowing/lifetime.
4. **Blanket prepared execution** — unsafe: `Ast::Call` deliberately falls back
   to legacy when any argument is a location (reference identity), documented at
   `src/Ast.h:36-42` and implemented at `src/Ast.cpp:108`.
5. **Replace maps with `unordered_map`** — unsafe and misdiagnosed: storage is
   still the v4.5 insertion-ordered vector; the map residual is interpreter cost,
   and ordered/duplicate-aware equality must be preserved.

## Architecture review

### A. RuntimeValue representation / lifetime
Current: 152-byte fat discriminated union; `type`/`num`/`boolean`/`string`/
`array`/`object`/`bytes`/`timer`/`error`; functions/lambdas/streams/FFI are
`String` sentinels in Parser side tables. No user destructor → every value
destroys all six heavyweight members; copy-assign clears 3 containers + resets 3
shared_ptrs even for scalars; copy-ctor is scalar-aware (`b818cb3`), move is
defaulted.
Finding: **low expected payoff for the current residual** (<2% of Ir on the
regressed workloads), but a real memory/cache constant-factor win. Recommendation:
**defer behind C/B**; if pursued, the cheap subset (scalar-aware destructor +
copy-assign) first, full small-representation rewrite last.

### B. VariableBinding / call frames
Current: `VariableBinding` ≈88B with `shared_ptr<RuntimeValue>` + a second
`shared_ptr<shared_ptr<RuntimeValue>>` slot (two allocations per parameter);
scopes are `vector<unordered_map<string,VariableBinding>>`; `enter_lexical_
environment` can copy whole scope maps per module call; `try_location_ref`
re-parses each non-quoted argument.
Finding: per-call allocations/hash inserts are real but small relative to the
legacy tokenization on the reproduced shape. Recommendation: **second priority**;
attack it together with C (they share the call path), preserving location/
closure/module semantics.

### C. Prepared-execution coverage (primary)
Current: top-level `$[...]` statements, script `while`/`for` bodies, lambda
bodies, struct-method bodies, collection callbacks, imports and workers run the
**legacy string evaluator**; only AST `Call` nodes inside prepared bodies use the
prepared executor, and `@for` over a map computes then discards a prepared body.
Finding: **this is where the residual lives** — per-iteration tokenization of
statement text. Recommendation: **first priority**; extend prepared execution to
the common script statement/loop/call shapes, gated by the existing differential
corpus, with the `arg_is_location` fallback retained (or taught to bind
locations in prepared form).

### D. Maps
Current: plain objects = insertion-ordered `vector<pair<string,RuntimeValue>>`
with linear lookup and O(n²) order-insensitive equality; `map`/`sorted_map`
collections carry `entries` + an `unordered_set<string> scalar_keys` candidate
index + exact-decimal numeric keys; `sorted_map` re-sorts per insert.
Finding: the container is **not** the current residual (fingerprint <0.1% here).
Recommendation: **defer**; revisit only if a map-heavy end-to-end workload
(not microbenchmarks) shows container cost after C/B land.

## Architecture dependencies

```text
C (prepared coverage)  ── biggest, most independent lever; do first
        │ simplifies
        ▼
B (call frames/binding) ── shares the call path; benefits from C's AST slots
        │ may reduce pressure for
        ▼
A (RuntimeValue)       ── constant-factor; best valued after C/B expose the true steady state
D (map container)      ── mostly orthogonal; defer
```

C before B (C reduces how often the expensive legacy binding/tokenisation runs,
and exposes the prepared call path where slot allocation matters). A after C/B
(so its payoff is measured against the post-C baseline, not today's
tokenisation-dominated one). D independent/last.

## Semantic invariants (must not change)

Pinned by the 92 external contract modules plus implementation tests; the
campaign must keep all green, notably: RuntimeType ordinals/fingerprints
(`tests/runtime_value.cpp`); deep-copy-on-value-copy; exact Number/StrNumber
equality/order/NaN/±inf; object equality order-insensitive but duplicate-aware;
map numeric-key unification and typed-key distinctness; map insertion order and
`sorted_map` order; location/reference identity across calls
(`tests/v44_element_assignment_smoke.sh`, `tests/v43_readval_writeval_smoke.sh`);
closure-captures-binding-not-snapshot; recursion cap 64; cyclic-reference
rejection; Bytes/Timer/Error non-serializable and Error-chain ≤16; prepared ≡
legacy observably (`tests/v44_ast_differential_corpus.sh`,
`tests/v44_ast_fuzz.py`); C ABI 1.3; website build behavior.

## Benchmark plan

- **cheap (per checkpoint):** `make test-scripting-perf`, the callgrind
  microbenchmarks above (fn_empty/fn_args/lambda/map_set_new/map_iterate), native
  wall-clock medians.
- **medium (per checkpoint gate):** `make test-v43-language test-collections
  test-v44-language-foundation`, the `v44_ast_*` differential/fuzz suite,
  `make test-performance-scaling`, the 10k website build.
- **full (campaign closeout):** v4.5.0 / v4.6.0 / candidate callgrind matrix
  (all 9 workloads), `make benchmark-10k` no-op/single-edit/shared-template/full,
  peak RSS, cross-platform CI + the 92 external contract modules.
- Callgrind = primary (load-independent); wall-clock = repeated/paired medians.

## Success / stop criteria

Per change: **CLEAR WIN** ≥10% Ir reduction on the target with no semantic
regression; **KEEP** 3–10% or clear memory/simplification win; **NEUTRAL** no
material change; **REJECT** complexity/risk without benefit; **REGRESSION** any
unrelated workload materially worse.

Campaign exit: stop when the regressed scripting workloads (fn_empty/fn_args/
lambda/map_set_new/map_iterate) are back within ~5% of v4.5.0 **or** a clearly
justified steady-state bound is reached with the remaining gap attributed and
accepted in writing. An area is closed when its next increment is REJECT/NEUTRAL
twice. Explicitly accepted-at-the-end residuals must be recorded.

## HTTP implications (not resumed)

The previous native HTTP path was dominated by socket I/O and protocol work, not
interpreter microbenchmarks; the runtime campaign is unlikely to move HTTP
throughput materially. Any benefit would be indirect (cheaper per-request
dispatch/callbacks). The worker/pool and native-helper feasibility remain
unaffected by A–D. HTTP/Strut/socket were **not** touched.

## Recommended campaign

**CP0 — baseline + semantic locks.** Re-land the harness + workloads as tracked
`benchmarks/` scripts (currently missing). Record v4.5.0/v4.6.0/candidate
callgrind matrix. Freeze the invariant test list. No runtime change.
*Accept:* harness reproduces the residual directions.

**CP1 — prepared statement coverage for script bodies (Area C).** Extend prepared
execution to the common script statement/loop shapes currently going through the
legacy evaluator, keeping the `arg_is_location` fallback. Files: `Ast.cpp`,
`ParserTemplate.cpp`, `ParserExpression.cpp`. *Semantic risk:* high — gate on
`v44_ast_differential_corpus.sh` + `v44_ast_fuzz.py` + the location/alias tests.
*Benchmark:* fn_empty/fn_args/lambda/numeric/map_iterate. *Accept:* CLEAR WIN on
the call workloads, no regression elsewhere. *Reversible:* yes (fallback switch).

**CP2 — lambda / callback / method body preparation (Area C cont.).** Reuse
`prepared_callables_` for lambda/method/callback bodies; stop building
`__nift_cb(...)` strings. *Accept:* lambda + collection-pipeline wins.

**CP3 — call-frame/binding allocation reduction (Area B).** Remove the second
per-parameter `make_shared` / avoid scope-map copies in
`enter_lexical_environment`; consider slot-indexed parameters. *Gate:* closure/
location/module differential tests. *Accept:* KEEP-or-better on fn_args once CP1
has landed.

**CP4 — RuntimeValue constant-factor (Area A, subset).** Scalar-aware destructor
+ scalar-aware copy-assign; measure. *Accept:* KEEP. (Full small-representation
rewrite only if CP1–CP3 leave a justified gap and with its own checkpoint.)

**CP5 — evaluate map container (Area D) only if end-to-end map workloads still
show cost.** Otherwise close D as REJECT.

**FINAL — campaign certification.** Full matrix vs v4.5.0/v4.6.0, website build
gates, cross-platform CI, 92 external modules, written accepted-residual list.

## Opportunity ranking

1. **Prepared-execution coverage (C)** — high payoff, high confidence (profile-
   proven), medium-high cost, high semantic risk but well-gated.
2. **Call frames / VariableBinding (B)** — medium payoff, medium confidence,
   medium cost, high semantic risk; do with C.
3. **RuntimeValue representation (A)** — low payoff for the current residual,
   high confidence it is small, high cost, high risk; defer/subset.
4. **Map container (D)** — low current payoff, low confidence of end-to-end
   benefit, medium cost, medium risk; defer.

The evidence does **not** justify the handover's order (A first). The profile
says **C first**, B with it, A/D deferred.

## Five-point summary

1. Residuals reproduce (fn_empty/fn_args/lambda ~+17–20%, map_set_new +14.1%,
   map_iterate +16.7%; numeric/array/get/contains faster).
2. The residual is legacy-executor tokenization + string cost, not RuntimeValue
   or the map container.
3. The handover's RuntimeValue-centric profile reflects the prepared path; the
   ordinary script path is legacy.
4. Recommended order: C → B → (A subset) → D, gated by the differential corpus.
5. Keep everything local; no push while `main` is frozen.
