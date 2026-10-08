# v4.8 final migration-guidance checkpoint

This is documentation/workflow refinement after the CP-F implementation,
not authorization for another runtime feature. Starting Nift HEAD is
`b11e48b44607bc96898a45c42e66b7a737af2630`.

The canonical playbook now establishes interactive/framework islands before
architecture proof. Nift generation composes with independently prepared React,
Vue, Svelte, Solid, Web Components, vanilla controllers and other browser bundles;
it does not itself compile those frameworks. Choices depend on parity, source
model, accessibility, maintainability, shared state and bundle/runtime cost.
Significant retained/introduced islands and preparation/mounting paths are recorded
in generated STATUS evidence.

The method and STATUS now require complete migration/parity and incremental
correctness, then a profiling/general-optimization campaign, complete parity
revalidation, and final production-equivalent benchmarks. Campaign records include
profiles, changes, before/after evidence, tradeoffs and justified deferrals. The
method forbids benchmark-specific cases, changed workload/corpus, weakened parity,
hidden production/compatibility stages and timing cheaper workflows. It retains
the existing stop-and-report boundary for Nift core changes.

MIGRATION owns detailed methodology; STATUS owns resumable operational state.
README, BASELINE and managed AGENTS receive only concise architecture/pipeline
pointers. PARITY-CONTRACT and canonical HANDOVER already describe their roles and
need no duplicate methodology. Initialization and overwrite/managed-block logic
are unchanged; the CLI diff contains guidance strings only.

Canonical bytes are generated with the existing `scripts/gen_migration_content.py`:
fixture = embedded header = fresh scaffold = website raw/displayed playbook.
The canonical MIGRATION SHA-256 is
`7c2b33af1d3264f06bc5c3959e28f64ddfcb163d310c26cb7a2a230d2a657fce`.

Local certification:

- Existing migration smoke: PASS, including ordering/content, deterministic
  canonical bytes, ordinary init, existing-file safety, AGENTS ownership and
  fresh-scaffold whitespace.
- Full GCC and Clang `make -j2 test`: PASS.
- Changed CLI source under GCC/Clang warnings-as-errors: PASS.
- Static integrity: 283 files, zero findings.
- NRS: 93/93 PASS; the existing migration module was extended, no module added.
- PRS: 12/12 PASS; package and benchmark repositories unchanged.
- Website: 104/104 full build, immediate 104/104 incremental no-op, both canonical
  displays byte-identical, Migrations-page guidance/order PASS, checker mutation
  self-tests PASS.

Repository ownership commits are separate: NRS `8048eb9`, generated public `b51ad13`,
and website stage `560b32b` (public committed before stage, matching gitlink).
Hosted and live certification are recorded in the final closeout after publication;
this local record does not claim those checks complete prematurely.

Once the required hosted/live checks are green, the feature queue is closed.
Next: feature freeze and stabilization, followed by deliberate release preparation.
No additional runtime feature or Release artifact is part of this checkpoint.
