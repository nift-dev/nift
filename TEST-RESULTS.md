# Test results — Nift C++ rewrite 1.0.12

Nift 1.0.12 was clean-built and validated against the v6 deep regression
suite in one complete run.

## Result

```text
PASS: 279 assertions/tests
```

The count is intentionally one lower than the previous 280-test baseline because
parsed block comments were removed from the language in 1.0.12. The obsolete
parsed-comment assertion was removed with the feature.

Two older `info` assertions in the suite were also modernised to validate the
JSON result semantically rather than grep legacy presentation text:

- multiple requested names must appear exactly once each;
- titles containing backslashes must round-trip to the exact original string.

These were test-harness updates only; the corresponding Nift behaviour was
already correct.

Additional validation performed on this source tree:

- clean `-std=c++17 -O2 -Wall -Wextra -pedantic -pthread` build: PASS
- tracked-content / nested-`@input` parser smoke test: PASS
- Nift comment semantics smoke test: PASS
- standalone JSON smoke test: PASS
- complete v6 deep regression suite: PASS (279/279)

The full-suite run covered parser behaviour, content/input composition,
metadata and escaping, raw comments, preformatted blocks, tracking and watch
state, malformed persistent JSON, path traversal, build failure propagation,
modified/hash/hybrid incremental behaviour, user dependencies, recursive
directory dependencies, `build-auto`, status/info commands and CLI mutation
operations.
