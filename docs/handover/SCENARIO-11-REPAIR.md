# Scenario 11 repair campaign

## Objective
Repair the legacy zero-mutation scenario 11 gate independently of the v4.1 template-language campaign. Preserve the invariant that a failed reconcile after a recovery-relevant derived deletion retains `.nift/.unfinished`, ordinary builds refuse, and `build --repair` recovers.

## Baseline finding
CP24 established that the same scenario-11 failure reproduces at v4.0.13, CP9, and CP24. The failure is therefore not caused by the v4.1 language work.

## Plan
- R11-0: freeze campaign and reproduce/root-cause the failure.
- R11-1: make the failure injection deterministic and privilege-independent; preserve production semantics.
- R11-2: rerun zero-mutation, repair, ownership, watch/incremental and full available certification gates; close campaign.

## R11-0 status
- commit: pending
- semantics changed: none
- tests added: none
- gates run: scenario 11 reproduced in CP24 evidence and re-inspected here
- performance result: not applicable
- rejected alternatives: treating chmod(0555) as a deterministic write failure; root/capability-bearing runners can still write
- next checkpoint: R11-1 deterministic watch-state-save failure seam
