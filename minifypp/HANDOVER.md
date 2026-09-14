# Minify++ development handover

This is the entry point for developers and coding agents working on standalone
Minify++. User-facing usage belongs in `README.md`; deeper project context and
development guidance lives in `docs/handover/`.

## Authority and identity

- Product: **Minify++**.
- Executable: `minify`.
- Current CLI version: `1.1.1`.
- Public format/API version: `minify::format_version == 1`.
- Language/toolchain: C++17 and Make.
- Supported current formats: HTML, CSS, JavaScript, JSX, JSON, XML, and SVG.
- Historical working name: Sift.

Current source/tests define behavior. The README and ReleaseNotes describe the
public checkpoint. Git records exact history. These handovers preserve rationale,
risk boundaries, testing practice, and production-roadmap context.

## Product boundary

Minify++ is a small conservative standalone native multi-format minifier. It is
not a bundler, module resolver, tree shaker, transpiler, compiler, framework
pipeline, package manager, or general asset system. Focus is a strength.

The central engineering priority is:

```text
semantic preservation
→ valid/controlled output
→ robust failures
→ performance
→ marginal compression ratio
```

A few extra bytes are preferable to a transformation whose safety cannot be
explained and tested.

## Architecture and Nift relationship

- Public library API: `include/minify/Minify.h`.
- Implementation: `src/Minify.cpp` and private vendored `src/Json.h`.
- CLI wrapper: `cli/main.cpp`.
- Tests: `tests/`.
- Nift embeds a standalone-style copy under `nift/minifypp/` and consumes only the
  public API.

At this checkpoint, standalone and embedded `Minify.cpp` and `Minify.h` are
byte-identical. The mirrored 19-file standalone contract can be checked with
`make check-nift-sync NIFT_MINIFYPP_DIR=/path/to/nift/minifypp`. Standalone
Minify++ is the intended canonical project identity;
changes should originate here, pass standalone validation, be synchronized into
Nift, and then pass Nift integration. Document allowed wrapper/build differences
rather than forcing every file to match.

The v1.1.2 development tree contains HTML, JavaScript and JSX scanner corrections
found by the independent WPT, Test262 and TypeScript JSX harnesses. Nift's embedded `Minify.cpp`
and retained minifier tests must therefore be updated before the next Nift
release; until then the standalone/Nift equality gate is expected to report
this deliberate pending synchronization. The supplied conformance workspace
does not contain Nift itself, so that cross-repository synchronization remains
an explicit release prerequisite rather than an unverified claim.

Nift minification is opt-in by configured extension and occurs at the final-output
boundary. Minify++ does not depend on Nift's parser, tracking state, or build
engine.

The private `src/Json.h` is also a synchronized vendored copy of standalone
**Jsonic++** `include/json.h`. Jsonic++ owns parser semantics; Minify++ owns the
minifier behavior that consumes it. Parser changes should originate in Jsonic++,
pass its standalone tests, synchronize here, then pass the complete Minify++
format/semantic corpus before Nift's embedded Minify++ copy is reconciled.

## Build and tests

```bash
make
make test
```

The current test target includes C++ smoke tests, Node semantic tests, generated
JavaScript semantic cases, JSX/TSX cases, format idempotence, cross-format
adversarial cases, and CLI behavior. Current retained checkpoint evidence includes
15,459 executable JavaScript semantic programs, 180 JSX/TSX cases, and 111
generated non-JavaScript documents. Treat counts as checkpoint evidence, not a
quality identity.

The Makefile provides `test-fuzz`, `test-sanitize`, and `benchmark` targets.
Performance-sensitive JavaScript changes must be benchmarked during development,
not deferred to batch integration. Any real-bundle post-validation regression is
an automatic rejection. Investigate aggregate throughput regressions above 5%,
and reject regressions above 10% on a large representative fixture unless an
explicitly measured trade-off is accepted. Compare against same-run controls when
available; stale competitor timings are not an environmental control.
Sanitizer claims remain workload-specific, and benchmark results remain host- and
fixture-specific evidence rather than portable promises.

## Development standard

For any transformation:

```text
establish baseline
→ define exact safe transformation
→ construct safe examples and unsafe neighbors
→ add regression/semantic oracle
→ implement conservatively
→ language-specific and full corpus
→ sanitizers
→ performance/output-size evidence where relevant
→ synchronize Nift and run integration
→ reconcile docs/site/handover/roadmap
```

Golden output alone is not enough, especially for JavaScript. Execute original
and minified programs and compare observable behavior where practical.

## Checkpoints and public actions

A validated checkpoint is an evidence-backed baseline, not automatically a
commit, tag, release, version bump, Nift update, website publication, or push.
Report baseline, transformation scope, new failure families, tests/corpus,
sanitizers, output-size/performance evidence, Nift sync/integration, docs/site,
repository state, and remaining limitations.

Do not commit, push, tag, release, deploy, or make destructive public/repository
changes without explicit approval.

The detailed operational checklist is `RELEASE.md`. The public `install`,
`download`, `update` and `uninstall` scripts are canonical under `packaging/`
and must be deployed byte-for-byte before the non-publishing release rehearsal.
Only the exact rehearsed commit may be tagged, and published assets are
immutable.

## Deeper handovers

- `docs/handover/PROJECT-CONTEXT.md`: identity, format risks, and history.
- `docs/handover/ARCHITECTURE.md`: scanner architecture, per-language semantic
  boundaries, integration ownership, and the August 2026 standalone/Nift
  reconciliation record.
- `docs/handover/DEVELOPMENT.md`: implementation/checkpoint workflow.
- `docs/handover/TESTING.md`: semantic, adversarial, corpus, and safety strategy.
- `docs/handover/DECISIONS.md`: settled/rejected/unresolved boundaries.
- `docs/handover/ROADMAP.md`: living production-readiness risk assessment.
- `docs/handover/PRODUCTION-READINESS.md`: current evidence-backed readiness
  decision, scope, limitations, and reopening conditions.
- `docs/handover/PROJECT-HISTORY.md`: detailed Minify++ history and
  institutional context, including production definition, per-format risks, testing,
  integration, and roadmap history.

## Maintaining this handover

These are living project documents. Review them when format behavior, public API,
tests/corpora, synchronization, release workflow, product boundaries, or durable
lessons change. Correct and consolidate rather than appending a diary. A
substantial checkpoint must review handover and production-roadmap impact.

## 2026-09-13 — mangler/printer checkpoints 1–14

Named function declarations, named function expressions, and conservatively
eligible class declarations now participate in coordinated scope mangling.
The printer additionally removes proven block-boundary line terminators,
single-primary grouping in narrowly certified expression contexts, restores
identical explicit object properties to shorthand, and shortens `0.x` decimal
fractions to `.x`. Public property spelling, direct-eval barriers, opaque class
references, loop empty statements, restricted-production newlines, and JSX
boundaries retain explicit regressions.

Integration evidence is recorded in
`docs/evidence/mangler-printer-checkpoint-14.md`. After installing the omitted
dependencies, the complete suite passed, including 15,459 generated JavaScript
programs, 180 JSX programs, 17 PostCSS semantic fixtures, 36 real-bundle
outputs, and 70,000 deterministic fuzz cases. Test262 revision
`419d3e0a2273ba01a3bfcbec423f2801425b8e93` returned zero unexpected
transformed failures after conservative barriers were added for grammar the
lightweight resolver cannot yet certify. Inferred `.name` spelling tests still
need an alpha-renaming-aware oracle, and generic-harness buffer-detachment
failures remain runtime infrastructure limitations. Fresh conservative,
structured, and aggressive four-fixture sizes are retained in the evidence
document; D3 is now the largest remaining benchmark coverage gap.

## 2026-09-13 — D3 checkpoints 1–10

The D3 gap was diagnosed rather than treated as a compressor problem. Compared
with UglifyJS without compression, 87.2% of the raw byte gap was identifier
spelling and another 12.0% was whitespace. A bundle-wide multi-arrow rejection
inside D3's UMD factory accounted for about 40 KiB by itself. Function units
are now allocated parent-first and the broad rejection is removed; D3
structured output fell from 336,276 to 295,969 bytes while the complete
selected-Test262 transformed-failure count remained zero.

Large-input profiling then found millions of repeated allocation and live-range
checks. Live ranges and scope depths are cached once, and generated replacement
names use their candidate index to reach only bindings sharing that spelling.
This reduced TypeScript structured from 5.761 s to 1.363–1.513 s and ECharts
structured from 11.709 s to 0.625–0.630 s on the checkpoint host. Aggressive
rewriting now has a seven-successful-round convergence guardrail: this retained
byte-identical TypeScript and ECharts outputs while bringing their aggressive
runs to 8.266 s and 3.296 s respectively.

Integration evidence is in `docs/evidence/d3-checkpoint-10-integration.md`.
All four diagnostic bundles complete in conservative, structured, and
aggressive modes under the upstream ten-second timeout. The full product gate
and all 48,011 selected tests at Test262 revision `419d3e0a…` passed, with
39,744 runtime passes, 8,267 runtime-inapplicable cases, and zero minifier
errors, minified timeouts, or semantic failures.

## Competitive benchmark checkpoint (2026-08-18)

Benchmarking now has three deliberately separate layers:

- `make benchmark` remains the in-process seven-format regression gate;
- `benchmarks/run_css_fixture_benchmark.sh <css-file>...` measures real CSS fixtures via the C++ API;
- `benchmarks/run_css_competitive.py` performs same-host process-inclusive CLI comparisons with pinned esbuild and Lightning CSS binaries and preserves raw samples.

The retained CSS comparison used the GoalSmashers Bootstrap 4, Animate.css 4.1.1 and Tailwind fixtures. Minify++ output was 165,836, 77,454 and 1,973,801 bytes. The public site compares those bytes with the values published by Lightning CSS for esbuild and Lightning CSS, but deliberately does not mix upstream timings from another host/API path with local Minify++ timings. `benchmarks/privatenumber_nift_adapter.ts` is a reference adapter for adding Nift to `privatenumber/minification-benchmarks`; no upstream Nift leaderboard result is claimed until that suite is actually run on its current artifacts. A local 515,136-byte JS integration probe confirmed standalone Minify++ and `nift minify` produced byte-identical output.

Treat this separation as durable methodology: output-size comparisons can be cross-host when the exact input/tool output is fixed, but speed comparisons require the same host and a comparable invocation boundary.

The definitive same-host checkpoint on the i7-12700H host used Minify++ 1.1.0,
esbuild 0.28.2 and lightningcss-cli 1.33.0 for 45 CLI samples after five
warmups. Minify++ had the lowest median CLI latency on all three fixtures;
Lightning CSS produced the smallest output on all three. Raw samples, gzip
sizes, commands and environment are retained in
`benchmarks/results/2026-08-18-css-competitive.json`.

The upstream JavaScript run used Nift 4.0.2 commit `aa60ab3` against
`privatenumber/minification-benchmarks` commit `fe89864f…`. Nift passed all 12
artifacts and five runs each, but ranked last among successful entries under the
upstream size-heavy score because conservative Minify++ output was larger. The
integrated CLI boundary and large-input latency rise are part of the result.
JShrink was unavailable without PHP/Composer and Closure failed without Java;
never describe either as measured successfully on this host.
## 2026-08-18 — memory-safety Checkpoint 2A

- Added a maintained long-lived seven-format API corpus and mixed-file CLI stress harness. At commit `db2a6ff`, 80 ASan/LSan/UBSan corpus iterations completed with zero findings; the 300-iteration native soak stabilized at 7,160 KiB RSS after a 7,096 KiB warm-up observation.
- Sanitized CLI stress passed 8×42 files and the native CLI soak passed 30×70 files, including in-place replacement and controlled mixed valid/invalid batch cleanup. The deterministic sanitizer fuzz corpus also passed 70,000 cases.
- No production Minify++ implementation change was required. The independent Valgrind target remains the Checkpoint 2B exit gate and is still open because Valgrind is unavailable in the current environment.
- Retained machine-readable Checkpoint 2A evidence lives under `docs/evidence/memory-safety-checkpoint-2a-*.json`; keep the public memory-safety page synchronized with those records.
- Nift-owned Minify++ integration stress is deliberately deferred to the cross-project memory checkpoint; standalone/Nift source synchronization remains required here.

## 2026-08-18 — memory-safety Checkpoint 2B complete

- Independent Linux confirmation is complete at Minify++ commit `2a51a38`: Valgrind 3.26.0 ran 30 maintained lifetime-corpus iterations with 0 errors, 0 bytes in use at exit, all 2,448 allocations freed, and no leaks possible. Peak Valgrind process RSS was 184,908 KiB.
- Exact machine-readable evidence is retained at `docs/evidence/memory-safety-checkpoint-2b-valgrind.json`. Together with the clean sanitizer corpus, stable native RSS soak, CLI stress and deterministic fuzz corpus, this closes the standalone Minify++ memory/lifetime checkpoint without a production source repair.
- Proceed next to Nift core lifecycle memory testing. Sustained Nift-owned Minify++/Jsonic++ integration pressure remains deliberately deferred to the later cross-project checkpoint.

## 2026-09-14 — performance campaign CP1–CP6

Performance work now has a checkpoint-level measurement discipline rather than being evaluated only at batch integration. CP1 freezes the acceptance rules; CP2 adds a repeatable API-level raw-sample harness; CP3 adds opt-in stage attribution; CP4 reduces predictable analysis allocation growth; and CP5 replaces linear var/function redeclaration rescans with a per-scope index. CP6 deliberately rejected a broad lexical-resolution cache after it failed to establish a stable representative win.

Diagnostic stage samples showed the retained CP4/CP5 work reducing concrete-syntax/reference-resolution costs on the broad generated probe and scope-graph cost by roughly 15% on the redeclaration stress probe. These synthetic results are not substitutes for the external 12-fixture benchmark. Before the campaign advances, rerun all real bundles with same-run OXC, SWC, esbuild and Terser controls and reject any new post-validation failure or material default-mode regression. Full detail is in `docs/evidence/performance-cp1-6.md`.

## 2026-09-14 — performance campaign CP7–CP12

CP7–CP12 continue the measured performance campaign without changing the certified output contract. Retained production work: linear mangler function-owner propagation, lazy semantic-fact construction for structured mode, and suppression of redundant binding-renamer planning after a successful rename. A class-ancestry shortcut was rejected immediately when smoke testing exposed a self-referential-class correctness regression; scanner/printer changes were also rejected because profiling did not justify them.

Generated seven-run probes show default essentially flat/slightly faster, structured workload-dependent (about 9.5% faster on nested bindings, flat on the aggressive-friendly probe, 4.2% slower on the class-heavy probe), and aggressive about 15–16% faster on all three probes. These are diagnostic only. Before further performance work or optimizer expansion, run the external 12-fixture benchmark with same-run OXC, SWC, esbuild and Terser controls. Any post-validation failure rejects the tranche; investigate aggregate slowdowns above 5% and reject >10% regressions on a large representative fixture unless an explicit measured tradeoff is approved. Full evidence: `docs/evidence/performance-cp7-12.md`.
