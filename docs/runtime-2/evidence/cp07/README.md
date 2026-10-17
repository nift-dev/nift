# CP07 evidence (2026-10-17, Linux x86_64, shared 2 vCPU, load ~15)

- Launch cost table and C microbenchmark: see `../../checkpoints/cp07-process-capture.md`.
- `tests/runtime2/process_capture.sh`: 9/9 PASS (CP07); reference c5db14d:
  8/9 (fails only "no /tmp/nift-proc-* files left": 4 leaked).
- Reviewer probes (CP07 pipe review items 1–6) on reference vs CP07:
  `stat -L /dev/stdout` regular empty file/same; S_ISREG True/same; `echo
  first; echo second > /dev/stdout` → `second`/same; lseek probe `BBAA`/same;
  `/dev/stderr` reopen `y`/same; `tee /dev/stdout` one line/same; descendant
  output after exit not captured/same; child survives SIGTERM to Nift yes/same;
  `ulimit -n 5..8` `hi 0`/same; `yes &` chases the file in both (pre-existing).
  The chatty-descendant probe is nondeterministic in both (7 or 13 bytes).
- Leak root cause: SIGKILL during a capture leaves 2 files per call with the
  reference, 0 with CP07.
- `make test-process-hardening`: 19 cases PASS. Process mutation family
  (`scripts/v411_hardening_mutations.py --family process`): 5/5 caught.
- rt-diff (CP08 snapshot 2fae2d2, traces off) reference vs CP07: 917 entries
  (vm-corpus, semantics, tests sites, extracted smoke, 500 generated, seed 717),
  0 differing; 500 generated entries confirmed in the report.
- Integrity scan: 0 findings.
- Not run, because the launch gate failed and the checkpoint is escalated
  (directive: stop and report): full `make test-all`, regression suite,
  300-page callgrind, rt-diff with 2,000 generated programs.

## Certification run (anonymous-file design, 7bb6173)
- rt-diff (CP08 snapshot 2fae2d2, traces off), reference c5db14d vs CP07,
  committed corpora + extracted smoke + 2,000 generated (seed 707):
  2,417 entries, 0 differing.
- `make test-all`: known environmental set plus `test-v44-language-foundation`
  (wall-clock scaling guard under load: push 40k=144 ms/80k=547 ms); passes 3/3
  alone.
- Regression suite (runtime-2): 92/94; `unreadable_source` (root) and
  `v44_executable_script` (no `nift` on PATH) — both pass in the suite's CI.
- Launch-cost gate: deferred, not passed (D-011).

