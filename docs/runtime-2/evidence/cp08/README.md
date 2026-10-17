# CP08 evidence (2026-10-17, Linux x86_64, 2 vCPU)

## (a) Same binary as A and B

`rt-diff --a nift --b nift` over the committed corpora (vm-corpus, semantics,
cp410 probes, conformance and bindings sites), 262 scripts extracted from the
smoke tests, and 2,500 generated programs (seed 2026), Host-op traces compared:

| Run | Entries | Differing |
| --- | ---: | ---: |
| 1 | 2,995 (2,983 scripts, 12 sites) | 0 |
| 2 | 2,995 | 0 |

Summaries: `gate-a-run1-summary.json`, `gate-a-run2-summary.json`. In each
run 1,359 entries produced Host-op traces (9,198 operations compared). In
`make test-runtime2-rt-diff` (200 generated): 187 entries, 887 Host ops.

All gates were re-run on the final binary (after the trace call sites in
templates were guarded by `nift::trace::enabled()`); the numbers here are from
that re-run.

## (b) Seeded differences

Variants built from a copy of this tree with one change each; A is this
tree's binary. Full DIFF listings (first 60 lines) in `gate-b-*.txt`.

| Variant | Change | Entries differing | Channels |
| --- | --- | ---: | --- |
| v1-trace | one extra `stat` trace line after every `read` | 140 | trace only |
| v2-output | newline appended to every page output file | 7 | files only |
| v3-stderr | "division by zero" → "divide by zero" | 15 | stderr only |
| v4-modulo | integer `%` result made non-negative | 7 | stdout only |
| CP04 experiment | top-level code on the prepared path (`--no-trace`: that binary predates the hook) | 274 | stdout 238, stderr 58, exit 8 |

Negative control: the first v4 build patched only the floating-point modulo
path, which integer operands never reach; rt-diff reported 0 differing, and
`-7 % 3` printed -1 on both binaries.

## (c) Trace hook cost when unset

300-page Markdown site, callgrind, both binaries in one session
(`gate-c.txt`):

| Binary | full build | no-op build |
| --- | ---: | ---: |
| runtime-2 | 800,552,368 | 94,058,887 |
| CP08 | 801,244,016 (+0.09%) | 94,060,881 (+0.002%) |

## (d) Test suite

`make -k -j4 test`: failing targets exactly test-dependency-status,
test-progress-pty, test-unreadable-source, test-v43-language,
test-v48-prepared-collection-guard, test-v48-prepared-object-literal-guard
(environmental: root user, no `/usr/bin/time`, no pty). Inside that run:
`PASS vm corpus: 101 scripts` and `rt-diff: 695 entries, 0 differing`.
All `tests/runtime2/*.sh` pass when run directly.
