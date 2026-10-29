# CP25-R — release readiness scope (D-057)

Status: scope recorded 2026-10-29 from the owner's decision. Gates G1–G6
approved in principle by the owner (2026-10-29), with the G2 refinement below.
They do not change during measurement.

CP25-R is **not** CP25. The original CP25 is now CP25-L (D-057), unchanged
and deferred behind CP24 (D-053). Nothing here claims or implies CP25-L
progress beyond what is measured.

## 1. Foundation

- Implementation base: the local CP19 + CP23 integration candidate `38d4d87f`
  (branch `cp23-integration`), pending its focused independent review.
- CP25-R work lives on branch `cp25-fallback-retirement`, one attributable
  commit per refusal family, separate from any CP19 performance or portability
  correction (comparator sort included).

## 2. Scope

**In scope:**
- Script entry points: `nift <file>.f`, `nift eval`, `-c` style entry, and
  modules imported by scripts.
- Persistent shell sessions (CP19, D-054).
- The refusal inventory for the release script corpora (section 4).

**Out of scope for CP25-R:**
- Templates, pages and hooks. They stay on the legacy renderer (D-053, CP17
  tripwire).
- Deleting the legacy evaluator (CP25-L).
- Default engine routing (D-057 item 7).
- Performance optimisation work.

## 3. Gates (approved in principle)

**G1 Inventory.**
- Every refusal reason produced on the release script corpora is listed,
  with its count, its affected entries and one class from section 5.
- Every class other than "missing VM capability" or "compiler/admission
  limitation" needs an owner-visible justification.
- The inventory is regenerated from the candidate binary by a committed tool.
  It is not hand-maintained.

**G2 Fallback (owner refinement, 2026-10-29).** On the release script
corpora, a non-VM execution is acceptable only when its reason is:
- explicitly classified (section 5);
- justified by the documented language or implementation contract;
- individually or categorically approved by the owner;
- covered by a test that verifies the fallback (or refusal) is intentional.

The existing fallback cases are **not** grandfathered: each must meet all four
conditions or be removed by genuine VM support. No "missing VM capability" or
"compiler/admission limitation" reason remains unless the owner defers it by
name. No admission guarantee is weakened. Measured over one full test cycle on
the release candidate.

**G3 Equivalence.** Each eliminated refusal has a committed legacy/VM
differential (`tests/runtime2/cp25/legacy_vm_diff.py`). It requires:
- byte-identical exit status, stdout and stderr;
- counted VM-only execution (one VM execution, zero legacy);
- coverage of value kinds and contexts: top level, function, loop,
  loop-reached function, and try;
- a negative control: the pre-change binary must fail the same differential.

**G4 No weakening.**
- Existing refusal-before-effects witnesses stay green.
- A test that asserted a refusal is updated only to assert genuine VM
  execution with parity, never deleted or loosened.
- Admission rules change only where the VM implements the behaviour.

**G5 Neighbours.** The following stay green:
- `test-rt-vm`, the VM corpus, CP19 and CP23 certification gates;
- CP13 diagnostics, CP17 resources, CP22 admission;
- lint and the certification guard.

**G6 Non-regression.**
- Website build output and performance are untouched: no template or renderer
  change.
- Local instruction counts on the VM scripting workloads do not regress
  beyond 1% without an explanation.
- Wall-clock confirmation stays with the reference-host release campaign.

## 4. Release script corpora

| Corpus | Contents |
|---|---|
| `tests/runtime2/vm-corpus` | 123 `.f` |
| `tests/runtime2/semantics` | 57 `.f` |
| `tests/runtime2/rt-diff-corpus` | 15 `.f`, plus 8 site/workspace entries |

- The site/workspace entries are CP24-dependent for their template steps.
- Also in scope:
  - scripts extracted from smoke-test heredocs (`rt-diff --extract-smoke tests`);
  - Labs scripts;
  - package scripts, where present locally.
- Not available locally:
  - the regression suite repository;
  - the package repositories.

  They join the inventory when present on the certification host.

## 5. Classification

Each refusal reason gets exactly one class:

| Class | Meaning |
|---|---|
| **M** | Missing VM capability: the VM lacks the behaviour |
| **A** | Compiler/admission limitation: the VM could run it, but the compiler cannot yet prove legacy-equivalent meaning |
| **I** | Intentional semantic refusal: an approved difference, or a refusal-before-effects boundary |
| **T** | Dependent on deferred CP24 (templates, pages, hooks) |
| **P** | Toolchain or platform portability (e.g. comparator `sort`) |
| **H** | Test-harness or fixture limitation |
| **X** | Outside the release contract |

Families are addressed together where one cause explains several reasons.
For example, statically certain legacy run-time Errors share one mechanism:
an Error raised when execution reaches it, instead of refusing the unit.
