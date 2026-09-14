# Performance campaign CP7–CP12

This tranche continues from CP6 (`3fe3d64`). Production changes are retained only when semantic gates remain green and repeated measurements show a representative benefit. The external 12-fixture benchmark with same-run OXC, SWC, esbuild and Terser controls remains the release-level performance oracle.

## CP7 — binding-renamer ownership propagation

The mangler previously recomputed each scope's owning function by walking its full ancestor chain. Scope creation is parent-before-child, so ownership is now propagated in one linear pass: function scopes own themselves and other scopes inherit their parent's owner. This removes redundant ancestry work without changing scope semantics.

## CP8 — semantic facts on demand

Structured mode does not consume `JsSemanticFacts`; those facts are needed by aggressive optimization and diagnostics. Construction is now skipped for ordinary structured runs and retained unchanged for aggressive mode and diagnostic APIs. This removes one full token pass from structured minification without changing output.

## CP9 — avoid repeated binding-renamer planning after a successful rename

A successful binding-renaming pass already assigns all eligible bindings in that compilation unit. Recursive cleanup now remembers that the binding rename phase has completed, so later aggressive rounds do not rebuild the same mangler plan. If the first attempt produces no rename the old behaviour is retained, avoiding changes to cases where later rewrites could alter eligibility.

## CP10 — class-ancestry locality shortcut rejected

A precomputed boolean intended to replace the per-reference opaque-class ancestor walk failed the existing self-referential-class smoke regression. The production change was removed completely. See `performance-cp10-class-ancestry.md`.

## CP11 — scanner/output audit

The profile harness accepts `--mode` so individual policies can be measured in isolation. The scanner/output path already reserves its output buffer and was not the dominant structured/aggressive cost on the generated probes, so no speculative printer change was retained. See `performance-cp11-output-audit.md`.

## CP12 — integration measurement and semantic certification

Seven-run medians compare CP6 (`3fe3d64`) with the CP11 endpoint on three generated diagnostic workloads. These are direction-finding probes, not substitutes for the external real-bundle benchmark.

| Probe | Mode | CP6 ms | CP11 ms | Delta |
|---|---|---:|---:|---:|
| nested functions/bindings | default | 2.301 | 2.272 | -1.3% |
| nested functions/bindings | structured | 22.081 | 19.976 | -9.5% |
| nested functions/bindings | aggressive | 35.558 | 30.069 | -15.4% |
| aggressive-friendly | default | 3.892 | 3.802 | -2.3% |
| aggressive-friendly | structured | 30.529 | 30.739 | +0.7% |
| aggressive-friendly | aggressive | 101.812 | 85.400 | -16.1% |
| class-heavy | default | 0.978 | 0.948 | -3.1% |
| class-heavy | structured | 7.776 | 8.105 | +4.2% |
| class-heavy | aggressive | 14.952 | 12.568 | -15.9% |

The class-heavy structured +4.2% result is below the campaign's 5% investigation threshold but is explicitly recorded rather than hidden. The external benchmark must decide whether the retained changes survive on real bundles.

Semantic gates completed at the endpoint:

- standalone smoke: pass;
- Node semantic differential: pass;
- structured semantic differential: 19/19 pass;
- aggressive differential: 11/11 pass;
- generated semantic corpus: 15,459 programs pass.

The external 12-fixture benchmark cannot be regenerated in this environment because the benchmark checkout lacks its installed pnpm dependencies. Do not call CP7–CP12 release-certified until the GitHub benchmark reports 36/36 Minify++ validation and same-run competitor timings.
