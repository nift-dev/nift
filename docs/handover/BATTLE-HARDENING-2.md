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

1. **BH1 — Guarantee registry + baseline map** — ChatGPT implements; DeepSeek reviews/attacks.
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

The implementation-side structural checker and self-corruption precheck may be
committed by ChatGPT. Final BH1 status remains **PENDING REVIEWER** until DeepSeek:

1. attacks the exact committed candidate independently;
2. demonstrates the checker red against a reviewer-authored representative defect;
3. retains that red-run evidence; and
4. either signs off or returns a counterexample for another implementation round.
