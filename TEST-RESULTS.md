# Test results — Nift C++ rewrite 1.0.7

The optimized rewrite was validated against the current 280-assertion deep regression
suite. The suite was run in explicit checkpoints to avoid the long-running build-auto
process and accumulated mtime sleeps obscuring completion:

- tests 1–229: PASS
- hash-mode build-auto state-refresh test: PASS
- tests 231–264: PASS
- tests 265–280: PASS

The two historical `info` presentation assertions were updated to parse the rewrite's
intentional JSON-formatted `info` output rather than grep the previous line-oriented
presentation. Their underlying contracts (deduplicated names and exact backslash title
round-trip) remain unchanged.

Additional checks:

- clean `-Wall -Wextra -pedantic` build: PASS
- standalone custom JSON smoke test: PASS
- 10,000-page modified/hash benchmark fixture: PASS
- generated representative HTML compared byte-for-byte between stripped and rewrite: PASS
