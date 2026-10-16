# Nift Runtime 2.0 campaign

Goal: replace Nift's textual evaluator with one compiled engine (parser → AST →
register bytecode → VM) shared by scripts, the shell and templates, while
keeping website builds at least as fast and as small as v4.12, and removing
mandatory project-root filesystem confinement.

The campaign is complete only when the legacy textual evaluator is deleted
(checkpoint CP25) and the end-to-end evidence in CP27 exists.

## Read in this order

| File | Purpose |
| --- | --- |
| `progress.md` | Current state, last completed checkpoint, next action. **Start here when resuming.** |
| `checkpoints.md` | Dependency-ordered checkpoint matrix with exit gates. |
| `decisions.md` | Owner decisions and campaign-level technical decisions (append-only). |
| `architecture.md` | Target architecture in one page. |
| `semantics.md` | Observed language semantics the new engine must honour, and proposed changes. |
| `compatibility.md` | Public interfaces and behaviours that must not change, and approved breaks. |
| `benchmarks.md` | Baseline suite, measurement rules, and where raw evidence lives. |
| `checkpoints/cpNN-*.md` | Per-checkpoint plan, evidence and review record. |
| `evidence/` | Raw benchmark and test output, one directory per checkpoint. |

## Operating model

Nift is developed by agents. A checkpoint passes on evidence, not on human
sign-off: tests, differential parity, sanitizers where relevant, benchmarks
against pre-registered gates, and recorded review passes. Reviews by the same
model are labelled as such and are not called independent. Escalate to the
owner only for the triggers listed in `decisions.md` (D-000).

Git: work on branch `runtime-2`; one or more local commits per checkpoint; never
push, tag, release or rewrite published history without owner approval.

## Source design documents

- *Nift v4.12.0 — Independent Engineering Review* (Claude, 2026-10-17).
- *Nift Runtime 2.0 — Execution Engine Design Report* (Claude, 2026-10-17).

Both are Claude Docs artifacts owned by the project owner. Their conclusions
that matter for execution are condensed into `architecture.md`.
