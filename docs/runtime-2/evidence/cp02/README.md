# CP02 / CP03 evidence (2026-10-17, Linux x86_64, 2 vCPU)

CP02 and CP03 were validated together (CP03 builds on CP02's observations).

## Behaviour tests

| Test | v4.12.0 | CP01 binary | CP02+CP03 |
| --- | --- | --- | --- |
| `tests/runtime2/dynamic_reads.sh` (33 checks, 3 modes) | 24 fail | — | pass |
| `tests/runtime2/dynamic_reads_review.sh` (26 checks) | — | 19 fail | pass |
| `tests/runtime2/fs_freedom.sh` (16 checks) | 14 fail | — | pass |
| `tests/runtime2/modified_race.sh` | fails | pass | pass |
| randomized incremental differential (dynamic ops) | fails at step 0 | — | pass |

## make -k test: failing targets

CP02+CP03 (after review fixes): test-dependency-status, test-progress-pty,
test-unreadable-source, test-v43-language, test-v48-prepared-collection-guard,
test-v48-prepared-object-literal-guard. Identical to the CP01 set; all are
environmental (root user, no /usr/bin/time, no pty) and fail on v4.12.0 too.
The six confinement-contract tests were changed as the D-002 contract change
(see `checkpoints/cp03-filesystem-freedom.md`) and pass.

## Instructions, 300-page Markdown site (callgrind, one session)

| Binary | no-op build | full build |
| --- | ---: | ---: |
| v4.12.0 | 113,778,517 | 821,216,968 |
| CP02+CP03 | 114,915,624 (+1.0%) | 848,328,528 (+3.3%) |

Gate: within +5%. The full-build increase is per-read observation work
(presence probes, listing fingerprints, read hashing in hash mode).

## Large shared listing (review finding #2)

201 pages sharing `ls("big/**/*.txt")` over 10,000 files, wall time:

| Binary | build --all | no-op build | status |
| --- | ---: | ---: | ---: |
| v4.12.0 | 4.79 s | 0.015 s | 0.012 s |
| CP02 before fix | 10.5 s | 5.3 s | 5.3 s |
| CP02+CP03 final | 4.68 s | 0.073 s | 0.065 s |
