# Nift Battle Hardening campaign — guarantee durability

## Campaign contract

The second deliberate hardening campaign starts from public Nift v4.0.4 with
v4.0.5 as the development line. The campaign does not reset the evidence from
Checkpoints 0–10. Its purpose is to make important guarantees increasingly hard
to break without the project noticing.

The operating rules are:

- one model implements a checkpoint and the other independently reviews it;
- the reviewer does not silently fix findings in the authoritative workspace;
- the reviewer works from the exact committed candidate in a separate clone or worktree;
- PASS, FAIL, SKIP and UNSUPPORTED are distinct outcomes;
- new or newly promoted guards are not trusted until they have gone red on a
  reviewer-authored representative defect and the red-run is retained;
- serious defects may reorder the campaign immediately;
- evidence and public wording must stay scoped to what was actually established;
- checkpoint infrastructure is time-boxed and must not become the purpose of the campaign;
- semantic contract changes and public-claim downgrades require maintainer approval;
- every checkpoint ends by reconciling evidence and replanning the remaining queue.

## Current checkpoint queue

The queue is provisional and must be reconsidered after every checkpoint:

1. **BH1 — Guarantee registry + baseline map — CLOSED / VERIFIED** — ChatGPT implemented; DeepSeek independently reviewed/attacked and signed off candidate `8419cec`.
2. **BH2 — Test integrity + enforcement architecture** — DeepSeek implements; ChatGPT reviews/attacks.
3. **BH3 — Curated guard mutation / test-of-test** — roles reverse per guard.
4. **BH4 — Incremental/state-transition adversarial II** — DeepSeek implements; ChatGPT reviews/attacks.
5. **BH5 — Parser/value/composition adversarial** — ChatGPT implements; DeepSeek reviews/attacks.
6. **BH6 — Init/starter/AI-context functional truth** — DeepSeek implements; ChatGPT reviews/attacks.
7. **BH7 — Persistence/crash/recovery II** — ChatGPT implements; DeepSeek reviews/attacks.
8. **BH8 — Performance/complexity invariants** — DeepSeek implements; ChatGPT reviews/attacks.
9. **BH9 — Platform/filesystem boundaries** — ownership chosen after BH8.
10. **BH10 — Claims/evidence/public-truth reconciliation** — DeepSeek leads the audit; ChatGPT implements reconciliations; DeepSeek signs off.

## BH1 — Guarantee registry + baseline map

BH1 introduces `docs/guarantees/registry.json` as a deliberately small, versioned
map from guarantees and significant public reliability claims to:

- scope and state;
- retained evidence;
- guard locations;
- enforcement class;
- platform scope;
- test-of-test status;
- accepted limitations and known unestablished areas.

The registry is machine-checked by `scripts/check_guarantee_registry.py`. The
checker is intentionally structural: it verifies registry shape, unique IDs,
repository references, retained-evidence paths, declared workflow/job references,
public-claim source needles when sibling repositories are available, source hashes
for the four reliability pages manually audited in BH1, and cross-links from claims
to guarantees. A changed page hash invalidates the mapping and forces a fresh claim
audit. The checker does **not** attempt to decide whether natural-language wording
overstates the evidence; BH10 keeps that semantic audit human/model reviewed instead
of hiding it in brittle heuristics.

Run the local structural check with:

```bash
make test-guarantee-registry
```

When the sibling website and external regression repositories are present, run
the full BH1 local precheck with:

```bash
make bh1-guarantee-registry
```

The latter also runs representative implementer-side corruptions. These are only
prechecks: BH1 is not closed until DeepSeek independently attacks the committed
checker and supplies the reviewer-authored liveness red-run required by the
campaign contract.

### Known issues seeded on day one

BH1 intentionally records rather than conceals the known open mismatches:

- the Battle Tested page contains incompatible Minify++ generated-JavaScript
  counts (`379` and `15,459`);
- Getting Started says to add a “provided” `ai-context.txt`, while the current
  starter/init flow does not provide that file;
- `memory_10k_benchmark.py` can exit successfully without measuring anything if
  `/usr/bin/time` is absent;
- several retained checkpoint/contract families are not yet continuously gated
  on every relevant change.

BH2/BH6/BH10 own those families unless a finding is severe enough to jump the queue.

### BH1 completion state

**CLOSED / VERIFIED.** DeepSeek independently attacked exact round-3 candidate `8419cec`, demonstrated the checker red against 12 reviewer-authored corruption cases, retained the reconciled reviewer evidence at `docs/evidence/bh1/registry-reviewer-round3.json`, verified the clean registry remained green, and signed off BH1. The structural checker meta-guard is therefore promoted to `VERIFIED_GUARD`.

### BH1 reviewer round 1 — returned to implementer

DeepSeek independently attacked candidate `a1bb6dd` and correctly kept BH1 open.
Its reviewer-authored liveness injections proved four checker paths red, but it
also found six false-green structural classes and one significant claim omission.
The retained review transcript summary is
`docs/evidence/bh1/registry-reviewer-round1.json`.

Implementation round 2 closes those reported classes structurally:

- `RETAINED`/`CAMPAIGN` evidence classes may no longer have empty `evidence_refs`;
- public claims and their referenced guarantees must both remain `ESTABLISHED`;
- `VERIFIED_GUARD` now requires a retained JSON red-run evidence reference;
- `make ...` manual enforcement commands must name a real target, including `-C`
  component Makefiles;
- `guard_refs` are restricted to executable/test artifacts in Nift or the external
  regression suite; a passive website/document page cannot masquerade as a guard;
- missing sibling repositories produce explicit `SKIP` with exit 2, never a green
  PASS that counts unvalidated public claims; and
- pagination incremental-vs-clean equivalence is now a first-class guarantee with
  its public 18-comparison claim mapped. The round also maps the quantitative
  subclaims identified during review (incremental sequence shape, shared-data
  concurrency fixture, parameter-interpolation checkpoint, integration fixture,
  watch/RSS figures, and component RSS soaks) rather than leaving them silently
  outside the manually audited surface inventory.

Two guards previously labelled `VERIFIED_GUARD` were deliberately downgraded to
`UNPROVEN`: their historical failure demonstrations were real, but BH1 had no
retained red-run artifact to which the registry could point. BH3 can promote them
again only after retained liveness evidence exists. This is an intentional
application of the campaign rule: **red before trusted green**.

BH1 remains **OPEN / PENDING REVIEWER ROUND 2**. DeepSeek must re-attack the exact
round-2 commit; the implementer does not self-close the checkpoint.

### BH1 reviewer round 2 — returned to implementer

DeepSeek re-attacked exact candidate `6ffc0b0`. It confirmed C1–C7 closed, then
found four additional false-green/schema-integrity classes: enforcement tiers
could cite workflows whose triggers did not match the claimed tier; arbitrary
non-`make` MANUAL commands were accepted without structural validation; public
claims could cite pages outside the hash-pinned audited surface set; and a guard
script could masquerade as `RETAINED` evidence.

Implementation round 3 closes P1–P4 without broadening BH1 into BH2:

- `CI_GATED` and `CROSS_PLATFORM_GATED` workflow refs must expose an automatic
  `push` or `pull_request` trigger; `RELEASE_GATED` must expose a release-capable
  trigger; `SCHEDULED` must expose `schedule`. Deeper path/job-condition coverage
  remains BH2 work.
- BH1's `MANUAL` enforcement contract is intentionally restricted to the simple
  `make` / `make -C` forms the registry actually uses, and the referenced target
  must exist. BH1 does not pretend to validate arbitrary shell commands.
- every public claim source must be one of the SHA-256-pinned
  `public_claim_surfaces`, closing the future-claim anti-drift hole.
- `RETAINED`/`CAMPAIGN` now requires a JSON artifact under a `docs/evidence/`
  path; executable `guard_refs` cannot stand in for completed-run evidence.

The four existing guarantees that had incorrectly labelled their guard as
retained evidence (`contracts.namespace-reservation`, `embedded.jsonic-sync`,
`pagination.incremental-clean-equivalence`, and
`templating.parameter-interpolation-contract`) were corrected honestly: their
`RETAINED` class and bogus evidence refs were removed rather than fabricating
retrospective run artifacts. Their executable guards and enforcement remain
mapped; BH3 may promote retained/test-of-test status after actual evidence is
captured.

The round-3 implementer liveness artifact is
`docs/evidence/bh1/registry-liveness-implementer-round3.json`; all 15 corruption
cases, including direct P1–P4 reproductions, go red. The full sibling-workspace
structural check is green.

BH1 reviewer round 3 subsequently attacked exact candidate `8419cec` and signed off.
The independent battery put all 12 reviewer-authored corruptions red while the clean
registry stayed green. DeepSeek also independently reconciled the retained campaign
figures used by the registry: 16 Checkpoint-8 filesystem cases, 720 Checkpoint-7
incremental-vs-clean comparisons, 18 Checkpoint-10 portable contracts, 1,217
Checkpoint-9 parser/resource cases, and 19 Checkpoint-6 Valgrind invocations.

### BH1 final reconciliation — CLOSED / VERIFIED

BH1 is complete. Reviewer evidence is retained in
`docs/evidence/bh1/registry-reviewer-round3.json`, and the registry structural checker
meta-guard is promoted to `VERIFIED_GUARD`. No checker implementation was changed
during closure reconciliation.

Three residual boundaries are deliberately carried forward rather than hidden:

- **BH2:** workflow-trigger parsing is intentionally structural and currently accepts
  the repository's ordinary block-style YAML subset. Unsupported valid YAML forms fail
  closed (false red), not green; BH2 owns CI parser/enforcement robustness.
- **BH2:** `CROSS_PLATFORM_GATED` proves a referenced workflow has an automatic trigger,
  but BH1 does not prove that the referenced job really spans Linux/macOS/Windows. BH2
  owns deeper workflow/job/path/condition enforcement semantics.
- **BH3:** `VERIFIED_GUARD` requires retained red-run evidence structurally; proving the
  artifact is semantically the matching guard mutation remains BH3 test-of-test work.

The next active checkpoint is **BH2 — Test integrity + enforcement architecture**,
implemented by DeepSeek and independently attacked by ChatGPT. Re-plan the remaining
queue after BH2 rather than treating the list above as immutable.

## BH2 — Test integrity + enforcement architecture

**Implementer:** DeepSeek (this session). **Reviewer/attacker:** ChatGPT (exact
candidate, separate worktree). BH2 closes the two CI-semantic residuals BH1
carried forward and introduces a shared, auditable outcome contract plus a
static false-green scanner.

### Outcome contract

`scripts/guard_outcome.py` defines the exit-code contract every Nift guard uses:

| code | meaning |
|------|---------|
| 0 | PASS — assertions executed and held |
| 1 | FAIL — assertions executed and broke |
| 2 | SKIP — prerequisite/tool absent; **not** success |
| 3 | UNSUPPORTED — platform/toolchain cannot run this guard |
| 124 | TIMEOUT — guard exceeded its time budget |

Consumers treat 0 as the only green code. SKIP is deliberately non-green.

### `/usr/bin/time` silent-success family (fixed)

The known BH1-era defect `tests/memory_10k_benchmark.py` printed `SKIP` and then
exited 0 — a silent green that measured nothing. The family is now non-green:

- `tests/memory_10k_benchmark.py` — missing `/usr/bin/time` → `SKIP` exit 2;
  runtime timeouts → `TIMEOUT` exit 124; added `--time-bin`/`--timeout-seconds`.
- `scripts/memory_safety.py` — RSS mode with no `/usr/bin/time` → `SKIP` exit 2
  (it can no longer claim a PASS with zero RSS samples); per-run timeout → 124.
- `scripts/checkpoint4_large_project.py` — missing `/usr/bin/time` → `SKIP`
  exit 2 instead of an unclean traceback; per-run timeout → 124.
- `scripts/checkpoint3_core_memory.py`, `scripts/checkpoint6_integration.py`,
  `scripts/checkpoint7_incremental_equivalence.py` — every Nift subprocess now
  has a timeout; a hang is `TIMEOUT` (124), never an unbounded CI hang.

### Static false-green scanner

`scripts/test_integrity_check.py` (make `test-test-integrity`) is a dependency-
free static scanner that flags five false-green families across `tests/` and
`scripts/`:

- `skip-as-pass` — SKIP message immediately followed by `exit 0`/`SystemExit(0)`
- `prereq-exit0` — a missing-tool guard exits 0
- `pipefail-missing` — a shell test pipelines without enabling pipefail
- `swallowed-returncode` — a subprocess result whose returncode is never checked
- `vacuous-pass` — a guard that can only ever print PASS (tests nothing)

It is wired into `.github/workflows/test-integrity.yml` and the `bh2-test-integrity`
make target.

### Enforcement tiers (registry updated)

`docs/guarantees/registry.json` now carries 14 CI job refs (was 7):

- `contracts.namespace-reservation`, `templating.parameter-interpolation-contract`,
  `pagination.incremental-clean-equivalence` → `CI_GATED` on the new
  `.github/workflows/test-integrity.yml#fast-suites` job (each suite is ~0.2 s).
- `incremental.clean-build-equivalence`, `parser.controlled-mutation-failure`,
  `memory.nift-lifecycle` → `SCHEDULED` on the new `.github/workflows/nightly-deep.yml`
  (checkpoint 9 fuzz, checkpoint 3 core lifecycle, checkpoint 4 watch endurance,
  checkpoint 7 equivalence).
- `scripts/check_guarantee_registry.py` gains `CROSS_PLATFORM_GATED` OS-breadth
  enforcement: a cross-platform workflow must list concrete `runner:` entries in a
  `strategy.matrix.include` covering every declared platform, closing the BH1
  residual that a trigger alone did not prove real Linux/macOS/Windows coverage.

### BH2 red-rule evidence (retained)

`docs/evidence/bh2/` retains:

- `time-family-red-runs.json` — the fixed `/usr/bin/time` guards all go RED
  (SKIP exit 2) when the tool is absent, and green only when they actually measure.
- `bh2-fixtures-report.json` — the scanner rejects 6 injected false-green/vacuous
  fixtures across all five families (exit 1).
- `bh2-clean-report.json` — the scanner is clean on the real `tests/` + `scripts/`
  (42 files, 0 findings), and clean on the regression-suite contract scripts.

### Residual boundaries after BH2

- **BH3:** guard mutation and semantic liveness — proving a guard's red evidence
  is the *right* guard mutation for the guarantee, not just *a* red run.
- **BH6:** onboarding artifacts are still not proven to match the registry.
- **BH8:** performance invariants are not yet their own guard family.
- The scanner is static and pattern-based; it cannot reason about a guard's runtime
  semantics. A future guard that skips at runtime (after passing the static scan)
  is a BH3 mutation target.

The next active checkpoint is **BH3 — Guard mutation and liveness**, with the
remaining queue re-planned after BH2 review.
