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

## R11-1 status
- commit: recorded by the R11-1 checkpoint commit
- semantics changed: none in production; added an environment-gated test seam immediately before watched `tracked.json` save
- tests added: scenario 11 now injects `NIFT_TEST_WATCH_STATE_SAVE_FAIL=1` instead of relying on directory mode bits
- gates run: `make test-zero-mutation` — all 16 scenarios PASS, including every scenario-11 assertion
- performance result: no production hot-path work; one getenv check occurs only at watched-state save
- rejected alternatives: chmod-based failure injection (privilege-dependent); changing `.unfinished` semantics (not defective)
- next checkpoint: R11-2 full repair/ownership/watch certification and closure

## R11-2 status
- commit: recorded by the R11-2 closure commit
- semantics changed: none; scenario 11's recovery invariant remains unchanged
- tests added: no additional tests beyond the deterministic R11-1 scenario
- gates run: zero-mutation PASS; repair campaign PASS; ownership/concurrency PASS; control-flow/adversarial PASS; JSON Schema integration PASS. Aggregate `make test` progressed cleanly through parser/content, commands, comments, contracts, JSON, JSON Schema, console and diagnostics before this execution window terminated while compiling Minify++ smoke; no test failure was reported before termination.
- performance result: no algorithmic change; the only production-path addition is one environment lookup at watched-state persistence, negligible relative to filesystem/JSON write work
- rejected alternatives: changing recovery behavior, weakening scenario 11, or classifying a privilege-dependent chmod test as a product defect
- next checkpoint: campaign closed; return to normal v4.1 work/release certification

## Root cause and resolution
Scenario 11 was not a Nift recovery defect. Its test attempted to force a watched-state save failure with `chmod(0555)`. That assumption fails when the runner is root or has DAC-override capability, so the save succeeds, the build succeeds, and `.unfinished` is correctly cleared. This explains why the same failure reproduced at v4.0.13, CP9 and CP24.

The repaired test uses the environment-gated `NIFT_TEST_WATCH_STATE_SAVE_FAIL` seam immediately before the watched `tracked.json` save. This deterministically reaches the intended post-derived-deletion failure on privilege-bearing and ordinary runners alike. The resulting behavior is the originally specified behavior: build fails, derived deletion has occurred, `.unfinished` remains, ordinary build refuses, and `build --repair` recovers and clears the marker.
