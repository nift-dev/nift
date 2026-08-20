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
candidate `7f8768b`, separate worktree). BH2 closes the two CI-semantic residuals
BH1 carried forward and introduces a shared, auditable outcome contract plus a
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
  Since round 2 the breadth is proven per enforcement job (the job's own matrix,
  unioned with its transitive `needs:` runners), and statically-disabled jobs are
  rejected.

### BH2 red-rule evidence (retained)

`docs/evidence/bh2/` retains:

- `time-family-red-runs.json` — the fixed `/usr/bin/time` guards all go RED
  (SKIP exit 2) when the tool is absent, and green only when they actually measure.
- `bh2-fixtures-report.json` — the scanner rejects 6 injected false-green/vacuous
  fixtures across all five families (exit 1).
- `bh2-reviewer-fixtures-report.json` — the round-1 reviewer's four independent
  fixtures (`sys.exit(0)` skip, dead-code returncode, `false | cat`, plain
  swallowed returncode) are all rejected (exit 1).
- `bh2-qfixtures-report.json` — the round-3 reviewer's three fixtures (dead-alias
  `ignored = rc`, backslash-continued `false | cat`, unreachable
  `if false; then set -o pipefail; fi`) are all rejected (exit 1).
- `bh2-r4-fixtures-report.json` — the round-4 reviewer's two fixtures plus five
  adjacent forms (native multiline pipeline without backslash; same-line
  `printf 'SKIP...'; exit 0`; leading-pipe continuation; bare `exit` after SKIP;
  brace-group SKIP; `&&`-separated SKIP; `|&` pipeline) are all rejected (exit 1).
- `bh2-s1-fixtures-report.json` — the round-5 pipefail-state fixtures (pipeline
  before `set -o pipefail`; `set +o pipefail` before pipeline; conditional
  disable inside `if`; conditional enable only) are rejected (exit 1), while a
  dead-block disable stays GREEN.
- `bh2-s2-path-coverage-red.json` — the round-5 path-coverage red demos: an
  ordered `'!src/**'` inside `paths:` and a `src/*` flat pattern for required
  `src/**` both fail the registry check.
- `bh2-t1-fixtures-report.json` — the round-6 pipefail-execution-environment
  fixtures (`set -o pipefail` in `$( )`, in a `( )` subshell, as a pipeline
  segment, backgrounded) are rejected (exit 1); the redirect form stays GREEN.
- `bh2-t2-path-coverage-red.json` — the round-6 red demo: an inline flow-style
  `paths: ['README.md']` filter sequence fails the registry check instead of
  collapsing into "covers everything".
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
- **Workflow-trigger parsing boundary (carried forward):** the trigger and path
  parsers support the repository's ordinary block-style `on:` and inline
  `on: push` / `on: [push, pull_request]` forms. Other valid GitHub Actions YAML
  shapes fail closed (false red), never false green. That boundary is
  intentionally carried forward rather than claimed fully closed; hardening it
  is BH3/BH5 work.
- **Path-coverage contract boundary (round 3):** coverage is proven structurally
  over static `paths:`/`paths-ignore:` filters for the automatic triggers
  (push / pull_request union). It does not reason about runtime event shape
  (`github.*`, `secrets`, `inputs`, branch/tag filters), nor about
  `pull_request_target`/`merge_group`. The required coverage set — guard refs,
  `src/**`, `docs/guarantees/**`, `Makefile`, and every enforcement workflow —
  is intentionally uniform across CI_GATED / CROSS_PLATFORM_GATED gates.
- **Static-false boundary (round 3):** `_if_statically_false` decides only a
  documented subset of the Actions expression grammar (literals, `always()`,
  `&&`/`||`/`!`, parentheses, always-empty `event_name`); every other shape is
  treated as live. It proves a definite-false subset, not full expression
  semantics.
- **Swallowed-returncode boundary (round 3):** the rule is bounded liveness over
  assignments, not full data-flow: it follows aliases, tuple unpacking, flags and
  dict stores within a file, and stops at function boundaries, control-flow
  joins, and interprocedural calls. A value that gates only *after* crossing a
  function boundary is conservatively treated as inspected at the store site.
- **Shell layout boundary (round 4):** the normalizer understands the
  continuation and compound constructs the scanner's families rely on —
  backslash-newline, trailing/leading `|`/`|&`, `if`/`while`/`for`/`case`
  blocks, and heredoc bodies. It does not fully tokenize shell: a leading `|`
  immediately following an open `case` alternation, quoted strings spanning
  physical lines, and nested `$( ... )` command substitution are approximated
  rather than parsed. Every such approximation fails toward a false red, never a
  false green.
- **SKIP-outcome boundary (round 4):** `exit 0` (or a bare `exit`) declared
  adjacent to a SKIP message is a false green regardless of layout. `exit $?`,
  `exit` with an explicit non-zero code, or a SKIP message followed by later
  commands before the exit are deliberately outside the rule.
- **Pipefail-state boundary (round 5/6):** state is tracked sequentially over
  top-level statements with a conditional-block taint and an execution-
  environment guard (`$( )`, `( )`, pipeline segments and backgrounded commands
  cannot establish parent state). It is not a full shell interpreter:
  within-statement ordering (e.g. `set -o pipefail && pipeline` on one `&&`
  chain), functions called at a different state than their definition point,
  and `trap`/`shopt`/`env`-based option changes are approximated; every
  approximation fails toward a false red, never a false green.
- **Trigger-syntax boundary (round 6):** the trigger parser now distinguishes
  genuinely-unfiltered triggers from unparsed filter syntax (which fails
  closed). Flow-style `paths: [...]` sequences are parsed; the inline mapping
  form `on: {push: {...}}` yields no triggers and is rejected. Other valid YAML
  shapes remain carried-forward false-red boundaries, never false green.
- **Path-coverage boundary (round 5):** coverage is proven structurally over the
  glob language, not by enumerating the current file tree: a tree requirement
  `root/**` is satisfied only by a recursive-tree pattern rooted at `root` (or an
  ancestor), and a negative pattern breaks coverage whenever its language
  overlaps the required tree. GitHub's `branches:`/`tags:`/`types:` narrowing and
  runtime `github.*`/`secrets` conditions remain out of scope for this static
  contract.
- **Outcome-flow boundary (round 5, reviewer observation):** a SKIP message
  followed by an intervening successful statement and then `exit 0`
  (`echo 'SKIP'; true; exit 0`) still escapes both shell and Python pattern
  checks. That is explicitly carried into BH3 (runtime guard mutation /
  outcome-flow), not silently implied closed by BH2's static scanner.

### BH2 reviewer round 1 — returned to implementer

DeepSeek committed round-1 candidate `7f8768b` and ChatGPT independently attacked
it in a detached worktree. No Nift semantic defect was found; four defects in the
integrity machinery itself were reported. The most serious was compositional: the
newly added `static-integrity` CI job ran `make test-guarantee-registry`, which
SKIPs (exit 2) when the website/regression sibling repositories are absent, and
GH Actions only checks out Nift — so the gate failed on a normal checkout and its
`needs: static-integrity` blocked `fast-suites` from ever running. Three scanner
bypasses were also demonstrated with fresh fixtures (`sys.exit(0)` after a SKIP
print; `.returncode` referenced only in dead code; an arbitrary command piped
into `cat`). The enforcement checker accepted a job with `if: ${{ false }}` and
credited OS breadth from a matrix that belonged to a different job. Round-1
findings retained at `docs/evidence/bh2/bh2-reviewer-fixtures-report.json`.

### BH2 implementer round 2 — fixes

- **P1 (CI gate composition):** `check_guarantee_registry.py` gains a `--local`
  single-repository mode (make `test-guarantee-registry-ci`) that asserts
  everything a Nift-only checkout can prove and PASSes, reporting the public-claim
  surface audit as explicitly deferred when sibling repositories are absent. SKIP
  semantics for the full audit are unchanged (still exit 2 without siblings).
  `test-integrity.yml#static-integrity` now runs the local variant, so the gate is
  usable in GH Actions and `fast-suites` can actually run.
- **P2 (scanner hardening):** `test_integrity_check.py` now catches
  `sys.exit(0)` / `exit(0)` after a SKIP print (same-line or next-statement);
  strips statically-dead blocks (`if False:`, `if 0:`, `if None:`, ...) before
  reasoning; requires a subprocess `returncode` to control a failure path via
  word-boundary refs with alias following, and flags bare discarded calls while
  honouring `return`/`yield`/chained use. Shell pipefail detection now flags any
  pipeline whose upstream command is outside the pure-filter set (`false | cat`,
  `nift ... | tee`, `make ... | grep`), not just `grep|wc|head|tail|diff|cmp`.
  All four reviewer fixtures are RED (retained evidence), the original six remain
  RED, and the real trees stay GREEN.
- **P3 (job executability + OS breadth):** the checker rejects a registered
  CI job whose `if:` is statically false (`${{ false }}`, literal false,
  `event_name == ''`, `always() && false`). Cross-platform breadth is now proven
  per enforcement job: the job's own `strategy.matrix.include` runners, unioned
  transitively with the runners of jobs it `needs:`. A matrix that merely exists
  elsewhere in the workflow no longer satisfies the job (demonstrated RED), while
  `compare` still passes because it consumes `corpus` via `needs:`.
- **P4 (trigger parsing):** `workflow_triggers` now also parses inline
  `on: push` and `on: [push, pull_request]` forms; unsupported valid YAML forms
  still fail closed (false red). The remaining false-red boundary is carried
  forward explicitly in the residual list above.

### BH2 reviewer round 2 — returned to implementer

ChatGPT attacked round-2 candidate `c06fa33` in a separate worktree. No Nift
semantic defect was found; three defects in the integrity machinery were
reported (Q1–Q3), so BH2 remains OPEN:

- **Q1 (path-filter coverage, most important):** the registry check never
  looked at workflow `paths:` filters at all — narrowing `test-integrity.yml`
  to `paths: ['README.md']` still PASSed, so a CI_GATED gate could be
  unreachable for changes that break the guarded guarantee. Separately,
  `test-integrity.yml` watched itself but not the other enforcement workflows
  (`nightly-deep.yml`, `checkpoint-10-cross-platform.yml`, `init-targets.yml`,
  `performance-regression.yml`, `distribution-verification.yml`).
- **Q2 (scanner escapes):** `rc = p.returncode; ignored = rc;
  print("PASS: done")` passed — a returncode that only flows into a dead alias
  is not an outcome-controlling use. Shell `false \\\n | cat` escaped the
  pipeline check (physical-line split), and an unreachable
  `if false; then set -o pipefail; fi` counted as pipefail enabled.
- **Q3 (static-false scope):** `if: ${{ false && always() }}` bypassed the
  static-false job detection; the reviewer asked to either strengthen it with a
  conservative boolean evaluator or narrow the claim.

### BH2 implementer round 3 — fixes

- **Q1:** `check_guarantee_registry.py` now machine-checks an enforcement
  path-coverage contract for every CI_GATED / CROSS_PLATFORM_GATED workflow.
  Each such workflow's automatic triggers (push / pull_request union) must cover
  the guarantee's nift `guard_refs`, `src/**`, `docs/guarantees/**`, `Makefile`,
  and every enforcement workflow referenced anywhere in the registry.
  Minimatch semantics are implemented statically (`**` crosses `/`, `*` does
  not) with `paths:`/`paths-ignore:` parsed per trigger, including the block and
  inline `on:` forms. Narrowing to `paths: ['README.md']` is demonstrated RED
  (retained). All four automatic-gated workflows gained the missing paths.
- **Q2:** the swallowed-returncode rule now follows value flow through
  assignments (aliases, tuple unpacking, comparison flags, dict stores) and
  counts a reference as inspection only when it reaches a **live** use — a line
  that is neither a pure print nor a dead store — so `ignored = rc` (never read
  again) can no longer green a guard while `data = {..., "exit": p.returncode}`
  feeding a later check still does. Shell analysis joins backslash-continued
  logical lines before splitting pipelines, and strips statically-dead
  `if false; then ...; fi` / `while false; do ...; done` blocks (preserving
  `else`/`elif` branches) before pipefail detection.
- **Q3:** `_if_statically_false` is now a conservative three-valued boolean
  evaluator over literals, `always()`, `&&`/`||`/`!`, parentheses, and the
  always-empty `event_name` shape. Only provably-false expressions reject a
  job; anything ambiguous is treated as live (never a false rejection).
  `${{ false && always() }}` is now caught; `always()`, `success()`,
  `github.event_name == 'push'` remain live.

All round-1 and round-2 fixtures stay RED, the three new round-3 reviewer
fixtures are RED (retained at `docs/evidence/bh2/bh2-qfixtures-report.json`),
and the real trees stay GREEN (42 tests/scripts files + 21 regression-suite
contract files, 0 findings). The full 3-repository audit and the CI-equivalent
single-repo sequence both PASS.

### BH2 reviewer round 3 — returned to implementer

ChatGPT attacked round-3 candidate `7d2621b` in a separate worktree. Q1–Q3 were
accepted as materially fixed and the untouched candidate's local registry check
was confirmed. Two new false-green holes in the shell scanner were reported
(R1, R2), so BH2 remains OPEN:

- **R1 (source-layout equivalence):** the backslash-continuation fix closed the
  specific example but not the invariant. Bash does not require a backslash
  after a pipe: `false |` newline `cat >/dev/null` is one pipeline, yet the
  scanner reported 0 findings because `_logical_lines()` joined only
  backslash-continued physical lines.
- **R2 (same-line SKIP + green):** `printf '%s\n' 'SKIP: missing'; exit 0` — a
  SKIP declaration whose outcome is green on the same physical line — was
  missed because `SH_SKIP_EXIT0` expected the `exit 0` on the following line.
  The reviewer emphasised the SKIP→green property must not depend on line
  layout, and pointed at ordinary compact prerequisite handlers such as
  `command -v foo >/dev/null 2>&1 || { echo "SKIP..."; exit 0; }`.

### BH2 implementer round 4 — fixes

- **R1 (shell pipeline layout):** the shell normalizer now rebuilds the logical
  commands the shell actually runs. Comments are stripped (quote-aware),
  heredoc bodies are skipped as data, and physical lines are joined across
  backslash-newline and across a trailing or leading `|`/`|&` pipeline operator
  (with a `case`-block guard so a leading `|` in a case pattern is not joined).
  `_split_pipeline` now splits `|` and `|&` while leaving `||` intact. Every
  shell rule reasons over the same normalized text, so a pipeline is analyzed
  as a whole regardless of where the newlines fall.
- **R2 (SKIP outcome layout):** `SH_SKIP_EXIT0` is now layout-independent: the
  SKIP statement and the green outcome may be separated by `;`, newlines,
  `{`/`}` braces, or `&&`/`||` on one line or many, and the green outcome may be
  `exit 0` or a bare `exit` (whose status is the preceding successful command's
  0). `exit 1`/`exit 2` remain untouched. The SKIP and missing-tool checks now
  run over the same dead-block-stripped, comment-stripped normalized text; the
  Python SKIP/prereq/vacuous checks were aligned onto the dead-block-stripped
  body for the same consistency.

All seven round-4 fixtures (the reviewer's two plus five adjacent forms:
leading-pipe continuation, bare `exit` after SKIP, brace-group SKIP,
`&&`-separated SKIP, `|&` pipeline) are RED, retained at
`docs/evidence/bh2/bh2-r4-fixtures-report.json`. All prior fixtures stay RED and
the real trees stay GREEN (42 + 21 files, 0 findings); registry CI, the full
3-repository audit, BH1 liveness, the CI-equivalent chain, and the time-family
SKIP(2) red-runs all pass.

### BH2 reviewer round 4 — returned to implementer

ChatGPT attacked round-4 candidate `88f1158` in a detached worktree. R1 and R2
were accepted as closed. Two new false-green families were reported (S1, S2),
so BH2 remains OPEN:

- **S1 (pipefail state, not presence):** `false | cat >/dev/null` followed later
  by `set -o pipefail` scanned GREEN (the setting cannot protect an earlier
  pipeline), and `set -o pipefail` followed by `set +o pipefail` then
  `false | cat` scanned GREEN (pipefail explicitly disabled before the tested
  pipeline). Both run green. The scanner reduced pipefail to "a live
  `set ... pipefail` occurs somewhere", not "pipefail is active when this
  pipeline executes".
- **S2 (GitHub `paths:` negation + pattern superset):** adding `'!src/**'` after
  `'src/**'` in both trigger `paths:` lists still passed the registry check,
  even though GitHub excludes `src/**` from triggering. Separately, replacing
  `src/**` with `src/*` also passed, because the checker matched the literal
  string `"src/**"` (`*` matching the two `*` characters) rather than proving
  the flat pattern covers the tree.

### BH2 implementer round 5 — fixes

- **S1:** `_shell_unsafe_pipelines` now tracks pipefail as sequential state over
  the normalized statements, quote- and `$( )`-aware. `set -euo pipefail` /
  `set -o pipefail` enable; `set +euo pipefail` / `set +o pipefail` disable;
  only unconditional top-level `;`/newline statements are trusted. A pipefail
  change inside a conditional block (`if`/`while`/`for`/`case`) or on the
  right-hand side of `&&`/`||` may never execute, so it taints the enclosing
  scope to "not provably active" (conservatively off). Subshells and function
  definitions do not leak state out. Each pipeline is flagged unless pipefail is
  provably active at its own execution point; statically-dead blocks are still
  stripped first, so `if false; then set +o pipefail; fi` does not disable it.
- **S2:** the path-coverage check now models GitHub's ordered negative `paths:`
  semantics: a file triggers only if it matches a positive pattern and no
  negative (`!`-prefixed or `paths-ignore:`) pattern. Required patterns are
  compared as languages, not literal strings: a literal file is matched
  concretely, and a tree requirement `root/**` is covered only by `**` or a
  `proot/**` whose root is the same or an ancestor (so `src/*` no longer
  satisfies `src/**`). A negative that overlaps any file of a required tree
  (decided by a bounded prefix-matching automaton over the glob) breaks the
  coverage contract.

All five S1 fixtures are RED (pipefail-after, disabled-before, conditional
disable, conditional enable; the dead-block disable stays GREEN as it should),
retained at `docs/evidence/bh2/bh2-s1-fixtures-report.json`. Both S2 mutations
(`!src/**` negation, `src/*` flat pattern) are demonstrated RED, retained at
`docs/evidence/bh2/bh2-s2-path-coverage-red.json`. All prior fixtures stay RED
and the real trees stay GREEN (42 + 21 files, 0 findings); registry CI, the
full 3-repository audit, BH1 liveness, and the CI-equivalent chain all pass.

### BH2 reviewer round 5 — returned to implementer

ChatGPT attacked round-5 candidate `689cd45` in a detached worktree. S1 and S2
were accepted as materially closed. Two new false greens were reported (T1, T2),
so BH2 remains OPEN:

- **T1 (pipefail state leaks out of its execution environment):**
  `x=$(set -o pipefail)` then `false | cat >/dev/null` scanned GREEN and ran
  green. `set` inside `$( )` command substitution runs in a subshell and cannot
  enable parent-shell pipefail, but `_PF_ON.search` promoted the state anyway.
  The adjacent `set -o pipefail | cat >/dev/null` (the `set` as a pipeline
  segment, also a subshell) had the same effect. This contradicted the round-5
  handover's own claim that the analysis is `$( )`-aware and subshell state
  does not leak.
- **T2 (unparsed filter collapses into unrestricted):** replacing both
  automatic trigger filters with the inline flow sequence
  `paths: ['README.md']` still passed the registry check. `_trigger_configs`
  only parsed the block form, so the trigger stayed `(None, None)`, and
  `_trigger_covers` treated `(None, None)` as "covers everything" — the exact
  opposite of fail-closed. "Unsupported/unparsed filter" and "no filter exists"
  were represented identically.

### BH2 implementer round 6 — fixes

- **T1:** a `set ... pipefail` now establishes parent-shell state only when its
  execution environment can actually mutate that state. `_set_establishes_pipefail`
  rejects a `set` inside `$( )` command substitution, inside a `( )` subshell,
  as a pipeline segment (each segment runs in a subshell), or backgrounded with
  `&`. A `set` that cannot establish state is ignored for state purposes (the
  state stays unchanged) and the statement still falls through to the normal
  pipeline check. A redirect (`set -o pipefail >/dev/null`) still establishes,
  because a redirect does not create a new shell.
- **T2:** `_trigger_configs` now parses flow-style `paths: ['a', 'b']` /
  `paths-ignore: [...]` sequences, and each trigger's config is a three-state
  value: "unfiltered" (genuinely no filter — covers everything), "filtered"
  (parsed filters), and "unparsed" (a filter construct the parser could not
  understand — fails closed, covers nothing). `_trigger_covers` treats only
  "unfiltered" as unrestricted; "unparsed" never covers a required path. The
  inline map form `on: {push: {...}}` still yields no triggers and is rejected
  by the not-automatic check.

All five T1 fixtures are RED (command-substitution `set`, pipeline-segment
`set`, subshell `set`, backgrounded `set`; the redirect form stays GREEN as it
should), retained at `docs/evidence/bh2/bh2-t1-fixtures-report.json`. The T2
inline `paths: ['README.md']` mutation is demonstrated RED, retained at
`docs/evidence/bh2/bh2-t2-path-coverage-red.json`, while an equivalent
flow-style full-coverage `paths:` still PASSes. All prior fixtures stay RED and
the real trees stay GREEN (42 + 21 files, 0 findings); registry CI, the full
3-repository audit, BH1 liveness, and the CI-equivalent chain all pass.

### BH2 handoff to reviewer

**Round-6 candidate: `389a4ac`** ("BH2 round 6: fix reviewer findings T1-T2
on top of 689cd45"). Reviewer attacks the exact round-6 commit in a separate
clone/worktree and reports findings back; the implementer does not modify the
tree during review. No changes are made to `7f8768b`, `c06fa33`, `7d2621b`,
`88f1158`, `689cd45` or the round-6 candidate `389a4ac` itself.

Review focus areas (attack each):

- **Exit-code contract:** inject a guard that prints SKIP then `exit 0`, or
  `SystemExit(0)`/`sys.exit(0)` after `print("SKIP...")`, and confirm the scanner
  rejects it (`skip-as-pass`), while legitimate `finish(2)`-style guards pass.
- **Swallowed-returncode (round-2 hardening):** a subprocess result whose
  `returncode` appears only in dead code (`if False:`), only in a `print()`, or is
  never referenced at all must be flagged; a result that gates an `if`/`assert`/
  `finish`/`raise`, or is handed up via `return`, must not be. Watch for regressions
  on the real checkpoint scripts.
- **Pipefail breadth (round-2 hardening):** any pipeline without pipefail whose
  upstream command is outside the pure-filter set (`false | cat`,
  `nift ... | tee`, `make ... | grep`) must be flagged; pure-transform pipelines
  (`printf | sed`) under pipefail must stay green.
- **`/usr/bin/time` family:** with GNU time absent, each fixed guard must exit 2
  (not 0, not a traceback). With a too-small `--time-bin`, `memory_10k_benchmark.py`
  and `memory_safety.py` must go red rather than silently green.
- **Timeout classification:** a hung Nift subprocess in checkpoint 3/4/6/7 must
  surface as exit 124 with an outcome line, not an unbounded wait or raw traceback.
- **CI composition (P1):** from a Nift-only checkout (no website/regression
  siblings) the full CI sequence must pass — scanner, then
  `make test-guarantee-registry-ci` — while `make test-guarantee-registry` still
  SKIPs (exit 2). The full 3-repository audit must still PASS.
- **Job executability (P3):** a registered CI_GATED/SCHEDULED job with
  `if: ${{ false }}` (or literal false / `event_name == ''`) must fail the registry
  check; every registry ref must point at a job that exists and is live.
- **OS breadth ownership (P3):** a job that only inherits a matrix from an
  unrelated job (no own matrix, no `needs:`) must fail; `compare` must still pass
  via `needs: corpus`; single-OS matrices must fail.
- **Trigger parsing (P4):** inline `on: push` / `on: [push, pull_request]` must be
  recognized; unsupported valid YAML forms must fail closed (never false green).
- **New workflow validity:** `test-integrity.yml` and `nightly-deep.yml` must be
  valid YAML with sensible triggers; a `core-memory` job that does not actually run
  checkpoint 3 against a sanitized build is a defect.

The next active checkpoint is **BH3 — Guard mutation and liveness**, with the
remaining queue re-planned after BH2 review.
