# JavaScript optimizer checkpoints 31–50 evidence

## Scope

Twenty controlled commits establish the observable semantic-analysis surface
needed by roadmap checkpoints 31–50. They add stable IR identities, inventories
for expression/statement/function/module constructs, expanded effect facts and
public deterministic IR and CFG signatures. Normal, abrupt, short-circuit,
conditional and loop edges are represented by the initial CFG.

This is foundation evidence, not a claim that the full roadmap definitions are
complete. In particular, the current representation remains source-positioned
and token-centred rather than a complete expression/statement AST; the normal
minifier printer is certified at semantic boundaries but is not yet an IR-only
printer; effect facts are locally path-aware rather than a fixed-point dataflow
solution; call summaries are target classifications rather than interprocedural
summaries; and the CFG is a conservative token relation rather than complete
basic blocks with exceptional completion routing. Those distinctions remain
release blockers for the corresponding broad claims.

## Retained capabilities

- `javascript_ir_signature` exposes deterministic node IDs, token spans,
  parentage, expression precedence, statements, properties/calls,
  construction/spread sites, binding patterns, functions/classes, modules and
  dynamic-scope barriers.
- The semantic printer boundary suite explicitly retains meaningful empty loop
  statements, ASI-sensitive return behavior and regex boundaries.
- The effect oracle records value/truthiness facts, abstract conversion and
  property categories, resolved/unknown invocation targets, allocation,
  iteration, suspension and abrupt completion markers.
- Alpha-equivalent local renaming is certified across calls, properties,
  computed access and throwing branches.
- `javascript_cfg_signature` exposes entry, normal and abrupt exits plus normal,
  conditional, short-circuit and loop-back edges.
- Diagnostic-only conversion/property/CFG inventories are isolated from the
  production optimization hot path.

## Validation

- Standalone repository test suite passed.
- Generated JavaScript semantics: 15,459 programs passed.
- Scope semantics: 12/12; structured semantics: 18/18; aggressive differential:
  11/11.
- Real bundles: all 36 syntax outputs and all 36 runtime validators passed.
- JSX conformance: 221/221.
- Test262: 39,747 transformed passes, 8,264 runtime-inapplicable cases and zero
  transformed failures.
- Deterministic fuzzing: 70,000 cases passed.
- ASan/UBSan smoke, 70,000-case fuzz and CLI gates passed with leak detection
  disabled for the harness.
- Optional tsc-generated JSX and PostCSS checks were unavailable because their
  dependencies were not installed.

## Performance retention

The initial implementation incorrectly allocated oracle-only inventories on
every structured/aggressive analysis pass and failed the 3% retention rule.
That version was not retained as the final state. A cold diagnostic dispatcher
and separately populated extended facts restore the production hot path.

Same-host 31-repetition, 500-iteration medians:

| Workload | Pre-31 | Checkpoint 50 | Change |
|---|---:|---:|---:|
| JavaScript | 64.9 MiB/s | 66.7 MiB/s | +2.8% |
| JS scope | 92.9 MiB/s | 100.8 MiB/s | +8.5% |
| Structured scope | 12.2 MiB/s | 12.4 MiB/s | +1.6% |
| Aggressive | 3.9 MiB/s | 4.0 MiB/s | +2.6% |
| JSX | 60.3 MiB/s | 60.4 MiB/s | +0.2% |

Output sizes were unchanged, as expected for analysis/oracle checkpoints.
