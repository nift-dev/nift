# Post-v4.6 runtime performance architecture handover

## Status

**Deferred architectural work.** This records the v4.5→v4.6 runtime performance
findings and the explicitly-deferred follow-up campaign. It is not a description
of shipped behavior and must not be presented as current functionality.

Decision (accepted): **ship v4.6 first, then run the post-v4.6 runtime
architecture campaign.** The residual interpreter regressions below are
**deferred work, not forgotten or permanently accepted.**

## Sequencing

```text
1. preserve this handover (done)
2. V4.6-WEBSITE-DOCS-AUDIT
3. v4.6 release preparation / certification
4. release v4.6.0
5. POST-V46-RUNTIME-ARCHITECTURE  (this document)
6. resume Nift HTTP work backed by Strut
```

If HTTP is later chosen to precede step 5, that is an explicit decision — not a
silent drop of the performance work.

## Baseline and method

- Baseline: the immutable released tag `v4.5.0` = commit `560863b`.
- Candidate: current `main` at the time of the campaign.
- Deterministic evidence: **callgrind instruction counts** (load-independent) are
  the preferred evidence for interpreter hot paths. Wall-clock is corroborating
  only — the current binary does more memory traffic per call, so wall-clock
  deltas are larger and noisier under shared-host load.
- Build both in isolated git worktrees with the same toolchain (`gcc`, `-O2`,
  `make -j$(nproc)`).

## Accepted v4.6 result

```text
website build performance     ~= v4.5 (10k full/no-op/incremental within ~4%)
numeric loops                 faster than v4.5 (callgrind ~-16%)
arrays (push/index/write)     parity or faster than v4.5 (index ~-11%, push ~-21%)
map get / overwrite / contains  parity or faster than v4.5 (-3% .. -14.5%)
```

## Known residuals deferred to the post-v4.6 campaign

Callgrind (deterministic) figures:

```text
fn_empty   (2M empty calls)     14.81B -> 16.54B   +11.7%
fn_args    (2M one-arg calls)   16.93B -> 19.48B   +15.1%
map_set_new (O(n^2) append)      9.21B -> 10.51B   +14.1%
```

Wall-clock (noisy, larger under memory pressure) corroboration: lambdas ~+13–20%,
map_iterate ~+12–25% depending on the exact workload/measurement.

These are meaningful (>10%) interpreter regressions. They are understood and
attributed; they are not yet recovered because safe local repair is unavailable
(see below).

## Causal evidence / commit attribution (callgrind, fn_empty)

```text
v4.5.0 / 560863b   baseline                          14.81B
7b97ae1            cp15: separate runtime values from json   14.78B  (parity)
                   -> the RuntimeValue representation change ALONE caused no
                      function-call regression; disproves "the 152-byte
                      RuntimeValue alone caused it"
64d57cb            cp15: harden runtime numeric boundaries    18.19B
                   -> introduced the numeric/index regressions via the
                      exact-number decimal layer (numeric_form on every numeric
                      op); subsequently recovered by fast paths (dc37ea3,
                      5b2395c) so numeric loops are now faster than v4.5
2bb28fa            refactor: split parser template implementation  19.49B
aa71108            feat: add recoverable error handling        27.80B  (+87%)
                   -> PRIMARY introducing commit for the remaining function-call
                      overhead (error/diagnostic/ExecOutcome scaffolding)
258dc74            perf: remove ExecOutcome payloads from hot path 21.66B (-6.1B)
dc37ea3            perf: Number fast path in runtime_number_is_integer
b818cb3            perf: scalar-aware RuntimeValue copy ctor/assignment
5b2395c            perf: Number fast paths in runtime_number_to_size/_is_zero
ae13adc            perf: harden Number fast paths (non-finite / 2^64)
85960d1            perf: skip builtin-call dispatch compares for user functions
                   -> recovered ~4 points (pre-guard 17.16B -> 16.54B)
```

## Current profile attribution (fn_empty)

```text
RuntimeValue lifecycle          ~20%
    destructor                  ~10.3%
    move assignment             ~6.0%
    copy assignment             ~3.7%
unordered_map / scope hashing   ~10%
string operations               ~8%
malloc / free                   ~4.5%
SourceContext / fs::path        ~1.8%
AST executor (Parser::parse/ast::evaluate)  ~20%
```

`fn_args` additionally pays a per-argument `std::make_shared<RuntimeValue>` plus
a scope/hash insertion per argument.

**Important:** the benchmarked normal function-call path **already uses prepared
AST execution**. Per-call re-parsing was investigated and **rejected** as the
root cause for that workload (`Parser::parse` is called only a handful of times,
not per call). Do not repeat that false diagnosis.

## Semantic constraints (hard correctness gates)

Any future optimization MUST preserve:

```text
caller-local visibility
shadowing
location/reference argument aliasing
closures
lambda captures
recursion
import/module semantics
struct methods
privacy
recoverable errors
exact-number / StrNumber semantics
bytes semantics
timers/resources
FFI ownership
```

Recorded aliasing hazard: the legacy `bind_call_param` may **alias location
arguments** (`f(a[0])` binds the parameter to `a[0]`'s root+path so in-body
mutation propagates), while the prepared dispatch currently binds value copies.
This is safe today only because the AST `Call` handler detects location arguments
and falls back to the legacy evaluator. **Blindly routing all calls through the
prepared path can change semantics.** Any prepared-execution extension needs a
differential test gate covering caller-local invisibility, shadowing, nested and
recursive calls, closures/lambda captures, imported callables, struct methods,
mutation of visible bindings through aliases, `return <location>` aliasing and
`:=` rebinding, `++/+=` on strings/arrays, division/modulo-by-zero diagnostics,
and const/readonly assignment errors.

## POST-V46-RUNTIME-ARCHITECTURE — campaign scope

Four related investigations; changing one affects the others, so investigate
together.

### A. RuntimeValue representation / lifetime

Investigate (do not assume): small tagged value; scalar-inline representation;
heap backing only for strings/containers/resources; variant representation;
hybrid/PIMPL; copy-on-write where semantically valid. Goal: reduce
destructor/copy/move/cache-footprint cost without weakening value semantics.
Note: a scalar-aware *copy* was already landed (`b818cb3`); the *destructor*
cannot be made scalar-aware in C++ without a representation change.

### B. VariableBinding / call-frame ownership

Investigate whether scalar arguments require a per-call
`shared_ptr<RuntimeValue>` allocation; whether temporary bindings can be
frame-owned; whether known parameter slots can avoid repeated hashing; whether
argument binding can avoid redundant copies/moves. Preserve all alias/reference
semantics.

### C. Prepared execution coverage

Investigate safely extending prepared AST execution to map `for-in`
destructuring, remaining function-call fallback shapes, and argument-bearing
calls where semantics match. Require the semantic differential tests above before
accepting any fast path.

### D. Map architecture

Current map/object storage remains `std::vector<std::pair<std::string,
RuntimeValue>>` — the same shape v4.5 used (so the map regression is not a
data-structure change; it is interpreter/string cost). Investigate an auxiliary
lookup index only if worthwhile. Any key canonicalisation MUST exactly preserve
current equality semantics: object structural equality/order rules, Number vs
StrNumber numeric equality, NaN behavior, cycles, iteration order, overwrite
semantics, copy semantics, serialization semantics, worker/thread transfer
semantics. Do not casually replace map semantics with `unordered_map`.

## Reproducible benchmark specification

Compare against immutable `v4.5.0` / `560863b` with the same toolchain in isolated
worktrees. Core workloads:

```text
numeric loop            (2M iterations of s += i)
array push / index read / index write / iterate
map set-new (pre-generated keys) / overwrite / get / contains present+missing / iterate
fn_empty (2M no-op calls) / fn_args (2M one-arg calls) / lambdas / struct methods
10k-page website benchmark (make benchmark-10k)
```

Use pre-generated string keys for map workloads so `to_string()` does not
contaminate the result; measure dynamic-key versions separately. Prefer callgrind
instruction counts for the interpreter hot paths.

## Related documents

- `docs/handover/PROJECT-CONTEXT.md` — project identity and product reasoning.
- `docs/handover/PERF-REGRESSION-AUDIT.md` — earlier (Embed-era) performance audit.
- `PERFORMANCE.md` — retained website-build performance evidence.
- `ReleaseNotes.md` — v4.6 release notes (must be completed before release).
