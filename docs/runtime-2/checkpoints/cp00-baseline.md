# CP00 — Baseline suite and campaign scaffolding

Status: **complete** (2026-10-17).

- Objective: reproducible baseline every later checkpoint is compared with.
- Scope: `docs/runtime-2/`, `benchmarks/runtime2/run.py`, `tests/runtime2/`
  (test added for CP01 lands in CP01's commits). No product source changed.
- Implementation: stdlib-only Python runner, six groups, golden-output checks,
  JSON-lines output with host fingerprint.
- Compatibility: none affected.
- Tests: runner self-checks every workload's output.
- Performance: n/a.
- Exit criteria: full suite under 20 minutes on 2 vCPU — met (about 13 min);
  raw evidence committed — met.
- Risks: wall-time noise on a shared VM → gates use instruction counts (D-005).
- Deliverables: runner, `benchmarks.md`, `evidence/cp00/baseline.jsonl`,
  campaign docs.
- Commits: (1) campaign docs, (2) runner + evidence.

## Evidence

`evidence/cp00/baseline.jsonl` (v4.12.0 binary built from `ce8cd36`, GCC 13.3
`-O2`). Summary tables in `../benchmarks.md`.

## Reviews

No separate review pass (tooling-only checkpoint). The runner's `ops` numbers
were cross-checked against the earlier `opcost.py`/`pyfn.py` measurements
(same workloads, within 3%) and the design-report fact-check agent.
