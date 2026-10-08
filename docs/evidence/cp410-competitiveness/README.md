# CP410 evidence and reproduction

Start with [the investigation report](report.md). This is local v4.10 evidence,
not a replacement for the frozen official `20261009-v490` series.

- `baseline.json`, `identities.json` and `environment.json`: exact source/binary
  identities, flags/toolchain provenance and host environment.
- `baseline-metrics.json`, `hotspots.json`: all 67 freshly executed baseline
  profiles; instructions, allocation requests/bytes, RSS and CPU/wall samples,
  plus leading self/inclusive symbols.
- `phase-metrics.json`, `lifecycle.json`, `destruction.json`, `json-metrics.json`:
  separate diagnostic runs. Phase scopes overlap. Counter builds alter layout
  and are not timing binaries. Aggregate-copy counters include nested levels.
- `capture-comparison.json`, `dispatch-comparison.json`, `final-comparison.json`,
  `rejected-inline.json`, `cpu/`, `scaling.json`, `rss-repeats.json`: each trial,
  initial observations and larger follow-ups, including uncertain controls.
- `baseline/`, `final/`, `contracts/`, `checks/`: actual test logs and certificates.
  The initial incorrectly profiled fuzz failure is retained; the corrected
  certificate uses the maintained deep-capable Make target. The native unary
  boundary comparison excludes only the dynamic build elapsed-time line.
- `oracle-before.json.gz`, `oracle-verification.json`: starting file hashes and
  final audit. Concurrent unrelated Labs publication changes are recorded and
  preserved; frozen results and scripting content are unchanged. Later external
  shell presentation edits are recorded separately, without being reverted.
- `candidate.patch`: the two production changes only. The allocation guard,
  Make/workflow registration and handover changes are reviewed directly in Git.
- `probes/`, `probes.json`, `probe-hashes.json`, `fixtures/`, `fixtures.json`:
  exact local source copies, output oracles and input fixtures. Filesystem tree
  manifests list original empty-file relative names instead of adding thousands
  of empty files to Git. `shell/` retains the bounded process probes and strace.
- `tools/`, `instrumentation/`: harness source, isolated counter sources and
  compile/link recipes. Historical CP49 references in authoring helpers describe
  where component source definitions came from; their metrics/binaries were not
  reused as fresh CP410 results.

## Semantic reproduction without external repository edits

Run from any checkout with Python 3.9+ and a separately built executable:

```sh
python3 docs/evidence/cp410-competitiveness/reproduce.py /absolute/path/to/nift
```

A final argument selects one family, for example `official-sort` or `call-closure`.
The tool creates temporary local inputs, rewrites absolute fixture paths to the
bundled copies, preserves relative module ownership, verifies output and removes
its own temporary tree. It writes nothing to benchmark or Labs repositories.
This reproduces workload semantics; changed fixture/source paths and machine
configuration can change exact instruction/allocation/startup observations.
Use the recorded original source paths for exact same-workspace comparisons.

## Fresh measurements

Build baseline `629b1f2` and candidate in isolated checkouts with the recorded
C++17/O2 Make flags, save both binaries, and run local probes against each.
The measurement harnesses in `tools/` show Callgrind, Memcheck, GNU time and
rotated process CPU/wall invocations. Callgrind/Memcheck use whole processes;
small timings include startup. Run native timing comparisons with no concurrent
build/profile workload and retain every preliminary run, not only the best one.
`final-comparison.json` pairs the saved original-baseline metrics from the capture
trial with the final binary metrics from the dispatch trial. Larger final CPU
measurements directly run the original baseline and final binary together.

Diagnostic reproduction requires an isolated baseline checkout: restore the
instrumentation under `.build/cp410`, copy its baseline core sources/headers into
the lifecycle source directory, apply the retained counter header, and execute
its `build.json` commands. The phase link recipe reuses the freshly built baseline
objects while replacing only the instrumented expression object/allocation hook.
Adjust absolute checkout/include paths to your isolated location. Never use a
counter binary to claim a production speed or ABI result. JSON phase fixtures
and `probe.cpp` are self-contained against canonical `src/Json.h`/`RuntimeJson.h`.

## Permanent gate and safety

```sh
make test-v410-allocation-guard
make test-sanitize-lifetime
make checkpoint-9-parser-fuzz
```

The allocation target requires Valgrind and has no wall-time budget. It is wired
into the existing performance workflow. Its recorded negative controls reject
the original capture defect and the capture-only native-dispatch defect.

Keep the two sanitizer profiles separate. The lifetime profile detects stack
use-after-scope and runs the shallow capture/alias/async corpus; deep recursion
belongs to the maintained deep-capable fuzz target. Its depth guard remains
unchanged. `tools/verify-final.sh` records the exact full campaign certification,
including NRS/PRS; these commands do not publish or push.

No hosted CI run is claimed for the unpushed candidate.
