# CP14B D4 — Linux compatibility exercise

The pre-controller exercise passed on the authorized integrated foundation
`70fda90227b31f6c7e5ea4ede4b9451047e94c17` (tree
`ad66b69f72494c036a5987837f71aa871f9ede20`).

The test-only Python continuation matches the actual libstdc++ stable sort over
legacy `RuntimeValue` in **234 cases**. It compares every comparator pair and
callback-visible receiver state, final state, failure state and invocation
count. Record moves include legacy's empty moved-from objects. **14 CLI checks**
triangulate against the exact-base Nift binary, including caught/inner-caught
Errors, non-Boolean comparator results and terminal arithmetic errors.

Final pass: 248 child processes, 14,141 callback requests. Including the
preliminary/debug and verification passes: **507 child processes and 42,415
callback requests**, within the approved 512 / 65,536 ceilings. No further
pre-controller profile expansion was performed.

Profile: GCC 15.2, `_GLIBCXX_RELEASE=15`, `__GLIBCXX__=20260321`. Exact compiler,
library/header hashes, binary hash, flags, raw traces and final status are in
the owner workspace `.claude-linode-runs/codex/cp14b/implementation/d4/`
(`profile.json`, `validation.json`). The standalone oracle compiles with:

```
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -Isrc \
  tests/runtime2/cp14b/d4_oracle.cpp -o /tmp/cp14b-d4-oracle
timeout 120 python3 tests/runtime2/cp14b/d4_validate.py \
  --oracle /tmp/cp14b-d4-oracle --nift /tmp/cp14b-base-nift \
  --output /tmp/cp14b-d4-validation.json
```

The driver imposes address-space, CPU, file and per-child timeout limits;
crashes, malformed records and unexpected exits are failures. The experiment
supports the full-buffer Linux profile only. It does not certify arbitrary
inputs, allocation-fallback schedules, libc++ or MSVC, nor does it prove the
later production controller, D5 analysis or allocation-free draining. Those
remain distinct implementation and portability obligations. It is not a
wall-clock performance benchmark. No production controller was implemented
before this exercise passed.
