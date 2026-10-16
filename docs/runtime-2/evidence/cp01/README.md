# CP01 evidence (2026-10-17, Linux x86_64, 2 vCPU)

## modified_race.sh (1,000 cycles)

```
PASS immediate edits: 0/1000 missed
PASS edit during render
--- baseline v4.12.0 (200 cycles):
FAIL immediate edits: missed 96/200
FAIL edit during render: stale output after a follow-up build
```

## make -k test: failing targets

Baseline v4.12.0:
```
make: *** [Makefile:945: test-v43-language] Error 1
make: *** [Makefile:892: test-v48-prepared-object-literal-guard] Error 1
make: *** [Makefile:898: test-v48-prepared-collection-guard] Error 1
make: *** [Makefile:1067: test-unreadable-source] Error 1
make: *** [Makefile:1073: test-incremental-modified-immediate] Error 1
make: *** [Makefile:268: test-progress-pty] Error 1
make: *** [Makefile:2002: test-consumer-snapshots] Error 1
make: *** [Makefile:2011: test-dependency-status] Error 1
```
CP01:
```
make: *** [Makefile:945: test-v43-language] Error 1
make: *** [Makefile:892: test-v48-prepared-object-literal-guard] Error 1
make: *** [Makefile:898: test-v48-prepared-collection-guard] Error 1
make: *** [Makefile:1067: test-unreadable-source] Error 1
make: *** [Makefile:268: test-progress-pty] Error 1
make: *** [Makefile:2011: test-dependency-status] Error 1
```

All CP01 failures are pre-existing and environmental: three permission tests fail because the sandbox runs as root (test-v43-language/FileValue read-only dir, test-unreadable-source, test-dependency-status denied-parent), two scripts need /usr/bin/time (v48 guards), test-progress-pty depends on the terminal. CP01 fixes test-incremental-modified-immediate and test-consumer-snapshots.

## Instructions, 300-page Markdown site (callgrind)

| Binary | no-op build | full build |
| --- | ---: | ---: |
| v4.12.0 | 102,388,113 | 815,105,975 |
| CP01 (first version, build-wide clock) | 102,519,843 (+0.13%) | 828,020,433 (+1.6%) |

After the review fixes (per-page clock, tick hashes), re-measured on the same
site with both binaries in one session:

| Binary | no-op build | full build |
| --- | ---: | ---: |
| v4.12.0 | 113,778,483 | 821,217,179 |
| CP01 final | 113,991,850 (+0.19%) | 825,333,865 (+0.50%) |

(Absolute counts differ from the first table because the site's page-info
state differed between sessions; compare rows within a table.)

## Review-fix scenarios (same-model adversarial review findings F1, F2)

| Scenario | v4.12.0 | CP01 first version | CP01 final |
| --- | --- | --- | --- |
| Pre-build hook regenerates an input; follow-up builds that rebuild (5 runs × 10) | 1–2/10 | up to 10/10 | 1/10 each run |
| `depends` prerequisite output; spurious dependant rebuilds | 0/45 | 3/9 – 4/45 | 0/45 |
